/**
 * DisplaySwitch Native — GPU Info (platform-agnostic interface)
 */
#include "displayswitch/display_detector.h"

namespace displayswitch {

std::string GPUInfo::formatted_name() const {
    if (dedicated_vram_bytes == 0) return name;
    double gb = dedicated_vram_bytes / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1.0) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s (%.1fGB)", name.c_str(), gb);
        return buf;
    }
    double mb = dedicated_vram_bytes / (1024.0 * 1024.0);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s (%.0fMB)", name.c_str(), mb);
    return buf;
}

} // namespace displayswitch
