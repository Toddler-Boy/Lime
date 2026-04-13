# Lime

A small collection of extra modules for [JUCE](https://juce.com/), providing professional-grade utilities, UI components, and more for application development.

## Prerequisites

- **JUCE 8.0+** (included as submodule)
- **CMake 4.2+**
- **C++20** compatible compiler
- **Supported Platforms:** Windows, macOS (10.13+), Linux

## Installation

### Adding Lime to Your CMake Project

1. Add Lime as a submodule:
```bash
git submodule add https://github.com/Toddler-Boy/Lime.git modules/lime
```

2. In your `CMakeLists.txt`:
```cmake
add_subdirectory(modules/lime/modules)

target_link_libraries(YourTarget PRIVATE
    lime_Logger              # For logging
    lime_ShaderToyComponent  # Simple render-pipeline with openGL shaders
    lime_YamlConfig          # YAML-like retrival and storage
    # ... other lime modules as needed
)
```

3. Lime is automatically in the JUCE Header:
```cpp
#include <JuceHeader.h>
```

## Modules

Lime is organized into focused modules that can be included independently:

| Module | Description |
|--------|-------------|
| **lime_Logger** | Basic juce::Logger implementation in a separate Windows/Component |
| **lime_ShaderToyComponent** | Simple render-pipeline with openGL shaders |
| **lime_YamlConfig** | Define a list of possible parameters and load/store them in a YAML-like file |
| **lime_Webcam** | Receives native pixel data (usually YUV12) and provides it in std::function callback |

## Features

### Logger (lime_Logger)
- Call `setCurrentLogger` as early as possible
- Multiple log-levels (debug, log, info, warning, error) with colored rendering
- Logs can be saved into a file

### ShaderToyComponent (lime_ShaderToyComponent)
- Create render pipelines with a simple JSON file or with a few lines of code.
- Everything is hot-reloaded: textures, shaders, and even the JSON itself.
- Shaders are text files in GLSL format and are largely ShaderToy-compatible, except that fragCoord is normalized rather than using pixels.

### YamlConfig (lime_YamlConfig)
- Create list of parameters with paths, fixed data-types and defaults.
- Load from YAML-like file, store into YAML-like file.
- Very fast and easy look up via `get<int> ( "root/child/parameter" )`

### Webcam (lime_Webcam)
- Set a preferred resolution and target fps
- Grabs first camera and starts calling the callback for every frame
- It's recommended to use a shader (openGL, Metal, etc.) to convert to RGB
