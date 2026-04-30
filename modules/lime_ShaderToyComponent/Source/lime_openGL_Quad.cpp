#include "lime_openGL_Quad.h"

namespace lime
{
//-----------------------------------------------------------------------------

openGL_Quad::~openGL_Quad ()
{
	release ();
}
//-----------------------------------------------------------------------------

void openGL_Quad::release ()
{
	// If the buffers are deleted while the owner context is not active, it's
	// impossible to delete them, so this will be a leak until the context itself
	// is deleted.
	jassert ( ( ! quadVBO && ! quadIBO ) || ownerContext == juce::OpenGLContext::getCurrentContext () );

	if ( ! ownerContext )
		return;

	safeDeleteQuery ( queryID[ 0 ] );
	safeDeleteQuery ( queryID[ 1 ] );

	safeDeleteBuffers ( quadVBO );
	safeDeleteBuffers ( quadIBO );
	safeDeleteVAO ( quadVAO );

	releaseFeedback ();
}
//-----------------------------------------------------------------------------

void openGL_Quad::newContext ()
{
	ownerContext = juce::OpenGLContext::getCurrentContext ();

	if ( ! ownerContext )
		return;

	auto&	ogl = ownerContext->extensions;

	if ( pendingData.empty () )
	{
		ogl.glGenVertexArrays ( 1, &quadVAO );

		// Vertex data
		ogl.glGenBuffers ( 1, &quadVBO );

		// Indices data
		ogl.glGenBuffers ( 1, &quadIBO );
		ogl.glBindBuffer ( juce::gl::GL_ELEMENT_ARRAY_BUFFER, quadIBO );
		ogl.glBufferData ( juce::gl::GL_ELEMENT_ARRAY_BUFFER, sizeof ( unsigned int ) * indexBuffer.size (), indexBuffer.data (), juce::gl::GL_STATIC_DRAW );
	}
	else
	{
		auto	totalCount = 0;
		for ( const auto& v : pendingLayout )
			totalCount += v.components;

		setupFeedbackBuffers ( pendingData.size () / totalCount, pendingData.data (), pendingLayout );
	}
}
//-----------------------------------------------------------------------------

void openGL_Quad::setVertices ( const std::array<vertex, 4>& vertexBuffer )
{
	auto&	ogl = ownerContext->extensions;

	// Copy vertices to GPU
	ogl.glBindBuffer ( juce::gl::GL_ARRAY_BUFFER, quadVBO );
	ogl.glBufferData ( juce::gl::GL_ARRAY_BUFFER, vertexBuffer.size () * sizeof ( vertex ), vertexBuffer.data (), juce::gl::GL_STREAM_DRAW );
	ogl.glBindBuffer ( juce::gl::GL_ARRAY_BUFFER, 0 );
}
//-----------------------------------------------------------------------------

void openGL_Quad::draw ()
{
	auto& ogl = ownerContext->extensions;

	if ( isFeedbackMode )
	{
		juce::gl::glEnable ( juce::gl::GL_PROGRAM_POINT_SIZE );

		ogl.glBindVertexArray ( tfVAO[ readIdx ] );

		juce::gl::glDrawArrays ( juce::gl::GL_POINTS, 0, particleCount );

		ogl.glBindVertexArray ( 0 );

		juce::gl::glDisable ( juce::gl::GL_PROGRAM_POINT_SIZE );
	}
	else
	{
		ogl.glBindVertexArray ( quadVAO );

		ogl.glBindBuffer ( juce::gl::GL_ARRAY_BUFFER, quadVBO );
		ogl.glBindBuffer ( juce::gl::GL_ELEMENT_ARRAY_BUFFER, quadIBO );

		// Tell the vertex-shader where the "position" attribute is (index 0, length 4, offset 0)
		ogl.glVertexAttribPointer ( 0, 4, juce::gl::GL_FLOAT, juce::gl::GL_FALSE, sizeof ( vertex ), (void*)0 );
		ogl.glEnableVertexAttribArray ( 0 );

		// Tell the vertex-shader where the "texture-coordinates" attribute is (index 1, length 2, offset 4)
		ogl.glVertexAttribPointer ( 1, 2, juce::gl::GL_FLOAT, juce::gl::GL_FALSE, sizeof ( vertex ), (void*)( 4 * sizeof ( float ) ) );
		ogl.glEnableVertexAttribArray ( 1 );

		juce::gl::glDrawElements ( juce::gl::GL_TRIANGLE_STRIP, GLsizei ( indexBuffer.size () ), juce::gl::GL_UNSIGNED_INT, nullptr );

		ogl.glBindBuffer ( juce::gl::GL_ARRAY_BUFFER, 0 );
		ogl.glBindBuffer ( juce::gl::GL_ELEMENT_ARRAY_BUFFER, 0 );
	}
}
//-----------------------------------------------------------------------------

void openGL_Quad::releaseFeedback ()
{
	if ( ! ownerContext )
		return;

	// Ensure we are on the message thread/GL thread as per JUCE requirements
	safeDeleteBuffers ( tfVBO[ 0 ] );
	safeDeleteBuffers ( tfVBO[ 1 ] );

	safeDeleteVAO ( tfVAO[ 0 ] );
	safeDeleteVAO ( tfVAO[ 1 ] );

	safeDeleteTFO ( tfo[ 0 ] );
	safeDeleteTFO ( tfo[ 1 ] );

	isFeedbackMode = false;
	particleCount = 0;
	readIdx = 0;
	writeIdx = 1;
}
//-----------------------------------------------------------------------------

void openGL_Quad::setFeedbackData ( std::span<const float> data, std::span<const feedbackVarying> layout )
{
	pendingData.assign ( data.begin (), data.end () );
	pendingLayout.assign ( layout.begin (), layout.end () );
}
//-----------------------------------------------------------------------------

void openGL_Quad::setupFeedbackBuffers ( const int count, const float* data, std::span<const feedbackVarying> varyings )
{
	releaseFeedback (); // Clean up existing buffers if any

	isFeedbackMode = true;
	particleCount = count;

	auto	totalStride = 0;
	for ( const auto& v : varyings )
		totalStride += v.components;

	juce::gl::glGenBuffers ( 2, tfVBO );
	juce::gl::glGenVertexArrays ( 2, tfVAO );
	juce::gl::glGenTransformFeedbacks ( 2, tfo );

	for ( auto i = 0; i < 2; ++i )
	{
		// Initialize VAOs (how to read the data)
		juce::gl::glBindVertexArray ( tfVAO[ i ] );

		// Initialize VBOs with your starting data
		juce::gl::glBindBuffer ( juce::gl::GL_ARRAY_BUFFER, tfVBO[ i ] );
		juce::gl::glBufferData ( juce::gl::GL_ARRAY_BUFFER, totalStride * count * sizeof ( float ), data, juce::gl::GL_STREAM_DRAW );

		auto	offset = 0;
		for ( auto loc = 0; loc < int ( varyings.size () ); ++loc )
		{
			const auto	components = varyings[ loc ].components;

			juce::gl::glEnableVertexAttribArray ( loc );
			juce::gl::glVertexAttribPointer ( loc, components, juce::gl::GL_FLOAT, juce::gl::GL_FALSE, totalStride * sizeof ( float ), reinterpret_cast<const void*> ( offset ) );

			offset += components * sizeof ( float );
		}

		// Initialize TFOs (where to write the data)
		juce::gl::glBindTransformFeedback ( juce::gl::GL_TRANSFORM_FEEDBACK, tfo[ i ] );
		juce::gl::glBindBufferBase ( juce::gl::GL_TRANSFORM_FEEDBACK_BUFFER, 0, tfVBO[ i ] );
	}

	juce::gl::glBindVertexArray ( 0 );
	juce::gl::glBindTransformFeedback ( juce::gl::GL_TRANSFORM_FEEDBACK, 0 );
}
//-----------------------------------------------------------------------------

void openGL_Quad::bindForUpdate ()
{
	juce::gl::glBindBuffer ( juce::gl::GL_ARRAY_BUFFER, 0 );

	juce::gl::glBindVertexArray ( tfVAO[ readIdx ] );
	juce::gl::glBindTransformFeedback ( juce::gl::GL_TRANSFORM_FEEDBACK, tfo[ writeIdx ] );
	juce::gl::glBindBufferBase ( juce::gl::GL_TRANSFORM_FEEDBACK_BUFFER, 0, tfVBO[ writeIdx ] );
}
//-----------------------------------------------------------------------------

void openGL_Quad::bindForRender ()
{
	juce::gl::glBindVertexArray ( tfVAO[ readIdx ] );
	juce::gl::glBindTransformFeedback ( juce::gl::GL_TRANSFORM_FEEDBACK, 0 );
}
//-----------------------------------------------------------------------------

void openGL_Quad::swapFeedbackBuffers ()
{
	std::swap ( readIdx, writeIdx );
}
//-----------------------------------------------------------------------------

void openGL_Quad::beginMeasurement ()
{
	juce::gl::glBeginQuery ( juce::gl::GL_TIME_ELAPSED, queryID[ queryIndex ] );
}
//-----------------------------------------------------------------------------

void openGL_Quad::endMeasurement ()
{
	juce::gl::glEndQuery ( juce::gl::GL_TIME_ELAPSED );

	queryIndex ^= 1;

	if ( juce::gl::glIsQuery ( queryID[ queryIndex ] ) )
	{
		juce::gl::glGetQueryObjectui64v ( queryID[ queryIndex ], juce::gl::GL_QUERY_RESULT, &elapsedTime );
		elapsedTimeMs = float ( double ( elapsedTime ) / 1000000.0 );
	}
}
//-----------------------------------------------------------------------------

void openGL_Quad::safeDeleteBuffers ( GLuint& buffer )
{
	juce::gl::glDeleteBuffers ( 1, &buffer );
	buffer = 0;
}
//-----------------------------------------------------------------------------

void openGL_Quad::safeDeleteVAO ( GLuint& vao )
{
	juce::gl::glDeleteVertexArrays ( 1, &vao );
	vao = 0;
}
//-----------------------------------------------------------------------------

void openGL_Quad::safeDeleteTFO ( GLuint& tfo )
{
	juce::gl::glDeleteTransformFeedbacks ( 1, &tfo );
	tfo = 0;
}
//-----------------------------------------------------------------------------

void openGL_Quad::safeDeleteQuery ( GLuint& query )
{
	juce::gl::glDeleteQueries ( 1, &query );
	query = 0;
}
//-----------------------------------------------------------------------------
}