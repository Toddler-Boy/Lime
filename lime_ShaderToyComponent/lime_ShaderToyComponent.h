
/*******************************************************************************
 The block below describes the properties of this module, and is read by
 the Projucer to automatically generate project code that uses it.
 For details about the syntax and how to create or use a module, see the
 JUCE Module Format.txt file.


 BEGIN_JUCE_MODULE_DECLARATION

  ID:					lime_ShaderToyComponent
  vendor:				Toddler-Boy
  version:				1.0.0
  name:					lime JUCE ShaderToyComponent class
  description:			Classes for creating simple openGL render-pipelines with glsl shaders
  license:				GPLv3
  minimumCppStandard:	20

  dependencies:     juce_core, juce_events, juce_graphics, juce_gui_basics, juce_opengl, gin, refx_logging

 END_JUCE_MODULE_DECLARATION

*******************************************************************************/

#pragma once
#define LIME_SHADERTOY_COMPONENT_H_INCLUDED

#include <optional>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>
#include <gin/gin.h>
#include <refx_logging/refx_logging.h>

//==============================================================================

#include "Source/lime_openGL_Image.h"
#include "Source/lime_openGL_Quad.h"
#include "Source/lime_openGL_Texture.h"
#include "Source/lime_shaderTarget.h"
#include "Source/lime_shaderTexture.h"
#include "Source/lime_ShaderToyComponent.h"
