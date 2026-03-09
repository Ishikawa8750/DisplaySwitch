/**
 * DisplaySwitch Native — Linux VCP Controller
 *
 * DDC/CI over i2c-dev.
 * The actual implementation is in display_detector_linux.cpp
 * (set_brightness / get_brightness / set_input / get_input methods).
 *
 * This file exists for source structure consistency across platforms.
 */
#ifdef __linux__

#include "displayswitch/display_detector.h"

// All VCP control logic is implemented directly in LinuxDisplayDetector
// (display_detector_linux.cpp) since i2c-dev access is tightly coupled
// with the display detection. This file exists for CMake source structure.

#endif // __linux__
