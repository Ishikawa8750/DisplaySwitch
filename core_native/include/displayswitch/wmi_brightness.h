/**
 * DisplaySwitch Native — WMI Brightness Control for Laptop Internal Displays
 *
 * Uses WMI WmiMonitorBrightness / WmiMonitorBrightnessMethods to control
 * brightness on internal displays (eDP / LVDS) that do not support DDC/CI.
 *
 * These WMI classes are only available on laptop internal panels.
 */

#pragma once

#include <cstdint>

namespace displayswitch {

/**
 * @brief Check if WMI brightness control is available (i.e. laptop internal panel exists).
 * @return true if at least one WmiMonitorBrightness instance is available.
 */
bool wmi_brightness_available();

/**
 * @brief Get the current brightness of the internal display via WMI.
 * @return Brightness level (0–100), or -1 on failure.
 */
int wmi_get_brightness();

/**
 * @brief Set the brightness of the internal display via WMI.
 * @param level Brightness level (0–100).
 * @return true on success, false on failure.
 */
bool wmi_set_brightness(int level);

} // namespace displayswitch
