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
	jassert ( ( ! vbo && ! ibo ) || ownerContext == juce::OpenGLContext::getCurrentContext () );

	if ( ! ownerContext )
		return;

	auto&	ogl = ownerContext->extensions;

	juce::gl::glDeleteQueries ( 2, &queryID[ 0 ] );

	ogl.glDeleteBuffers ( 1, &vbo );
	ogl.glDeleteBuffers ( 1, &ibo );

	queryID[ 0 ] = queryID[ 1 ] = 0;
	vbo = 0;
	ibo = 0;
}
//-----------------------------------------------------------------------------

void openGL_Quad::newContext ()
{
	ownerContext = juce::OpenGLContext::getCurrentContext ();

	if ( ! ownerContext )
		return;

	auto&	ogl = ownerContext->extensions;

	// Vertex data
	ogl.glGenBuffers ( 1, &vbo );

	// Indices data
	ogl.glGenBuffers ( 1, &ibo );
	ogl.glBindBuffer ( juce::gl::GL_ELEMENT_ARRAY_BUFFER, ibo );
	ogl.glBufferData ( juce::gl::GL_ELEMENT_ARRAY_BUFFER, sizeof ( unsigned int ) * indexBuffer.size (), indexBuffer.data (), juce::gl::GL_STATIC_DRAW );
}
//-----------------------------------------------------------------------------

void openGL_Quad::setVertices ( const std::array<vertex, 4>& vertexBuffer )
{
	auto&	ogl = ownerContext->extensions;

	// Copy vertices to GPU
	ogl.glBindBuffer ( juce::gl::GL_ARRAY_BUFFER, vbo );
	ogl.glBufferData ( juce::gl::GL_ARRAY_BUFFER, vertexBuffer.size () * sizeof ( vertex ), vertexBuffer.data (), juce::gl::GL_STREAM_DRAW );

	// Tell the vertex-shader where the "position" attribute is (index 0, length 4, offset 0)
	ogl.glVertexAttribPointer ( 0, 4, juce::gl::GL_FLOAT, juce::gl::GL_FALSE, sizeof ( vertex ), (void*)0 );
	ogl.glEnableVertexAttribArray ( 0 );

	// Tell the vertex-shader where the "texture-coordinates" attribute is (index 1, length 2, offset 4)
	ogl.glVertexAttribPointer ( 1, 2, juce::gl::GL_FLOAT, juce::gl::GL_FALSE, sizeof ( vertex ), (void*)( 4 * sizeof ( float ) ) );
	ogl.glEnableVertexAttribArray ( 1 );
}
//-----------------------------------------------------------------------------

void openGL_Quad::draw ()
{
	auto&	ogl = ownerContext->extensions;

	ogl.glBindBuffer ( juce::gl::GL_ELEMENT_ARRAY_BUFFER, ibo );
	juce::gl::glDrawElements ( juce::gl::GL_TRIANGLE_STRIP, GLsizei ( indexBuffer.size () ), juce::gl::GL_UNSIGNED_INT, nullptr );

	ogl.glDisableVertexAttribArray ( 1 );
	ogl.glDisableVertexAttribArray ( 0 );
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