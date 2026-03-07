/**
 * DisplaySwitch Native — Windows Display Detector
 *
 * Port of python_version/core/control.py WindowsDisplayController to C++.
 * Uses:
 *   - EnumDisplayMonitors / GetMonitorInfo
 *   - EnumDisplayDevicesW (adapter & monitor info)
 *   - Registry EDID read (same path as Python)
 *   - CCD API (DisplayConfigGetDeviceInfo) for connection type & refresh
 *   - Physical Monitor API (dxva2) for DDC/CI
 */
#ifdef _WIN32

#include "displayswitch/display_detector.h"
#include "displayswitch/edid_parser.h"
#include "displayswitch/wmi_brightness.h"

#include <Windows.h>
#include <SetupAPI.h>
#include <LowLevelMonitorConfigurationAPI.h>
#include <HighLevelMonitorConfigurationAPI.h>
#include <PhysicalMonitorEnumerationAPI.h>
#include <wingdi.h>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <regex>
#include <iostream>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace displayswitch {

// ─── Helpers ────────────────────────────────────────────────────────────────

static std::string wstr_to_utf8(const wchar_t* ws) {
    if (!ws || !ws[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, s.data(), len, nullptr, nullptr);
    return s;
}

static std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/**
 * Parse the MCCS Capabilities string to extract supported values for a given VCP code.
 *
 * The capabilities string looks like:
 *   (prot(monitor) type(lcd) model(EW3270U) cmds(...) vcp(10 12 ... 60(11 12 0F 1B) ...) ...)
 *
 * For VCP code 0x60 (input select), the values in parentheses after "60" are the
 * actually supported input source codes — only these should be shown in the UI.
 */
static std::vector<int> parse_capabilities_vcp_values(const std::string& caps, uint8_t vcp_code) {
    std::vector<int> result;

    // Build search token: e.g. "60(" for VCP 0x60
    char hex_buf[8];
    std::snprintf(hex_buf, sizeof(hex_buf), "%02X(", vcp_code);
    std::string token_upper(hex_buf);
    std::snprintf(hex_buf, sizeof(hex_buf), "%02x(", vcp_code);
    std::string token_lower(hex_buf);

    // Find "60(" in the capabilities string (case-insensitive)
    auto pos = caps.find(token_upper);
    if (pos == std::string::npos) pos = caps.find(token_lower);
    if (pos == std::string::npos) return result;

    pos += token_upper.size(); // skip past "60("

    // Find matching closing paren
    int depth = 1;
    size_t end = pos;
    while (end < caps.size() && depth > 0) {
        if (caps[end] == '(') ++depth;
        else if (caps[end] == ')') --depth;
        ++end;
    }
    if (depth != 0) return result;

    // Extract the content between parens: "11 12 0F 1B"
    std::string content = caps.substr(pos, end - pos - 1);

    // Parse space-separated hex values
    std::istringstream iss(content);
    std::string tok;
    while (iss >> tok) {
        try {
            int val = std::stoi(tok, nullptr, 16);
            result.push_back(val);
        } catch (...) {
            // skip non-hex tokens
        }
    }

    return result;
}

/**
 * Query the DDC/CI Capabilities string from a physical monitor handle.
 * Returns the raw capabilities string, or empty string on failure.
 */
static std::string get_monitor_capabilities(HANDLE hPhysicalMonitor) {
    DWORD len = 0;
    if (!GetCapabilitiesStringLength(hPhysicalMonitor, &len) || len == 0)
        return {};

    std::string caps(len, '\0');
    if (!CapabilitiesRequestAndCapabilitiesReply(hPhysicalMonitor, caps.data(), len))
        return {};

    // Remove trailing null if any
    while (!caps.empty() && caps.back() == '\0') caps.pop_back();
    return caps;
}

// ─── Registry EDID reader ───────────────────────────────────────────────────

static std::map<std::string, std::vector<uint8_t>> read_all_edid_from_registry() {
    std::map<std::string, std::vector<uint8_t>> result;

    HKEY display_key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY",
            0, KEY_READ, &display_key) != ERROR_SUCCESS)
        return result;

    WCHAR monitor_type[256];
    for (DWORD mi = 0;
         RegEnumKeyW(display_key, mi, monitor_type, 256) == ERROR_SUCCESS;
         ++mi) {
        std::wstring mt_path = std::wstring(L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\") + monitor_type;
        HKEY mt_key;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, mt_path.c_str(), 0, KEY_READ, &mt_key) != ERROR_SUCCESS)
            continue;

        WCHAR instance_id[256];
        for (DWORD ii = 0;
             RegEnumKeyW(mt_key, ii, instance_id, 256) == ERROR_SUCCESS;
             ++ii) {
            std::wstring edid_path = mt_path + L"\\" + instance_id + L"\\Device Parameters";
            HKEY params_key;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, edid_path.c_str(), 0, KEY_READ, &params_key) != ERROR_SUCCESS)
                continue;

            DWORD type = 0, size = 0;
            if (RegQueryValueExW(params_key, L"EDID", nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size >= 128) {
                std::vector<uint8_t> edid(size);
                if (RegQueryValueExW(params_key, L"EDID", nullptr, &type, edid.data(), &size) == ERROR_SUCCESS) {
                    std::string dev_id = wstr_to_utf8(monitor_type) + "_" + wstr_to_utf8(instance_id);
                    result[dev_id] = std::move(edid);
                }
            }
            RegCloseKey(params_key);
        }
        RegCloseKey(mt_key);
    }
    RegCloseKey(display_key);
    return result;
}

