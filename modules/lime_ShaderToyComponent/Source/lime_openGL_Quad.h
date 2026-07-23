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

	struct feedbackVarying
	{
		std::string name;			// e.g., "outPos"
		int			components;		// e.g., 3 for vec3
	};

	openGL_Quad () = default;
	~openGL_Quad ();

	// Resource management
	void release ();
	void newContext ();

	// Set the vertices of the quad
	void setVertices ( const std::array<vertex, 4>& vertexBuffer );
	void draw ();

	// Transform feedback for particle systems
	void setFeedbackData ( std::span<const float> data, std::span<const feedbackVarying> layout );
	void bindForUpdate ();
	void bindForRender ();
	void swapFeedbackBuffers ();

	int getParticleCount () const { return particleCount; }

	// Performance measurement
	void beginMeasurement ();
	void endMeasurement ();
	float getElapsedTimeMs () const { return elapsedTimeMs; }

private:
	void safeDeleteBuffers ( GLuint& buffer );
	void safeDeleteVAO ( GLuint& vao );
	void safeDeleteTFO ( GLuint& tfo );
	void safeDeleteQuery ( GLuint& query );

	juce::OpenGLContext*	ownerContext = nullptr;

	// Performance measurement
	int			queryIndex = 0;
	GLuint64	elapsedTime = 0;
	float		elapsedTimeMs = 0.0f;
	GLuint		queryID[ 2 ] = { 0, 0 };

	// Vextex and indice buffers
	GLuint		quadVAO = 0;
	GLuint		quadVBO = 0;
	GLuint		quadIBO = 0;
	std::array<unsigned int, 4>		indexBuffer = { 0, 1, 3, 2 };

	// Transform feedback housekeeping
	bool	isFeedbackMode = false;
	int		particleCount = 0;
	int		readIdx = 0;
	int		writeIdx = 1;

	GLuint	tfo[ 2 ] = { 0, 0 }; // Transform Feedback Objects
	GLuint	tfVBO[ 2 ] = { 0, 0 };
	GLuint	tfVAO[ 2 ] = { 0, 0 };

	std::vector<float>				pendingData;
	std::vector<feedbackVarying>	pendingLayout;

	void releaseFeedback ();
	void setupFeedbackBuffers ( const int count, const float* data, std::span<const feedbackVarying> varyings );

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR ( openGL_Quad )
};
//-----------------------------------------------------------------------------
}