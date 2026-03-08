/**
 * DisplaySwitch Native — C API Implementation
 *
 * Wraps the C++ DisplayDetector into a flat C interface for FFI.
 */
#include "displayswitch/c_api.h"
#include "displayswitch/display_detector.h"

#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#define Sleep(ms) usleep((ms) * 1000)
#endif

using namespace displayswitch;

/* ─── Internal state behind opaque handle ──────────────────────────────── */

struct DsDetector {
    std::unique_ptr<DisplayDetector> detector;
    std::vector<DisplayInfo>         cached_displays;
    std::string                      last_error;
    std::mutex                       mtx;
};

/* Thread-local last error for the global ds_last_error() */
static thread_local std::string g_last_error;

static void set_error(DsDetector* det, const std::string& msg) {
    if (det) det->last_error = msg;
    g_last_error = msg;
}

/* ─── String helpers ───────────────────────────────────────────────────── */

/** Duplicate a std::string to a heap-allocated C string. Caller frees. */
static char* dup_str(const std::string& s) {
    if (s.empty()) return nullptr;
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p) {
        std::memcpy(p, s.c_str(), s.size() + 1);
    }
    return p;
}

/** Join a vector of strings with a separator. */
static std::string join_strings(const std::vector<std::string>& v, const char* sep) {
    std::string result;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) result += sep;
        result += v[i];
    }
    return result;
}

/* ─── Conversion: C++ DisplayInfo → C DsDisplayInfoC ───────────────────── */

static DsDisplayInfoC convert_display(const DisplayInfo& d) {
    DsDisplayInfoC c{};

    c.name              = dup_str(d.name);
    c.device_path       = dup_str(d.device_path);
    c.manufacturer_id   = dup_str(d.manufacturer_id);
    c.product_code      = dup_str(d.product_code);
    c.is_internal       = d.is_internal ? 1 : 0;

    // GPU
    c.gpu.name                  = dup_str(d.gpu.name);
    c.gpu.vendor_name           = dup_str(d.gpu.vendor_name);
    c.gpu.dedicated_vram_bytes  = d.gpu.dedicated_vram_bytes;
    c.gpu.driver_version        = dup_str(d.gpu.driver_version);
    c.gpu.formatted_name        = dup_str(d.gpu.formatted_name());

    // Connection
    c.connection_type    = dup_str(d.connection_type);
    c.refresh_rate       = d.refresh_rate;
    c.hdmi_version       = dup_str(d.hdmi_version);
    c.hdmi_frl_rate      = dup_str(d.hdmi_frl_rate);
    c.max_tmds_clock_mhz = d.max_tmds_clock_mhz;

    // HDR
    c.supports_hdr = d.supports_hdr ? 1 : 0;
    c.hdr_formats  = dup_str(join_strings(d.hdr_formats, ", "));

    // Physical size
    c.screen_width_mm   = d.screen_width_mm;
    c.screen_height_mm  = d.screen_height_mm;

    // Resolution
    c.resolution_width   = d.resolution_width;
    c.resolution_height  = d.resolution_height;
    c.resolution_str     = dup_str(d.resolution_str);
    c.bits_per_pixel     = d.bits_per_pixel;

    // Bandwidth
    c.bandwidth.max_bandwidth_gbps = d.bandwidth.max_bandwidth_gbps;
    c.bandwidth.bandwidth_str      = dup_str(d.bandwidth.bandwidth_str);
    c.bandwidth.can_support_4k60   = d.bandwidth.can_support_4k60  ? 1 : 0;
    c.bandwidth.can_support_4k120  = d.bandwidth.can_support_4k120 ? 1 : 0;
    c.bandwidth.can_support_8k60   = d.bandwidth.can_support_8k60  ? 1 : 0;

    // Input
    c.current_input = d.current_input;
    if (!d.supported_inputs.empty()) {
        int* arr = static_cast<int*>(std::malloc(sizeof(int) * d.supported_inputs.size()));
        if (arr) {
            for (size_t i = 0; i < d.supported_inputs.size(); ++i)
                arr[i] = d.supported_inputs[i];
        }
        c.supported_inputs       = arr;
        c.supported_inputs_count = static_cast<int>(d.supported_inputs.size());
    } else {
        c.supported_inputs       = nullptr;
        c.supported_inputs_count = 0;
    }

    c.brightness = -1;  // Will be filled after scan

    return c;
}

/** Free all heap-allocated strings inside a DsDisplayInfoC. */
static void free_display_internals(DsDisplayInfoC& c) {
    std::free(const_cast<char*>(c.name));
    std::free(const_cast<char*>(c.device_path));
    std::free(const_cast<char*>(c.manufacturer_id));
    std::free(const_cast<char*>(c.product_code));
    std::free(const_cast<char*>(c.gpu.name));
    std::free(const_cast<char*>(c.gpu.vendor_name));
    std::free(const_cast<char*>(c.gpu.driver_version));
    std::free(const_cast<char*>(c.gpu.formatted_name));
    std::free(const_cast<char*>(c.connection_type));
    std::free(const_cast<char*>(c.hdmi_version));
    std::free(const_cast<char*>(c.hdmi_frl_rate));
    std::free(const_cast<char*>(c.hdr_formats));
    std::free(const_cast<char*>(c.resolution_str));
    std::free(const_cast<char*>(c.bandwidth.bandwidth_str));
    std::free(const_cast<int*>(c.supported_inputs));
}