// ─── CCD connection info ────────────────────────────────────────────────────

struct CCDInfo {
    std::string tech;          // "HDMI", "Internal LCD", …
    double      refresh_hz;
    std::string friendly_name;
};

static std::map<std::string, CCDInfo> get_ccd_map() {
    std::map<std::string, CCDInfo> result;

    UINT32 num_paths = 0, num_modes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &num_paths, &num_modes) != ERROR_SUCCESS)
        return result;

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(num_paths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(num_modes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &num_paths, paths.data(),
                           &num_modes, modes.data(), nullptr) != ERROR_SUCCESS)
        return result;

    for (UINT32 i = 0; i < num_paths; ++i) {
        auto& p = paths[i];
        CCDInfo info;

        // Source device name → "\\.\DISPLAY5"
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src_name{};
        src_name.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src_name.header.size      = sizeof(src_name);
        src_name.header.adapterId = p.sourceInfo.adapterId;
        src_name.header.id        = p.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src_name.header) != ERROR_SUCCESS) continue;
        std::string dev_name = wstr_to_utf8(src_name.viewGdiDeviceName);

        // Target friendly name → "BenQ EW3270U"
        DISPLAYCONFIG_TARGET_DEVICE_NAME tgt_name{};
        tgt_name.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tgt_name.header.size      = sizeof(tgt_name);
        tgt_name.header.adapterId = p.targetInfo.adapterId;
        tgt_name.header.id        = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tgt_name.header) == ERROR_SUCCESS)
            info.friendly_name = wstr_to_utf8(tgt_name.monitorFriendlyDeviceName);

        // Connection technology
        switch (tgt_name.outputTechnology) {
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI:          info.tech = "HDMI"; break;
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL: info.tech = "DisplayPort"; break;
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:      info.tech = "Internal LCD"; break;
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI:           info.tech = "DVI"; break;
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_D_JPN:
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDI:           info.tech = "Other"; break;
            default:                                           info.tech = "Unknown"; break;
        }

        // Refresh rate
        auto& r = p.targetInfo.refreshRate;
        info.refresh_hz = (r.Denominator) ? static_cast<double>(r.Numerator) / r.Denominator : 0;

        result[dev_name] = info;
    }

    return result;
}

// ─── Resolution via DEVMODE ─────────────────────────────────────────────────

