#ifdef LIME_WEBCAM_H_INCLUDED
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
 */
 #error "Incorrect use of lime::Webcam cpp file"
#endif

#include "lime_Webcam.h"

#include "Source/lime_Webcam.cpp"
#include "Source/lime_sr_webcam.cpp"
#include "Source/lime_sr_webcam_linux.cpp"
#include "Source/lime_sr_webcam_mac.mm"
#include "Source/lime_sr_webcam_windows.cpp"
