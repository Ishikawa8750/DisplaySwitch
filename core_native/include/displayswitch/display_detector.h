#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <memory>
#include "edid_parser.h"

namespace displayswitch {

/**
 * @brief GPU information
 */
struct GPUInfo {
    std::string name;              // "NVIDIA GeForce RTX 3070 Ti Laptop GPU"
    std::string vendor_name;       // "NVIDIA"
    uint32_t vendor_id = 0;        // 0x10DE
    uint32_t device_id = 0;        // 0x24A0
    uint64_t dedicated_vram_bytes = 0;
    uint64_t shared_system_bytes = 0;
    std::string driver_version;
    int adapter_index = 0;

    std::string formatted_name() const;
};

/**
 * @brief Bandwidth capability assessment
 */
struct BandwidthInfo {
    double max_bandwidth_gbps = 0;
    std::string bandwidth_str;
    bool can_support_4k60  = false;
    bool can_support_4k120 = false;
    bool can_support_8k60  = false;
};

/**
 * @brief A detected display (monitor)
 */
struct DisplayInfo {
    // Identity
    std::string name;                // "BenQ EW3270U"
    std::string device_path;         // "\\.\DISPLAY5"
    std::string manufacturer_id;     // "BNQ"
    std::string product_code;        // "0x7950"
    bool is_internal = false;

    // GPU
    GPUInfo gpu;

    // Connection
    std::string connection_type;     // "HDMI", "DisplayPort", "Internal LCD"
    double refresh_rate = 0;

    // HDMI / DP specifics
    std::string hdmi_version;        // "HDMI 1.4"
    std::string hdmi_frl_rate;       // "FRL 6 (48Gbps)"
    uint16_t max_tmds_clock_mhz = 0;

    // HDR
    bool supports_hdr = false;
    std::vector<std::string> hdr_formats;

    // Physical
    uint16_t screen_width_mm  = 0;
    uint16_t screen_height_mm = 0;

    // Resolution
    uint16_t resolution_width  = 0;
    uint16_t resolution_height = 0;
    std::string resolution_str;
    uint8_t bits_per_pixel = 0;

    // Bandwidth
    BandwidthInfo bandwidth;

    // DDC/CI
    void* physical_handle = nullptr; // HPHYSICALMONITOR on Windows
    bool  has_physical_handle = false; // Handle value 0 is valid on Windows!
    int current_input = -1;
    std::vector<int> supported_inputs;
    int cached_brightness = -1;       // Filled during scan to avoid DDC/CI bus contention

    // Full EDID
    EDIDInfo edid;
};

/**
 * @brief Platform-agnostic display detector interface
 */
class DisplayDetector {
public:
    virtual ~DisplayDetector() = default;

    /** Scan all connected monitors */
    virtual std::vector<DisplayInfo> scan() = 0;

    /** Brightness control (DDC/CI VCP 0x10) */
    virtual bool set_brightness(DisplayInfo& display, int level) = 0;
    virtual int  get_brightness(DisplayInfo& display) = 0;

    /** Input source (DDC/CI VCP 0x60) */
    virtual bool set_input(DisplayInfo& display, int input_code) = 0;
    virtual int  get_input(DisplayInfo& display) = 0;

    /** HDR mode control */
    virtual bool get_hdr_enabled(DisplayInfo& display) { (void)display; return false; }
    virtual bool set_hdr(DisplayInfo& display, bool enabled) { (void)display; (void)enabled; return false; }

    /** Release resources */
    virtual void close() = 0;
};

/**
 * @brief Factory: create platform detector
 */
std::unique_ptr<DisplayDetector> create_detector();

/**
 * @brief Get Thunderbolt/USB4 topology as JSON string (macOS only)
 */
std::string get_thunderbolt_topology_json();

/**
 * @brief GPU detection (DXGI on Windows)
 */
class GPUDetector {
public:
    virtual ~GPUDetector() = default;
    virtual std::vector<GPUInfo> get_all_gpus() = 0;
    virtual GPUInfo get_gpu_for_adapter(const std::string& adapter_string) = 0;
};

std::unique_ptr<GPUDetector> create_gpu_detector();

/**
 * @brief Bandwidth calculator
 */
BandwidthInfo calculate_bandwidth(const std::string& connection_type,
                                  const std::string& hdmi_version,
                                  uint16_t max_tmds_mhz,
                                  uint8_t frl_rate);

} // namespace displayswitch
