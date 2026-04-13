
/*******************************************************************************
 The block below describes the properties of this module, and is read by
 the Projucer to automatically generate project code that uses it.
 For details about the syntax and how to create or use a module, see the
 JUCE Module Format.txt file.


 BEGIN_JUCE_MODULE_DECLARATION

  ID:					lime_CRTEmulation
  vendor:				Toddler-Boy
  version:				1.0.0
  name:					lime CRTEmulation class
  description:			Class to store/retrieve yaml-like files into a quick look-up
  license:				GPLv3
  minimumCppStandard:	20

  dependencies:     juce_core, juce_events, juce_graphics, juce_gui_basics, juce_opengl, lime_ShaderToyComponent, lime_YamlConfig, lime_Webcam, lime_Logger

 END_JUCE_MODULE_DECLARATION

*******************************************************************************/

#pragma once
#define LIME_CRT_EMULATION_H_INCLUDED

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

//==============================================================================

#include <lime_ShaderToyComponent/lime_ShaderToyComponent.h>
#include <lime_YamlConfig/lime_YamlConfig.h>
#include <lime_Webcam/lime_Webcam.h>
#include "Source/lime_CRTEmulation.h"
