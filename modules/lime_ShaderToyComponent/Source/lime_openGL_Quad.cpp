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

	auto&	ogl = ownerContext->extensions;

	juce::gl::glDeleteQueries ( 2, &queryID[ 0 ] );
	queryID[ 0 ] = queryID[ 1 ] = 0;

	auto deleteBuffersSafe = [ &ogl ] ( GLuint& buffer )
	{
		if ( buffer )
			ogl.glDeleteBuffers ( 1, &buffer );
		buffer = 0;
	};

	auto safeDeleteVAO = [ &ogl ] ( GLuint& vao )
	{
		if ( vao )
			ogl.glDeleteVertexArrays ( 1, &vao );
		vao = 0;
	};

	deleteBuffersSafe ( quadVBO );
	deleteBuffersSafe ( quadIBO );
	safeDeleteVAO ( quadVAO );

	safeDeleteVAO ( instanceVAO );
	deleteBuffersSafe ( instanceVBO );
}
//-----------------------------------------------------------------------------

void openGL_Quad::newContext ()
{
	ownerContext = juce::OpenGLContext::getCurrentContext ();

	if ( ! ownerContext )
		return;

	auto&	ogl = ownerContext->extensions;

	if ( instanceData.empty () )
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
		// Create the Point-Sprite VAO
		ogl.glGenVertexArrays ( 1, &instanceVAO );
		ogl.glBindVertexArray ( instanceVAO );

		// Create and bind the Instance VBO
		ogl.glGenBuffers ( 1, &instanceVBO );
		ogl.glBindBuffer ( juce::gl::GL_ARRAY_BUFFER, instanceVBO );

		// Map location 0 to our instance data
		ogl.glEnableVertexAttribArray ( 0 );

		ogl.glBindVertexArray ( 0 );
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

void openGL_Quad::setInstances ( std::span<const float> _instanceData, int _instanceStride )
{
	instanceData = _instanceData;
	instanceStride = _instanceStride;
}
//-----------------------------------------------------------------------------

void openGL_Quad::draw ()
{
	auto& ogl = ownerContext->extensions;

	if ( instanceData.empty () )
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
	else
	{
		juce::gl::glEnable ( juce::gl::GL_PROGRAM_POINT_SIZE );
		ogl.glBindVertexArray ( instanceVAO );

		// Ensure location 1 (UV) from the Quad pass is disabled for points
		ogl.glDisableVertexAttribArray ( 1 );

		// Upload the generic span data
		ogl.glBindBuffer ( juce::gl::GL_ARRAY_BUFFER, instanceVBO );
		ogl.glBufferData ( juce::gl::GL_ARRAY_BUFFER, instanceData.size_bytes (), instanceData.data (), juce::gl::GL_STREAM_DRAW );

		// Map the generic stride to location 0
		ogl.glEnableVertexAttribArray ( 0 );
		ogl.glVertexAttribPointer ( 0, instanceStride, juce::gl::GL_FLOAT, juce::gl::GL_FALSE, sizeof ( float ) * instanceStride, nullptr );

		// Draw all points in one go
		juce::gl::glDrawArrays ( juce::gl::GL_POINTS, 0, (GLsizei)( instanceData.size () / instanceStride ) );

		ogl.glBindVertexArray ( 0 );
		ogl.glBindBuffer ( juce::gl::GL_ARRAY_BUFFER, 0 );
		juce::gl::glDisable ( juce::gl::GL_PROGRAM_POINT_SIZE );
	}
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
}