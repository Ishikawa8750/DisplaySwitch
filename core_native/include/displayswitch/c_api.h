/**
 * DisplaySwitch Native — C API for FFI
 *
 * Flat C interface suitable for calling from Rust, Python ctypes, C#, etc.
 * All strings are UTF-8, all memory is managed by the library unless noted.
 *
 * Usage:
 *   DsDetector* det = ds_create_detector();
 *   int count = 0;
 *   DsDisplayInfoC* displays = ds_scan(det, &count);
 *   for (int i = 0; i < count; i++) { ... displays[i] ... }
 *   ds_free_displays(displays, count);
 *   ds_destroy_detector(det);
 */

#ifndef DISPLAYSWITCH_C_API_H
#define DISPLAYSWITCH_C_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── DLL export macro ──────────────────────────────────────────────────── */
#ifdef _WIN32
  #ifdef DS_BUILD_DLL
    #define DS_API __declspec(dllexport)
  #else
    #define DS_API __declspec(dllimport)
  #endif
#else
  #define DS_API __attribute__((visibility("default")))
#endif

/* ─── Opaque handle ─────────────────────────────────────────────────────── */
typedef struct DsDetector DsDetector;

/* ─── C-compatible data structures ──────────────────────────────────────── */

typedef struct DsGPUInfoC {
    const char* name;               /* e.g. "NVIDIA GeForce RTX 3070 Ti Laptop GPU" */
    const char* vendor_name;        /* e.g. "NVIDIA" */
    uint64_t    dedicated_vram_bytes;
    const char* driver_version;
    const char* formatted_name;     /* e.g. "NVIDIA GeForce RTX 3070 Ti (8.0GB)" */
} DsGPUInfoC;

typedef struct DsBandwidthInfoC {
    double      max_bandwidth_gbps;
    const char* bandwidth_str;      /* e.g. "10.2 Gbps (HDMI 1.4)" */
    int         can_support_4k60;   /* bool: 0 or 1 */
    int         can_support_4k120;
    int         can_support_8k60;
} DsBandwidthInfoC;

typedef struct DsDisplayInfoC {
    const char* name;               /* e.g. "BenQ EW3270U" */
    const char* device_path;        /* e.g. "\\\\.\\DISPLAY1" */
    const char* manufacturer_id;    /* e.g. "BNQ" */
    const char* product_code;       /* e.g. "0x7950" */
    int         is_internal;        /* bool */

    DsGPUInfoC  gpu;

    const char* connection_type;    /* e.g. "HDMI 1.4" */
    double      refresh_rate;
    const char* hdmi_version;       /* nullable: "HDMI 2.0" */
    const char* hdmi_frl_rate;      /* nullable */
    uint16_t    max_tmds_clock_mhz;

    int         supports_hdr;       /* bool */
    const char* hdr_formats;        /* comma-separated: "HDR10, HLG" */

    uint16_t    screen_width_mm;
    uint16_t    screen_height_mm;

    uint16_t    resolution_width;
    uint16_t    resolution_height;
    const char* resolution_str;     /* e.g. "3840x2160" */
    uint8_t     bits_per_pixel;

    DsBandwidthInfoC bandwidth;

    int         current_input;      /* VCP 0x60 value, -1 if unknown */
    const int*  supported_inputs;   /* array */
    int         supported_inputs_count;
    int         brightness;         /* 0-100, -1 if unavailable */
} DsDisplayInfoC;

/* ─── Lifecycle ─────────────────────────────────────────────────────────── */

/** Create a new detector. Returns NULL on failure. */
DS_API DsDetector* ds_create_detector(void);

/** Destroy a detector and free all associated resources. */
DS_API void ds_destroy_detector(DsDetector* det);

/* ─── Scanning ──────────────────────────────────────────────────────────── */

/**
 * Scan for connected monitors.
 * Returns a heap-allocated array of DsDisplayInfoC.
 * Caller MUST call ds_free_displays() when done.
 * Sets *out_count to the number of displays found.
 */
DS_API DsDisplayInfoC* ds_scan(DsDetector* det, int* out_count);

/** Free a display array returned by ds_scan(). */
DS_API void ds_free_displays(DsDisplayInfoC* displays, int count);

/* ─── Control ───────────────────────────────────────────────────────────── */

/** Set brightness (0-100) for display at index. Returns 0 on success, -1 on error. */
DS_API int ds_set_brightness(DsDetector* det, int display_index, int level);

/** Get brightness for display at index. Returns 0-100, or -1 on error. */
DS_API int ds_get_brightness(DsDetector* det, int display_index);

/** Set input source (VCP 0x60) for display at index. Returns 0 on success. */
DS_API int ds_set_input(DsDetector* det, int display_index, int input_code);

/** Get current input source. Returns VCP code or -1 on error. */
DS_API int ds_get_input(DsDetector* det, int display_index);

/* ─── Utility ───────────────────────────────────────────────────────────── */

/** Get the library version string. Do NOT free the returned pointer. */
DS_API const char* ds_version(void);

/** Get last error message. Do NOT free. Returns "" if no error. */
DS_API const char* ds_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAYSWITCH_C_API_H */
