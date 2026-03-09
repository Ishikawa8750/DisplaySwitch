/**
 * DisplaySwitch Native — Linux Display Detector
 *
 * Uses:
 *   - DRM/KMS via /sys/class/drm for display enumeration
 *   - EDID from /sys/class/drm/card*-*/edid
 *   - i2c-dev (/dev/i2c-*) for DDC/CI brightness & input control
 *   - libdrm for connector info
 *
 * Requires: i2c-dev kernel module loaded, user in 'i2c' group or root.
 */
#ifdef __linux__

#include "displayswitch/display_detector.h"
#include "displayswitch/edid_parser.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <thread>

// Linux I2C
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

// DRM
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace fs = std::filesystem;

namespace displayswitch {

// ─── DDC/CI constants ──────────────────────────────────────────────────

static constexpr uint8_t DDC_CI_ADDR         = 0x37;
static constexpr uint8_t DDC_CI_HOST_ADDR    = 0x51;
static constexpr uint8_t DDC_CI_DISPLAY_ADDR = 0x6E;
static constexpr uint8_t VCP_BRIGHTNESS      = 0x10;
static constexpr uint8_t VCP_INPUT_SOURCE    = 0x60;

// ─── I2C DDC/CI helpers ────────────────────────────────────────────────

/**
 * Calculate DDC/CI checksum: XOR of all bytes including the implicit
 * source address.
 */
static uint8_t ddc_checksum(uint8_t source_addr, const uint8_t* data, size_t len) {
    uint8_t chk = source_addr;
    for (size_t i = 0; i < len; i++) chk ^= data[i];
    return chk;
}

/**
 * Open the I2C bus for a given bus number.
 * Returns fd or -1 on failure.
 */
static int open_i2c_bus(int bus_num) {
    char path[64];
    std::snprintf(path, sizeof(path), "/dev/i2c-%d", bus_num);
    int fd = open(path, O_RDWR);
    if (fd < 0) return -1;
    if (ioctl(fd, I2C_SLAVE, DDC_CI_ADDR) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/**
 * Send a DDC/CI VCP Get request and read the response.
 * Returns the current value on success, -1 on failure.
 */
static int ddc_get_vcp(int i2c_fd, uint8_t vcp_code) {
    // Build Get VCP Feature request:
    // [dest=0x6E] [length|0x80 = 0x82] [0x01] [vcp_code] [checksum]
    uint8_t request[4];
    request[0] = DDC_CI_DISPLAY_ADDR; // destination
    request[1] = 0x82;                // length=2 | 0x80
    request[2] = 0x01;                // Get VCP Feature
    request[3] = vcp_code;
    uint8_t chk = ddc_checksum(DDC_CI_HOST_ADDR, request, 4);

    // Write: skip the destination byte (it's the I2C address already set)
    uint8_t write_buf[4];
    write_buf[0] = request[1];
    write_buf[1] = request[2];
    write_buf[2] = request[3];
    write_buf[3] = chk;
    if (write(i2c_fd, write_buf, 4) != 4) return -1;

    // Wait for monitor to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Read response: [src=0x6E] [length|0x80] [0x02] [result] [vcp_code] [type] [max_hi] [max_lo] [cur_hi] [cur_lo] [chk]
    uint8_t resp[12];
    if (read(i2c_fd, resp, 11) < 8) return -1;

    // resp[0] = length|0x80, resp[1] = 0x02 (VCP Feature Reply), resp[2] = result code
    if (resp[1] != 0x02 || resp[2] != 0x00) return -1;

    int current_value = (resp[7] << 8) | resp[8];
    return current_value;
}

/**
 * Send a DDC/CI VCP Set request.
 * Returns 0 on success, -1 on failure.
 */
static int ddc_set_vcp(int i2c_fd, uint8_t vcp_code, uint16_t value) {
    // Build Set VCP Feature request:
    // [dest=0x6E] [0x84] [0x03] [vcp_code] [value_hi] [value_lo] [checksum]
    uint8_t request[6];
    request[0] = DDC_CI_DISPLAY_ADDR;
    request[1] = 0x84;  // length=4 | 0x80
    request[2] = 0x03;  // Set VCP Feature
    request[3] = vcp_code;
    request[4] = (value >> 8) & 0xFF;
    request[5] = value & 0xFF;
    uint8_t chk = ddc_checksum(DDC_CI_HOST_ADDR, request, 6);

    uint8_t write_buf[6];
    write_buf[0] = request[1];
    write_buf[1] = request[2];
    write_buf[2] = request[3];
    write_buf[3] = request[4];
    write_buf[4] = request[5];
    write_buf[5] = chk;
    if (write(i2c_fd, write_buf, 6) != 6) return -1;

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return 0;
}

// ─── DRM connector type to string ──────────────────────────────────────

static std::string connector_type_str(uint32_t type) {
    switch (type) {
        case DRM_MODE_CONNECTOR_VGA:       return "VGA";
        case DRM_MODE_CONNECTOR_DVII:
        case DRM_MODE_CONNECTOR_DVID:
        case DRM_MODE_CONNECTOR_DVIA:      return "DVI";
        case DRM_MODE_CONNECTOR_HDMIA:
        case DRM_MODE_CONNECTOR_HDMIB:     return "HDMI";
        case DRM_MODE_CONNECTOR_DisplayPort: return "DisplayPort";
        case DRM_MODE_CONNECTOR_eDP:       return "Internal LCD";
        case DRM_MODE_CONNECTOR_LVDS:      return "LVDS";
#ifdef DRM_MODE_CONNECTOR_USB
        case DRM_MODE_CONNECTOR_USB:       return "USB-C";
#endif
        default:                           return "Unknown";
    }
}

static bool is_internal_connector(uint32_t type) {
    return type == DRM_MODE_CONNECTOR_eDP ||
           type == DRM_MODE_CONNECTOR_LVDS ||
#ifdef DRM_MODE_CONNECTOR_DSI
           type == DRM_MODE_CONNECTOR_DSI ||
#endif
           false;
}

// ─── Find I2C bus number for a DRM connector ──────────────────────────

/**
 * Map a DRM card + connector to its I2C bus number.
 * Looks in /sys/class/drm/<connector>/i2c-*/
 */
static int find_i2c_bus_for_connector(const std::string& drm_connector_path) {
    // e.g. /sys/class/drm/card0-HDMI-A-1 → look for i2c-* subdir
    try {
        for (const auto& entry : fs::directory_iterator(drm_connector_path)) {
            std::string name = entry.path().filename().string();
            if (name.substr(0, 4) == "i2c-") {
                int bus = -1;
                try { bus = std::stoi(name.substr(4)); } catch (...) {}
                if (bus >= 0) return bus;
            }
        }
    } catch (...) {}

    // Fallback: try ddc subdirectory
    try {
        std::string ddc_path = drm_connector_path + "/ddc";
        if (fs::exists(ddc_path)) {
            for (const auto& entry : fs::directory_iterator(ddc_path)) {
                std::string name = entry.path().filename().string();
                if (name.substr(0, 4) == "i2c-") {
                    int bus = -1;
                    try { bus = std::stoi(name.substr(4)); } catch (...) {}
                    if (bus >= 0) return bus;
                }
            }
        }
    } catch (...) {}

    return -1;
}

// ─── Read EDID from sysfs ─────────────────────────────────────────────

static std::vector<uint8_t> read_edid_from_sysfs(const std::string& connector_path) {
    std::string edid_path = connector_path + "/edid";
    std::ifstream f(edid_path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(f),
        std::istreambuf_iterator<char>()
    );
}

// ─── Read backlight brightness for internal displays ──────────────────

static int read_backlight_brightness() {
    // Try common backlight interfaces
    const char* paths[] = {
        "/sys/class/backlight/intel_backlight",
        "/sys/class/backlight/amdgpu_bl0",
        "/sys/class/backlight/nvidia_0",
        "/sys/class/backlight/acpi_video0",
    };

    for (const char* base : paths) {
        std::string brightness_path = std::string(base) + "/brightness";
        std::string max_path = std::string(base) + "/max_brightness";

        std::ifstream fb(brightness_path);
        std::ifstream fm(max_path);
        if (fb && fm) {
            int cur = 0, max_val = 0;
            fb >> cur;
            fm >> max_val;
            if (max_val > 0) {
                return (cur * 100) / max_val;
            }
        }
    }

    // Fallback: scan all backlight entries
    try {
        for (const auto& entry : fs::directory_iterator("/sys/class/backlight")) {
            std::string brightness_path = entry.path().string() + "/brightness";
            std::string max_path = entry.path().string() + "/max_brightness";
            std::ifstream fb(brightness_path);
            std::ifstream fm(max_path);
            if (fb && fm) {
                int cur = 0, max_val = 0;
                fb >> cur;
                fm >> max_val;
                if (max_val > 0) return (cur * 100) / max_val;
            }
        }
    } catch (...) {}

    return -1;
}

static bool set_backlight_brightness(int level) {
    // Try common backlight interfaces
    try {
        for (const auto& entry : fs::directory_iterator("/sys/class/backlight")) {
            std::string max_path = entry.path().string() + "/max_brightness";
            std::string brightness_path = entry.path().string() + "/brightness";
            std::ifstream fm(max_path);
            if (fm) {
                int max_val = 0;
                fm >> max_val;
                if (max_val > 0) {
                    int raw = (level * max_val) / 100;
                    std::ofstream fb(brightness_path);
                    if (fb) {
                        fb << raw;
                        return true;
                    }
                }
            }
        }
    } catch (...) {}
    return false;
}

// ─── Linux Display Detector ────────────────────────────────────────────

class LinuxDisplayDetector : public DisplayDetector {
public:
    LinuxDisplayDetector() = default;
    ~LinuxDisplayDetector() override { close(); }

    std::vector<DisplayInfo> scan() override {
        std::vector<DisplayInfo> results;

        // Enumerate DRM devices
        for (int card = 0; card < 16; card++) {
            char drm_path[64];
            std::snprintf(drm_path, sizeof(drm_path), "/dev/dri/card%d", card);

            int drm_fd = open(drm_path, O_RDWR | O_CLOEXEC);
            if (drm_fd < 0) continue;

            drmModeRes* resources = drmModeGetResources(drm_fd);
            if (!resources) {
                ::close(drm_fd);
                continue;
            }

            for (int i = 0; i < resources->count_connectors; i++) {
                drmModeConnector* conn = drmModeGetConnector(drm_fd, resources->connectors[i]);
                if (!conn) continue;

                if (conn->connection != DRM_MODE_CONNECTED) {
                    drmModeFreeConnector(conn);
                    continue;
                }

                DisplayInfo info;
                info.connection_type = connector_type_str(conn->connector_type);
                info.is_internal = is_internal_connector(conn->connector_type);
                info.device_path = std::string(drm_path) + "/connector-" + std::to_string(conn->connector_id);

                // Connector name (e.g. "HDMI-A-1", "eDP-1")
                std::string conn_name = connector_type_str(conn->connector_type)
                    + "-" + std::to_string(conn->connector_type_id);

                // Resolution from current mode
                if (conn->encoder_id) {
                    drmModeEncoder* enc = drmModeGetEncoder(drm_fd, conn->encoder_id);
                    if (enc) {
                        drmModeCrtc* crtc = drmModeGetCrtc(drm_fd, enc->crtc_id);
                        if (crtc) {
                            if (crtc->mode_valid) {
                                info.resolution_width = crtc->mode.hdisplay;
                                info.resolution_height = crtc->mode.vdisplay;
                                info.resolution_str = std::to_string(info.resolution_width)
                                    + "x" + std::to_string(info.resolution_height);
                                info.refresh_rate = (crtc->mode.clock * 1000.0) /
                                    (crtc->mode.htotal * crtc->mode.vtotal);
                            }
                            drmModeFreeCrtc(crtc);
                        }
                        drmModeFreeEncoder(enc);
                    }
                }

                // Read EDID from sysfs
                char sysfs_path[256];
                std::snprintf(sysfs_path, sizeof(sysfs_path),
                    "/sys/class/drm/card%d-%s-%d",
                    card,
                    connector_type_str(conn->connector_type).c_str(),
                    conn->connector_type_id);

                // Try alternative connector naming (HDMI-A-1 etc.)
                std::string sysfs_str(sysfs_path);
                if (!fs::exists(sysfs_str)) {
                    // Try different naming patterns
                    std::string alt_name;
                    switch (conn->connector_type) {
                        case DRM_MODE_CONNECTOR_HDMIA: alt_name = "HDMI-A"; break;
                        case DRM_MODE_CONNECTOR_HDMIB: alt_name = "HDMI-B"; break;
                        case DRM_MODE_CONNECTOR_DisplayPort: alt_name = "DP"; break;
                        case DRM_MODE_CONNECTOR_eDP: alt_name = "eDP"; break;
                        case DRM_MODE_CONNECTOR_VGA: alt_name = "VGA"; break;
                        case DRM_MODE_CONNECTOR_DVID: alt_name = "DVI-D"; break;
                        case DRM_MODE_CONNECTOR_DVII: alt_name = "DVI-I"; break;
                        default: alt_name = connector_type_str(conn->connector_type); break;
                    }
                    std::snprintf(sysfs_path, sizeof(sysfs_path),
                        "/sys/class/drm/card%d-%s-%d",
                        card, alt_name.c_str(), conn->connector_type_id);
                    sysfs_str = sysfs_path;
                }

                auto edid_raw = read_edid_from_sysfs(sysfs_str);
                if (!edid_raw.empty()) {
                    info.edid = EDIDParser::parse(edid_raw.data(), edid_raw.size());
                    info.manufacturer_id = info.edid.manufacturer;
                    info.product_code = info.edid.product_code;
                    if (info.manufacturer_id[0] != '\0' && info.product_code[0] != '\0') {
                        std::string edid_name = std::string(info.manufacturer_id) + " " + std::string(info.product_code);
                        if (!edid_name.empty() && edid_name != " ")
                            info.name = edid_name;
                        else
                            info.name = conn_name;
                    } else {
                        info.name = conn_name;
                    }
                    info.screen_width_mm = info.edid.screen_width_mm;
                    info.screen_height_mm = info.edid.screen_height_mm;

                    // HDR from EDID metadata
                    if (info.edid.hdr_metadata) {
                        info.supports_hdr = true;
                        auto& hdr = *info.edid.hdr_metadata;
                        if (hdr.sdr_support)          info.hdr_formats.push_back("SDR");
                        if (hdr.hdr10_support)        info.hdr_formats.push_back("HDR10");
                        if (hdr.hlg_support)          info.hdr_formats.push_back("HLG");
                        if (hdr.dolby_vision_support) info.hdr_formats.push_back("Dolby Vision");
                    }

                    // HDMI version from connector caps
                    for (auto& c : info.edid.connectors) {
                        if (c.type == Connector::Type::HDMI && c.hdmi_caps) {
                            info.max_tmds_clock_mhz = c.hdmi_caps->max_tmds_clock_mhz;
                            break;
                        }
                    }

                    info.bits_per_pixel = info.edid.preferred_mode.bit_depth > 0
                        ? info.edid.preferred_mode.bit_depth * 3 : 0;
                } else {
                    info.name = conn_name;
                }

                // Calculate bandwidth
                info.bandwidth = calculate_bandwidth(
                    info.connection_type, info.hdmi_version,
                    info.max_tmds_clock_mhz, 0);

                // Find I2C bus for DDC/CI
                int i2c_bus = find_i2c_bus_for_connector(sysfs_str);
                if (i2c_bus >= 0 && !info.is_internal) {
                    // Store bus number as physical_handle (reinterpret cast)
                    info.physical_handle = reinterpret_cast<void*>(static_cast<intptr_t>(i2c_bus));
                    info.has_physical_handle = true;

                    // Read brightness via DDC/CI
                    int fd = open_i2c_bus(i2c_bus);
                    if (fd >= 0) {
                        info.cached_brightness = ddc_get_vcp(fd, VCP_BRIGHTNESS);
                        info.current_input = ddc_get_vcp(fd, VCP_INPUT_SOURCE);
                        ::close(fd);
                    }
                } else if (info.is_internal) {
                    info.cached_brightness = read_backlight_brightness();
                }

                // Common DDC/CI input codes
                if (!info.is_internal && info.has_physical_handle) {
                    info.supported_inputs = {15, 16, 17, 18, 27}; // DP-1, DP-2, HDMI-1, HDMI-2, USB-C
                }

                results.push_back(std::move(info));
                drmModeFreeConnector(conn);
            }

            drmModeFreeResources(resources);
            ::close(drm_fd);
        }

        return results;
    }

    bool set_brightness(DisplayInfo& display, int level) override {
        if (display.is_internal) {
            return set_backlight_brightness(level);
        }
        if (!display.has_physical_handle) return false;
        int bus = static_cast<int>(reinterpret_cast<intptr_t>(display.physical_handle));
        int fd = open_i2c_bus(bus);
        if (fd < 0) return false;
        int rc = ddc_set_vcp(fd, VCP_BRIGHTNESS, static_cast<uint16_t>(level));
        ::close(fd);
        return rc == 0;
    }

    int get_brightness(DisplayInfo& display) override {
        if (display.is_internal) {
            return read_backlight_brightness();
        }
        if (!display.has_physical_handle) return -1;
        int bus = static_cast<int>(reinterpret_cast<intptr_t>(display.physical_handle));
        int fd = open_i2c_bus(bus);
        if (fd < 0) return -1;
        int val = ddc_get_vcp(fd, VCP_BRIGHTNESS);
        ::close(fd);
        return val;
    }

    bool set_input(DisplayInfo& display, int input_code) override {
        if (!display.has_physical_handle || display.is_internal) return false;
        int bus = static_cast<int>(reinterpret_cast<intptr_t>(display.physical_handle));
        int fd = open_i2c_bus(bus);
        if (fd < 0) return false;
        int rc = ddc_set_vcp(fd, VCP_INPUT_SOURCE, static_cast<uint16_t>(input_code));
        ::close(fd);
        return rc == 0;
    }

    int get_input(DisplayInfo& display) override {
        if (!display.has_physical_handle || display.is_internal) return -1;
        int bus = static_cast<int>(reinterpret_cast<intptr_t>(display.physical_handle));
        int fd = open_i2c_bus(bus);
        if (fd < 0) return -1;
        int val = ddc_get_vcp(fd, VCP_INPUT_SOURCE);
        ::close(fd);
        return val;
    }

    void close() override {
        // No persistent resources to clean up
    }
};

std::unique_ptr<DisplayDetector> create_detector() {
    return std::make_unique<LinuxDisplayDetector>();
}

} // namespace displayswitch

#endif // __linux__