static void fill_resolution(DisplayInfo& d) {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    std::wstring wdev(d.device_path.begin(), d.device_path.end());
    if (EnumDisplaySettingsW(wdev.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
        d.resolution_width  = static_cast<uint16_t>(dm.dmPelsWidth);
        d.resolution_height = static_cast<uint16_t>(dm.dmPelsHeight);
        d.bits_per_pixel    = static_cast<uint8_t>(dm.dmBitsPerPel);
        d.resolution_str    = std::to_string(d.resolution_width) + "x" + std::to_string(d.resolution_height);
    }
}

// ─── Windows Display Detector ───────────────────────────────────────────────

class WindowsDisplayDetector : public DisplayDetector {
public:
    WindowsDisplayDetector() {
        gpu_detector_ = create_gpu_detector();
        edid_map_     = read_all_edid_from_registry();
        ccd_map_      = get_ccd_map();
    }

    std::vector<DisplayInfo> scan() override {
        // Properly release old physical monitor handles before re-scanning
        close();
        displays_.clear();

        // Re-read CCD info (may have changed if monitors were plugged/unplugged)
        ccd_map_ = get_ccd_map();

        // Enumerate monitors
        EnumDisplayMonitors(nullptr, nullptr, monitor_enum_proc, reinterpret_cast<LPARAM>(this));

        return displays_;
    }

    bool set_brightness(DisplayInfo& d, int level) override {
        if (d.is_internal) {
            // Internal display: use WMI
            return wmi_set_brightness(level);
        }
        if (!d.has_physical_handle) return false;
        // Use Low-Level VCP 0x10 (Luminance) — MUST NOT mix with High-Level API
        return SetVCPFeature(static_cast<HANDLE>(d.physical_handle),
                             0x10, static_cast<DWORD>(level)) != 0;
    }

    int get_brightness(DisplayInfo& d) override {
        if (d.is_internal) {
            // Internal display: use WMI
            return wmi_get_brightness();
        }
        if (!d.has_physical_handle) return -1;
        DWORD cur = 0, max_v = 0;
        MC_VCP_CODE_TYPE ct;
        if (GetVCPFeatureAndVCPFeatureReply(
                static_cast<HANDLE>(d.physical_handle), 0x10, &ct, &cur, &max_v))
            return static_cast<int>(cur);
        return -1;
    }

    bool set_input(DisplayInfo& d, int code) override {
        if (!d.has_physical_handle) return false;
        return SetVCPFeature(static_cast<HANDLE>(d.physical_handle),
                             0x60, static_cast<DWORD>(code)) != 0;
    }

    int get_input(DisplayInfo& d) override {
        if (!d.has_physical_handle) return -1;
        DWORD cur = 0, max_v = 0;
        MC_VCP_CODE_TYPE ct;
        if (GetVCPFeatureAndVCPFeatureReply(
                static_cast<HANDLE>(d.physical_handle), 0x60, &ct, &cur, &max_v))
            return static_cast<int>(cur);
        return -1;
    }

    void close() override {
        for (auto& d : displays_) {
            if (d.has_physical_handle) {
                DestroyPhysicalMonitor(static_cast<HANDLE>(d.physical_handle));
                d.physical_handle = nullptr;
                d.has_physical_handle = false;
            }
        }
    }

    ~WindowsDisplayDetector() override { close(); }

private:
    std::unique_ptr<GPUDetector> gpu_detector_;
    std::map<std::string, std::vector<uint8_t>> edid_map_;
    std::map<std::string, CCDInfo> ccd_map_;
    std::vector<DisplayInfo> displays_;

    static BOOL CALLBACK monitor_enum_proc(HMONITOR hMon, HDC, LPRECT, LPARAM ctx) {
        auto* self = reinterpret_cast<WindowsDisplayDetector*>(ctx);
        self->process_monitor(hMon);
        return TRUE;
    }

    void process_monitor(HMONITOR hMon) {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(hMon, &mi)) return;

        std::string device_name = wstr_to_utf8(mi.szDevice);

        // ── Adapter string (GPU name from EnumDisplayDevices) ───────────
        std::string adapter_string;
        DISPLAY_DEVICEW dd{};
        dd.cb = sizeof(dd);
        for (DWORD idx = 0; EnumDisplayDevicesW(nullptr, idx, &dd, 0); ++idx) {
            if (wstr_to_utf8(dd.DeviceName) == device_name) {
                adapter_string = wstr_to_utf8(dd.DeviceString);
                break;
            }
            dd.cb = sizeof(dd);
        }

        // ── GPU matching ────────────────────────────────────────────────
        GPUInfo gpu;
        if (gpu_detector_ && !adapter_string.empty()) {
            gpu = gpu_detector_->get_gpu_for_adapter(adapter_string);
        }
        if (gpu.name.empty()) gpu.name = adapter_string.empty() ? "Unknown GPU" : adapter_string;

        // ── Monitor PnP ID ──────────────────────────────────────────────
        std::string pnp_id;
        std::string real_name = "Generic PnP Monitor";
        {
            DISPLAY_DEVICEW dm{};
            dm.cb = sizeof(dm);
            std::wstring wdev(device_name.begin(), device_name.end());
            if (EnumDisplayDevicesW(wdev.c_str(), 0, &dm, 0)) {
                std::string dev_id = wstr_to_utf8(dm.DeviceID);
                std::regex re(R"(MONITOR\\([^\\]+))");
                std::smatch match;
                if (std::regex_search(dev_id, match, re))
                    pnp_id = match[1].str();
            }
        }

        // ── CCD info ────────────────────────────────────────────────────
        std::string conn_type = "Unknown";
        double refresh = 0;
        bool is_internal = false;
        if (auto it = ccd_map_.find(device_name); it != ccd_map_.end()) {
            conn_type = it->second.tech;
            refresh   = it->second.refresh_hz;
            if (!it->second.friendly_name.empty())
                real_name = it->second.friendly_name;
            if (conn_type.find("Internal") != std::string::npos)
                is_internal = true;
        }

        // ── Physical monitor handle (DDC/CI) ────────────────────────────
        DWORD num_phys = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMon, &num_phys) || num_phys == 0)
            return;

        std::vector<PHYSICAL_MONITOR> phys(num_phys);
        if (!GetPhysicalMonitorsFromHMONITOR(hMon, num_phys, phys.data()))
            return;

        for (DWORD i = 0; i < num_phys; ++i) {
            DisplayInfo d;
            d.device_path    = device_name;
            d.gpu            = gpu;
            d.connection_type = conn_type;
            d.refresh_rate   = refresh;
            d.is_internal    = is_internal;
            d.manufacturer_id = pnp_id;
            d.physical_handle = phys[i].hPhysicalMonitor;
            d.has_physical_handle = true;  // Handle value 0 is valid!

            // Try to set name from CCD friendly name
            std::string desc = wstr_to_utf8(phys[i].szPhysicalMonitorDescription);
            d.name = (real_name != "Generic PnP Monitor") ? real_name : desc;

            // ── Enrich with EDID ────────────────────────────────────────
            enrich_with_edid(d, pnp_id);

            // Fix generic name after EDID
            if (d.name == "Generic PnP Monitor") {
                if (!d.manufacturer_id.empty() && !d.product_code.empty())
                    d.name = d.manufacturer_id + " " + d.product_code;
                else if (d.is_internal)
                    d.name = "Internal Display";
            }

            // ── Resolution & bandwidth ──────────────────────────────────
            fill_resolution(d);
            uint8_t frl = 0;
            if (!d.edid.connectors.empty()) {
                auto& c = d.edid.connectors.front();
                if (c.hdmi_caps) frl = c.hdmi_caps->max_frl_rate;
            }
            d.bandwidth = calculate_bandwidth(d.connection_type, d.hdmi_version,
                                              d.max_tmds_clock_mhz, frl);

            // ── DDC/CI queries ────────────────────────────────────────────
            // Order matters: brightness (fast VCP read) BEFORE the slow
            // CapabilitiesRequestAndCapabilitiesReply call, to avoid DDC/CI
            // bus contention that causes brightness reads to fail.
            if (!d.is_internal && d.has_physical_handle) {
                // 1. Read brightness first (fast VCP 0x10)
                d.cached_brightness = get_brightness(d);

                // 2. Read current input (fast VCP 0x60)
                int inp = get_input(d);
                if (inp >= 0) d.current_input = inp;

                // Small delay to let DDC/CI bus settle
                Sleep(40);

                // 3. Parse the MCCS Capabilities string (SLOW — 1-5 sec)
                std::string caps = get_monitor_capabilities(
                    static_cast<HANDLE>(d.physical_handle));
                if (!caps.empty()) {
                    d.supported_inputs = parse_capabilities_vcp_values(caps, 0x60);
                }

                // Always include current input in the list
                if (d.current_input > 0) {
                    auto it = std::find(d.supported_inputs.begin(),
                                        d.supported_inputs.end(), d.current_input);
                    if (it == d.supported_inputs.end())
                        d.supported_inputs.push_back(d.current_input);
                    std::sort(d.supported_inputs.begin(), d.supported_inputs.end());
                }
            } else if (d.is_internal) {
                // Internal display: use WMI for brightness (DDC/CI not available)
                d.cached_brightness = wmi_get_brightness();
            }

            displays_.push_back(std::move(d));
        }
    }

    void enrich_with_edid(DisplayInfo& d, const std::string& pnp_id) {
        // Find matching EDID in registry map
        for (auto& [key, data] : edid_map_) {
            if (key.find(pnp_id) != std::string::npos) {
                try {
                    d.edid = EDIDParser::parse(data.data(), data.size());

                    d.manufacturer_id = d.edid.manufacturer;
                    d.product_code    = d.edid.product_code;
                    d.screen_width_mm  = d.edid.screen_width_mm;
                    d.screen_height_mm = d.edid.screen_height_mm;

                    // HDR
                    if (d.edid.hdr_metadata) {
                        d.supports_hdr = true;
                        auto& hdr = *d.edid.hdr_metadata;
                        if (hdr.sdr_support)          d.hdr_formats.push_back("SDR");
                        if (hdr.hdr10_support)        d.hdr_formats.push_back("HDR10");
                        if (hdr.hlg_support)          d.hdr_formats.push_back("HLG");
                        if (hdr.dolby_vision_support) d.hdr_formats.push_back("Dolby Vision");
                    }

                    // HDMI version from connectors
                    for (auto& c : d.edid.connectors) {
                        if (c.type == Connector::Type::HDMI && c.hdmi_caps) {
                            auto& caps = *c.hdmi_caps;
                            d.max_tmds_clock_mhz = caps.max_tmds_clock_mhz;
                            switch (caps.version) {
                                case Connector::HDMICapabilities::Version::HDMI_2_1:
                                    d.hdmi_version = "HDMI 2.1";
                                    if (caps.max_frl_rate > 0) {
                                        uint8_t gbps = caps.max_frl_rate * caps.frl_lanes;
                                        d.hdmi_frl_rate = "FRL " + std::to_string(caps.max_frl_rate)
                                            + " (" + std::to_string(gbps) + "Gbps)";
                                    }
                                    break;
                                case Connector::HDMICapabilities::Version::HDMI_2_0:
                                    d.hdmi_version = "HDMI 2.0"; break;
                                default:
                                    d.hdmi_version = "HDMI 1.4"; break;
                            }
                            // Connection type update
                            if (d.connection_type == "HDMI")
                                d.connection_type = d.hdmi_version;
                            break;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "EDID parse error for " << pnp_id << ": " << e.what() << "\n";
                }
                break;
            }
        }
    }
};

// Factory
std::unique_ptr<DisplayDetector> create_detector() {
    return std::make_unique<WindowsDisplayDetector>();
}

} // namespace displayswitch

#endif // _WIN32