/* ─── C API implementation ─────────────────────────────────────────────── */

extern "C" {

DS_API DsDetector* ds_create_detector(void) {
    try {
        auto* det = new DsDetector();
        det->detector = create_detector();
        if (!det->detector) {
            delete det;
            g_last_error = "Failed to create platform detector";
            return nullptr;
        }
        return det;
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return nullptr;
    }
}

DS_API void ds_destroy_detector(DsDetector* det) {
    if (!det) return;
    try {
        det->detector->close();
    } catch (...) {}
    delete det;
}

DS_API DsDisplayInfoC* ds_scan(DsDetector* det, int* out_count) {
    if (!det || !out_count) {
        if (out_count) *out_count = 0;
        set_error(det, "Invalid arguments");
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(det->mtx);

    try {
        det->cached_displays = det->detector->scan();
    } catch (const std::exception& e) {
        set_error(det, std::string("scan failed: ") + e.what());
        *out_count = 0;
        return nullptr;
    }

    int n = static_cast<int>(det->cached_displays.size());
    *out_count = n;
    if (n == 0) return nullptr;

    auto* arr = static_cast<DsDisplayInfoC*>(std::calloc(n, sizeof(DsDisplayInfoC)));
    if (!arr) {
        set_error(det, "Out of memory");
        *out_count = 0;
        return nullptr;
    }

    for (int i = 0; i < n; ++i) {
        arr[i] = convert_display(det->cached_displays[i]);
        // Use brightness cached during scan (read BEFORE slow capabilities query)
        // to avoid DDC/CI bus contention causing read failures.
        int bri = det->cached_displays[i].cached_brightness;
        if (bri < 0 && !det->cached_displays[i].is_internal) {
            // Fallback: try reading again (in case scan didn't cache it)
            Sleep(50);
            bri = det->detector->get_brightness(det->cached_displays[i]);
        }
        arr[i].brightness = bri;
    }

    det->last_error.clear();
    return arr;
}

DS_API void ds_free_displays(DsDisplayInfoC* displays, int count) {
    if (!displays) return;
    for (int i = 0; i < count; ++i) {
        free_display_internals(displays[i]);
    }
    std::free(displays);
}

DS_API int ds_set_brightness(DsDetector* det, int display_index, int level) {
    if (!det) return -1;
    std::lock_guard<std::mutex> lock(det->mtx);
    if (display_index < 0 || display_index >= static_cast<int>(det->cached_displays.size())) {
        set_error(det, "Display index out of range");
        return -1;
    }
    bool ok = det->detector->set_brightness(det->cached_displays[display_index], level);
    if (!ok) set_error(det, "set_brightness failed (DDC/CI or WMI)");
    return ok ? 0 : -1;
}

DS_API int ds_get_brightness(DsDetector* det, int display_index) {
    if (!det) return -1;
    std::lock_guard<std::mutex> lock(det->mtx);
    if (display_index < 0 || display_index >= static_cast<int>(det->cached_displays.size()))
        return -1;
    return det->detector->get_brightness(det->cached_displays[display_index]);
}

DS_API int ds_set_input(DsDetector* det, int display_index, int input_code) {
    if (!det) return -1;
    std::lock_guard<std::mutex> lock(det->mtx);
    if (display_index < 0 || display_index >= static_cast<int>(det->cached_displays.size())) {
        set_error(det, "Display index out of range");
        return -1;
    }
    bool ok = det->detector->set_input(det->cached_displays[display_index], input_code);
    if (!ok) set_error(det, "set_input failed (DDC/CI)");
    return ok ? 0 : -1;
}

DS_API int ds_get_input(DsDetector* det, int display_index) {
    if (!det) return -1;
    std::lock_guard<std::mutex> lock(det->mtx);
    if (display_index < 0 || display_index >= static_cast<int>(det->cached_displays.size()))
        return -1;
    return det->detector->get_input(det->cached_displays[display_index]);
}

DS_API int ds_get_hdr_enabled(DsDetector* det, int display_index) {
    if (!det) return -1;
    std::lock_guard<std::mutex> lock(det->mtx);
    if (display_index < 0 || display_index >= static_cast<int>(det->cached_displays.size()))
        return -1;
    return det->detector->get_hdr_enabled(det->cached_displays[display_index]) ? 1 : 0;
}

DS_API int ds_set_hdr(DsDetector* det, int display_index, int enabled) {
    if (!det) return -1;
    std::lock_guard<std::mutex> lock(det->mtx);
    if (display_index < 0 || display_index >= static_cast<int>(det->cached_displays.size())) {
        set_error(det, "Display index out of range");
        return -1;
    }
    bool ok = det->detector->set_hdr(det->cached_displays[display_index], enabled != 0);
    if (!ok) set_error(det, "set_hdr failed (unsupported or CoreDisplay API unavailable)");
    return ok ? 0 : -1;
}

DS_API void ds_free_string(const char* str) {
    std::free(const_cast<char*>(str));
}

DS_API const char* ds_get_thunderbolt_topology(void) {
    try {
        std::string json = displayswitch::get_thunderbolt_topology_json();
        if (json == "[]") return nullptr;
        return dup_str(json);
    } catch (...) {
        return nullptr;
    }
}

DS_API const char* ds_version(void) {
    return "2.0.0";
}

DS_API const char* ds_last_error(void) {
    return g_last_error.c_str();
}

} /* extern "C" */
