/**
 * DisplaySwitch Native — Display Detector (shared logic)
 * Bandwidth calculation used by all platforms.
 */
#include "displayswitch/display_detector.h"

namespace displayswitch {

BandwidthInfo calculate_bandwidth(const std::string& connection_type,
                                  const std::string& hdmi_version,
                                  uint16_t max_tmds_mhz,
                                  uint8_t frl_rate) {
    BandwidthInfo bw;
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(c));
        return s;
    };

    std::string conn = lower(connection_type);

    if (conn.find("hdmi") != std::string::npos) {
        if (frl_rate >= 6) {
            bw.max_bandwidth_gbps = 48.0;
            bw.bandwidth_str      = "48 Gbps (HDMI 2.1 FRL6)";
            bw.can_support_4k60   = true;
            bw.can_support_4k120  = true;
            bw.can_support_8k60   = true;
        } else if (frl_rate >= 3) {
            bw.max_bandwidth_gbps = 24.0;
            bw.bandwidth_str      = "24 Gbps (HDMI 2.1 FRL3)";
            bw.can_support_4k60   = true;
            bw.can_support_4k120  = true;
        } else if (max_tmds_mhz >= 600) {
            bw.max_bandwidth_gbps = 18.0;
            bw.bandwidth_str      = "18 Gbps (HDMI 2.0)";
            bw.can_support_4k60   = true;
        } else if (max_tmds_mhz >= 340) {
            bw.max_bandwidth_gbps = 10.2;
            bw.bandwidth_str      = "10.2 Gbps (HDMI 2.0)";
            bw.can_support_4k60   = true;
        } else {
            bw.max_bandwidth_gbps = 10.2;
            bw.bandwidth_str      = "10.2 Gbps (HDMI 1.4)";
        }
    } else if (conn.find("displayport") != std::string::npos || conn.find("dp") != std::string::npos) {
        bw.max_bandwidth_gbps = 25.92;
        bw.bandwidth_str      = "25.92 Gbps (DP 1.4 est.)";
        bw.can_support_4k60   = true;
        bw.can_support_4k120  = true;
    } else if (conn.find("internal") != std::string::npos) {
        bw.max_bandwidth_gbps = 32.4;
        bw.bandwidth_str      = "eDP (Internal)";
        bw.can_support_4k60   = true;
    }

    return bw;
}

} // namespace displayswitch

namespace displayswitch {

#if !defined(__APPLE__)
std::string get_thunderbolt_topology_json() {
    // Thunderbolt/USB4 topology enumeration is currently implemented on macOS only.
    return "[]";
}
#endif

} // namespace displayswitch
