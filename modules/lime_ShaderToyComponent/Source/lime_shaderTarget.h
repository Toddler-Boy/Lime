#pragma once

#include <mutex>

#include "lime_shaderTexture.h"
#include "lime_openGL_Quad.h"

namespace lime
{
//-----------------------------------------------------------------------------

class shaderTarget
{
public:
	shaderTarget ( juce::OpenGLContext& oglContext, const juce::String& name );

	void newContext ();
	void losingContext ();
	void render ( float viewportWidth, float viewportHeight, float scale );

	void lock ()	{	rmutex.lock ();		}
	void unlock ()	{	rmutex.unlock ();	}

	void setBounds ( const juce::Rectangle<float>& r );
	juce::Rectangle<float> getBounds () const;
	void setBounds ( const juce::Rectangle<int>& r );
	void setSize ( const int width, const int height );

	void setVertices ( const std::array<std::array<float, 4>, 4>& v );

	template <typename T>
	void setPointSprites ( std::span<const T> data )
	{
		static_assert ( std::is_standard_layout_v<T>, "Point sprite struct must be POD" );

		// Capture the raw float data and how many floats per struct
		pointSpriteData = { reinterpret_cast<const float*> ( data.data () ), data.size_bytes () / sizeof ( float ) };
		pointSpriteStride = sizeof ( T ) / sizeof ( float );

		glQuad.setPointSprites ( pointSpriteData, pointSpriteStride );
	}

	void setName ( const juce::String& name );
	juce::String& getName () { return name; }

	void setEnabled ( const bool _enabled )		{ enabled = _enabled;	}

	void setAutoSize ( const bool _autoSize )	{ autoSize = _autoSize;	}
	bool wantsViewportSize () const				{ return autoSize;	}

	void setMeasurePerformance ( const bool _enabled ) { measurePerformance = _enabled; }
	float getMeasuredTimeMs () const { return glQuad.getElapsedTimeMs (); }

	void setShaders ( const juce::String& shaderStr );

	enum BlendMode : int8_t
	{
		normal,
		add,
	};

	void setEnableBlend ( const bool _enabled, const bool _premultipliedAlpha = true, const BlendMode mode = normal )
	{
		blendEnabled = _enabled;
		premultipliedAlpha = _premultipliedAlpha;
		blendMode = mode;
	}

	void setTexture ( const int index, shaderTexture* txt );
	shaderTexture* getTexture ( const int index )	{	return textures[ index ].tex;	}

	void setTextureBorderColor ( const int index, juce::Colour col );
	void setTextureFilter ( const int index, const bool linear );
	void setTextureClampMode ( const int index, const int mode );

	void setUniform_f ( const std::string& name, const float n1 );
	void setUniform_i ( const std::string& name, const int n1 );
	void setUniform_vec2 ( const std::string& name, const float n1, const float n2 );
	void setUniform_vec3 ( const std::string& name, const float n1, const float n2, const float n3 );
	void setUniform_vec4 ( const std::string& name, const float n1, const float n2, const float n3, const float n4 );
	void setUniform_ivec4 ( const std::string& name, const int n1, const int n2, const int n3, const int n4 );
	void setUniform_f ( const std::string& name, const std::span<const float>& values );
	void setUniform_f ( const std::string& name, const std::initializer_list<const float>& values );

	void setTargetBuffer ( shaderTexture* txt );
	void setTargetBackgroundColor ( juce::Colour col );

	void setBufferSizePixels ( const int w, const int h );
	void setBufferSizePixelsScaled ( const int w, const int h );
	void setBufferSizeRelative ( const float s );
	void setBufferSizeRelative ( const float sw, const float sh );
	void setBufferSizeSquareRelative ( const float s );

private:
	juce::OpenGLContext&	openGLContext;
	juce::String			name;

	openGL_Quad				glQuad;

	GLfloat	backgroundColor[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };

	bool	enabled = true;
	bool	autoSize = false;
	bool	measurePerformance = false;

	bool	blendEnabled = true;
	bool	premultipliedAlpha = true;
	BlendMode	blendMode = normal;

	GLfloat		vertices[ 4 ][ 4 ] = {};

	//
	// Vertices
	//
	std::array<openGL_Quad::vertex, 4>	vertexBuffer {};

	//
	// Point sprite data
	//
	std::span<const float>	pointSpriteData;
	int						pointSpriteStride = 0;

	//
	// Textures
	//
	struct shaderTextureMeta
	{
		shaderTexture* tex = nullptr;

		GLfloat	borderColor[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };
		bool	linearFilter = true;
		GLint	clampMode = juce::gl::GL_CLAMP_TO_EDGE;
	};

	static constexpr auto	maxTextures = 16;
	std::array<shaderTextureMeta, maxTextures>	textures {};

	//
	// Shaders
	//
	void compileOpenGLShaders ();
	juce::String	openGLStatus;

	std::recursive_mutex	rmutex;
	std::atomic<bool>		shaderUpdated = false;
	std::string				vertexShaderProgramStr;
	std::string				fragmentShaderProgramStr;
	std::unique_ptr<juce::OpenGLShaderProgram>	shaderProgram;

	//
	// Uniforms
	//
	struct uniform
	{
		std::unique_ptr<juce::OpenGLShaderProgram::Uniform>	uniformPtr;
		union
		{
			float	fValues[ 4 ];
			int		iValues[ 4 ];
		};
		bool	isFloat;
		int		count;

		void update ();
	};
	std::unordered_map<std::string, uniform>	uniforms;

	void removeUniform ( const std::string& name );
	void resetUniforms ();
	void updateUniforms ();

	//
	// Target buffer
	//
	shaderTexture*	targetBuffer = nullptr;

	//
	// Remember sizes and size-modes
	//
	enum autoSizeModes : int8_t
	{
		none = 0,
		pixels,
		pixelsScaled,
		relative,
		relativeSmaller,
	};

	int		sizeMode = autoSizeModes::relative;

	int		pixWidth = 0;
	int		pixHeight = 0;

	float	relWidth = 1.0f;
	float	relHeight = 1.0f;
};
//-----------------------------------------------------------------------------
}