#ifdef LIME_CRT_EMULATION_H_INCLUDED
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
 */
 #error "Incorrect use of lime::CRTEmulation cpp file"
#endif

#include "lime_CRTEmulation.h"

#include "Source/lime_CRTEmulation.cpp"
#include "Source/lime_CRT_DustParticles.cpp"
