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
	void setInstances ( std::span<const float> instanceData, int instanceStride );
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
	GLuint		quadVAO = 0;
	GLuint		quadVBO = 0;
	GLuint		quadIBO = 0;
	std::array<unsigned int, 4>		indexBuffer = { 0, 1, 3, 2 };

	// Instance data in case we want to use point-sprites
	GLuint	instanceVAO = 0;
	GLuint	instanceVBO = 0;
	std::span<const float>	instanceData;
	int		instanceStride = 0;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR ( openGL_Quad )
};
//-----------------------------------------------------------------------------
}