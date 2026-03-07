#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace displayswitch {

// ─── VideoMode (must be defined before EDIDInfo uses it by value) ───────────

/**
 * @brief Video mode timing information
 */
struct VideoMode {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t refresh_rate = 0;           // Integer part (60, 120, 144, etc.)
    float precise_refresh_rate = 0;     // Exact value (59.94, 119.88, etc.)
    uint8_t bit_depth = 0;              // 6, 8, 10, 12
    
    // Timing details
    uint32_t pixel_clock_khz = 0;
    uint16_t h_active = 0;
    uint16_t h_blanking = 0;
    uint16_t v_active = 0;
    uint16_t v_blanking = 0;
    
    // Flags
    bool interlaced = false;
    bool reduced_blanking = false;      // CVT-RB, CVT-RB2
    
    enum class Standard {
        DMT,                        // VESA Display Monitor Timing
        CEA,                        // Consumer Electronics Association
        CVT,                        // Coordinated Video Timings
        GTF,                        // Generalized Timing Formula
        Custom
    } standard = Standard::DMT;
};

/**
 * @brief Physical connector information
 */
struct Connector {
    enum class Type {
        Unknown,
        VGA,
        DVI_Single,
        DVI_Dual,
        HDMI,
        DisplayPort,
        USB_C,
        Thunderbolt,
        Internal_LVDS,
        Internal_eDP
    } type = Type::Unknown;
    
    uint8_t physical_index = 0;     // Physical port number on the monitor
    
    // HDMI-specific
    struct HDMICapabilities {
        enum class Version {
            HDMI_1_4,
            HDMI_2_0,
            HDMI_2_1
        } version = Version::HDMI_1_4;
        
        bool frl_support = false;           // Fixed Rate Link (HDMI 2.1)
        uint8_t max_frl_rate = 0;           // 3, 6, 8, 10, 12 (Gbps per lane)
        uint8_t frl_lanes = 0;              // Usually 3 or 4
        
        bool vrr_support = false;           // Variable Refresh Rate
        bool allm_support = false;          // Auto Low Latency Mode
        bool qms_support = false;           // Quick Media Switching
        
        uint16_t max_tmds_clock_mhz = 0;    // For HDMI 2.0
    };
    std::optional<HDMICapabilities> hdmi_caps;
    
    // DisplayPort-specific
    struct DisplayPortCapabilities {
        enum class Version {
            DP_1_2,
            DP_1_3,
            DP_1_4,
            DP_2_0,
            DP_2_1
        } version = Version::DP_1_2;
        
        uint8_t lane_count = 0;             // 1, 2, or 4
        uint32_t max_link_rate_mbps = 0;    // 5400, 8100, 13500, 20000, etc.
        
        bool mst_support = false;           // Multi-Stream Transport
        bool dsc_support = false;           // Display Stream Compression
        bool adaptive_sync = false;         // FreeSync/G-Sync Compatible
    };
    std::optional<DisplayPortCapabilities> dp_caps;
    
    // USB-C-specific
    struct USBCCapabilities {
        bool dp_alt_mode = false;
        uint8_t dp_lanes = 0;               // 2 or 4
        bool thunderbolt_3 = false;
        bool thunderbolt_4 = false;
        uint16_t power_delivery_watts = 0;  // USB-PD power capability
    };
    std::optional<USBCCapabilities> usbc_caps;
    
    // VCP input source code (for DDC/CI switching)
    uint8_t vcp_input_code = 0;     // 0x60 parameter (0x0F=DP, 0x11=HDMI1, etc.)
};

/**
 * @brief HDR metadata from EDID
 */
struct HDRMetadata {
    bool sdr_support = false;
    bool hdr10_support = false;
    bool dolby_vision_support = false;
    bool hdr10_plus_support = false;
    bool hlg_support = false;               // Hybrid Log-Gamma
    
    // Static metadata (ST.2086)
    uint16_t max_luminance_nits = 0;        // Max content light level
    uint16_t max_frame_avg_luminance = 0;   // Max frame average light level
    float min_luminance_nits = 0;           // Min content light level
    
    // Primaries (DCI-P3, BT.2020, etc.)
    struct {
        float red_x = 0, red_y = 0;
        float green_x = 0, green_y = 0;
        float blue_x = 0, blue_y = 0;
    } color_primaries;
};

// ─── EDIDInfo (all member types are now fully defined above) ────────────────

/**
 * @brief Complete EDID information structure
 */
struct EDIDInfo {
    // Basic identification
    char manufacturer[4]{};           // "BNQ", "DEL", "SAM", etc.
    char product_code[14]{};          // "EW3270U"
    uint32_t serial_number = 0;
    uint16_t manufacture_week = 0;
    uint16_t manufacture_year = 0;

    // Physical display characteristics
    uint16_t screen_width_mm = 0;
    uint16_t screen_height_mm = 0;
    float gamma = 0;

    // Color characteristics
    struct {
        float red_x = 0, red_y = 0;
        float green_x = 0, green_y = 0;
        float blue_x = 0, blue_y = 0;
        float white_x = 0, white_y = 0;
    } chromaticity;

    // Feature support
    struct {
        bool dpms_standby = false;
        bool dpms_suspend = false;
        bool dpms_active_off = false;
        bool srgb_default = false;
        bool preferred_timing_mode = false;
        bool continuous_frequency = false;
    } features;

    // Detailed timing (preferred mode)
    VideoMode preferred_mode{};

    // All supported video modes
    std::vector<VideoMode> supported_modes;

    // Physical connectors
    std::vector<Connector> connectors;

    // HDR capabilities (from CEA-861.3 extension)
    std::optional<HDRMetadata> hdr_metadata;

    // VCP (DDC/CI) supported codes
    std::vector<uint8_t> vcp_codes;

    // Raw EDID data
    std::vector<uint8_t> raw_data;
};

/**
 * @brief EDID parser class
 */
class EDIDParser {
public:
    /**
     * @brief Parse EDID data
     * @param data Raw EDID data (128+ bytes)
     * @param length Length of EDID data
     * @return Parsed EDID information
     * @throws std::runtime_error if EDID is invalid
     */
    static EDIDInfo parse(const uint8_t* data, size_t length);
    
    /**
     * @brief Validate EDID checksum
     * @param block EDID block (128 bytes)
     * @return true if checksum is valid
     */
    static bool validate_checksum(const uint8_t* block);
    
private:
    static void parse_base_block(const uint8_t* block, EDIDInfo& info);
    static void parse_detailed_timing(const uint8_t* block, VideoMode& mode);
    static void parse_cea_extension(const uint8_t* block, EDIDInfo& info);
    static void parse_displayid_extension(const uint8_t* block, EDIDInfo& info);
    static void parse_hdr_metadata(const uint8_t* data, size_t length, HDRMetadata& hdr);
    static void parse_hdmi_vsdb(const uint8_t* data, size_t length, Connector::HDMICapabilities& caps);
    static void parse_hdmi_hf_vsdb(const uint8_t* data, size_t length, Connector::HDMICapabilities& caps);
};

} // namespace displayswitch
