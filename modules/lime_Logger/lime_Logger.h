
/*******************************************************************************
 The block below describes the properties of this module, and is read by
 the Projucer to automatically generate project code that uses it.
 For details about the syntax and how to create or use a module, see the
 JUCE Module Format.txt file.


 BEGIN_JUCE_MODULE_DECLARATION

  ID:					lime_logger
  vendor:               Toddler-Boy
  version:				1.0.0
  name:					lime JUCE logging classes
  description:			Classes for easy logging/debugging output
  license:				GPLv3
  minimumCppStandard:	20

  dependencies:     juce_core, juce_events, juce_graphics, juce_gui_basics

 END_JUCE_MODULE_DECLARATION

*******************************************************************************/

#pragma once
#define LIME_LOGGER_H_INCLUDED

#include <optional>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================

#include "Source/lime_Logger.h"
#include "Source/lime_LoggerWindow.h"
#include "Source/lime_LoggerComponent.h"
