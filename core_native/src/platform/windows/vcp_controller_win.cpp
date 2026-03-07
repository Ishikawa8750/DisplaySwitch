/**
 * DisplaySwitch Native — Windows VCP Controller
 *
 * DDC/CI brightness and input source control.
 * The actual implementation is in display_detector_win.cpp
 * (set_brightness, get_brightness, set_input, get_input on WindowsDisplayDetector).
 *
 * This file provides standalone VCP utility functions.
 */
#ifdef _WIN32

#include "displayswitch/display_detector.h"

#include <Windows.h>
#include <LowLevelMonitorConfigurationAPI.h>
#include <PhysicalMonitorEnumerationAPI.h>

#pragma comment(lib, "dxva2.lib")

namespace displayswitch {

/**
 * Read arbitrary VCP code.
 * @return pair of (current, max). Returns (-1,-1) on failure.
 */
std::pair<int, int> read_vcp(void* physical_handle, uint8_t vcp_code) {
    if (!physical_handle) return {-1, -1};
    DWORD cur = 0, max_v = 0;
    MC_VCP_CODE_TYPE ct;
    if (GetVCPFeatureAndVCPFeatureReply(
            static_cast<HANDLE>(physical_handle), vcp_code, &ct, &cur, &max_v))
        return {static_cast<int>(cur), static_cast<int>(max_v)};
    return {-1, -1};
}

/**
 * Write arbitrary VCP code.
 */
bool write_vcp(void* physical_handle, uint8_t vcp_code, uint32_t value) {
    if (!physical_handle) return false;
    return SetVCPFeature(static_cast<HANDLE>(physical_handle), vcp_code, value) != 0;
}

} // namespace displayswitch

#endif // _WIN32
