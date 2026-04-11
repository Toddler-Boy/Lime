#pragma once

namespace lime
{
//-----------------------------------------------------------------------------

class openGL_Quad
{
public:
	struct vertex
	{
		float	position[ 4 ];
		float	textureUV[ 2 ];
	};

	openGL_Quad () = default;
	~openGL_Quad ();

	// Resource management
	void release ();
	void newContext ();

	// Set the vertices of the quad
	void setVertices ( const std::array<vertex, 4>& vertexBuffer );
	void draw ();

	// Performance measurement
	void beginMeasurement ();
	void endMeasurement ();
	float getElapsedTimeMs () const { return elapsedTimeMs; }

private:
	juce::OpenGLContext*	ownerContext = nullptr;

	// Performance measurement
	int			queryIndex = 0;
	GLuint64	elapsedTime = 0;
	float		elapsedTimeMs = 0.0f;
	GLuint		queryID[ 2 ];

	// Vextex and indice buffers
	GLuint		vbo = 0;

	GLuint		ibo = 0;
	std::array<unsigned int, 4>		indexBuffer = { 0, 1, 3, 2 };

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR ( openGL_Quad )
};
//-----------------------------------------------------------------------------
}