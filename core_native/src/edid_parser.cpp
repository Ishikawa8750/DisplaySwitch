/**
 * DisplaySwitch Native — EDID Parser
 *
 * Port of python_version/core/edid_reader.py to C++.
 * Parses the 128-byte base block + 128-byte CEA-861 extension.
 */
#include "displayswitch/edid_parser.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace displayswitch {

// ─── helpers ────────────────────────────────────────────────────────────────

static uint16_t read_le16(const uint8_t* p) { return p[0] | (p[1] << 8); }

static std::string manufacturer_from_id(uint16_t raw) {
    // EDID manufacturer ID: 3 × 5-bit compressed ASCII (big-endian)
    uint16_t be = (raw >> 8) | (raw << 8);           // swap bytes
    char c1 = ((be >> 10) & 0x1F) + 'A' - 1;
    char c2 = ((be >>  5) & 0x1F) + 'A' - 1;
    char c3 = ((be >>  0) & 0x1F) + 'A' - 1;
    return {c1, c2, c3};
}

// ─── public ─────────────────────────────────────────────────────────────────

bool EDIDParser::validate_checksum(const uint8_t* block) {
    uint8_t sum = 0;
    for (int i = 0; i < 128; ++i) sum += block[i];
    return sum == 0;
}

EDIDInfo EDIDParser::parse(const uint8_t* data, size_t length) {
    if (length < 128)
        throw std::runtime_error("EDID data too short (< 128 bytes)");

    // Check header: 0x00 FF FF FF FF FF FF 00
    static const uint8_t HDR[] = {0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00};
    if (std::memcmp(data, HDR, 8) != 0)
        throw std::runtime_error("Invalid EDID header");

    EDIDInfo info{};
    info.raw_data.assign(data, data + length);

    parse_base_block(data, info);

    // Extension blocks
    uint8_t ext_count = data[126];
    for (uint8_t ext = 0; ext < ext_count; ++ext) {
        size_t offset = 128 * (ext + 1);
        if (offset + 128 > length) break;
        const uint8_t* blk = data + offset;

        if (blk[0] == 0x02)               // CEA-861
            parse_cea_extension(blk, info);
        else if (blk[0] == 0x70)           // DisplayID
            parse_displayid_extension(blk, info);
    }

    return info;
}

// ─── base block ─────────────────────────────────────────────────────────────

void EDIDParser::parse_base_block(const uint8_t* b, EDIDInfo& info) {
    // Manufacturer (bytes 8-9)
    uint16_t mfr_raw = (b[8] << 8) | b[9];
    auto mfr = manufacturer_from_id(mfr_raw);
    std::strncpy(info.manufacturer, mfr.c_str(), 3);
    info.manufacturer[3] = '\0';

    // Product code (10-11) – store as hex string like Python version
    uint16_t prod = read_le16(b + 10);
    {
        std::ostringstream oss;
        oss << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << prod;
        std::strncpy(info.product_code, oss.str().c_str(), 13);
        info.product_code[13] = '\0';
    }

    // Serial number (12-15)
    info.serial_number = b[12] | (b[13]<<8) | (b[14]<<16) | (b[15]<<24);

    // Manufacture date
    info.manufacture_week = b[16];
    info.manufacture_year = b[17] + 1990;

    // EDID version/revision (18-19) – not stored directly but implied

    // Screen size (21-22) in cm
    info.screen_width_mm  = b[21] * 10;
    info.screen_height_mm = b[22] * 10;

    // Gamma
    info.gamma = (b[23] + 100) / 100.0f;

    // Feature support byte (24)
    uint8_t feat = b[24];
    info.features.dpms_standby       = (feat >> 7) & 1;
    info.features.dpms_suspend       = (feat >> 6) & 1;
    info.features.dpms_active_off    = (feat >> 5) & 1;
    info.features.srgb_default       = (feat >> 2) & 1;
    info.features.preferred_timing_mode = (feat >> 1) & 1;
    info.features.continuous_frequency  = feat & 1;

    // Chromaticity (25-34) – simplified
    // (full decode omitted for brevity, see spec for 10-bit precision)
    info.chromaticity = {};

    // Detailed timing descriptors (bytes 54-125, four 18-byte blocks)
    for (int i = 0; i < 4; ++i) {
        const uint8_t* dtd = b + 54 + i * 18;
        uint16_t pixel_clk = read_le16(dtd);
        if (pixel_clk == 0) continue;  // Not a timing descriptor
        if (i == 0) {
            parse_detailed_timing(dtd, info.preferred_mode);
        }
        VideoMode mode{};
        parse_detailed_timing(dtd, mode);
        info.supported_modes.push_back(mode);
    }
}

// ─── detailed timing ────────────────────────────────────────────────────────

void EDIDParser::parse_detailed_timing(const uint8_t* b, VideoMode& m) {
    m.pixel_clock_khz = read_le16(b) * 10;   // stored in 10 kHz units
    m.h_active   = b[2] | ((b[4] & 0xF0) << 4);
    m.h_blanking = b[3] | ((b[4] & 0x0F) << 8);
    m.v_active   = b[5] | ((b[7] & 0xF0) << 4);
    m.v_blanking = b[6] | ((b[7] & 0x0F) << 8);
    m.width  = m.h_active;
    m.height = m.v_active;
    m.interlaced = (b[17] >> 7) & 1;

    uint32_t h_total = m.h_active + m.h_blanking;
    uint32_t v_total = m.v_active + m.v_blanking;
    if (h_total && v_total) {
        m.precise_refresh_rate =
            static_cast<float>(m.pixel_clock_khz * 1000.0) /
            (static_cast<double>(h_total) * v_total);
        m.refresh_rate = static_cast<uint8_t>(m.precise_refresh_rate + 0.5);
    }
    m.bit_depth = 8;
    m.standard  = VideoMode::Standard::DMT;
}

// ─── CEA-861 extension ──────────────────────────────────────────────────────

void EDIDParser::parse_cea_extension(const uint8_t* blk, EDIDInfo& info) {
    // blk[0] == 0x02 (tag), blk[1] == revision, blk[2] == dtd_offset
    uint8_t dtd_offset = blk[2];
    if (dtd_offset <= 4 || dtd_offset > 127) return;

    size_t pos = 4;
    while (pos < dtd_offset) {
        uint8_t header = blk[pos];
        uint8_t tag  = (header >> 5) & 0x07;
        uint8_t len  =  header & 0x1F;
        if (pos + 1 + len > 128) break;

        const uint8_t* payload = blk + pos + 1;

        switch (tag) {
        case 3: {  // Vendor-Specific Data Block
            if (len < 3) break;
            uint32_t ieee = payload[0] | (payload[1] << 8) | (payload[2] << 16);

            if (ieee == 0x000C03) {
                // HDMI VSDB (HDMI 1.4 / 2.0)
                Connector::HDMICapabilities caps{};
                caps.version = Connector::HDMICapabilities::Version::HDMI_1_4;
                if (len >= 7) {
                    caps.max_tmds_clock_mhz = payload[6] * 5;
                    if (caps.max_tmds_clock_mhz >= 340)
                        caps.version = Connector::HDMICapabilities::Version::HDMI_2_0;
                }
                parse_hdmi_vsdb(payload, len, caps);

                Connector conn{};
                conn.type = Connector::Type::HDMI;
                conn.hdmi_caps = caps;
                info.connectors.push_back(conn);
            }
            else if (ieee == 0xC45DD8) {
                // HDMI Forum VSDB (HDMI 2.1)
                if (!info.connectors.empty()) {
                    auto& caps = info.connectors.back().hdmi_caps;
                    if (caps.has_value())
                        parse_hdmi_hf_vsdb(payload, len, *caps);
                }
            }
            break;
        }
        case 7: {  // Extended tag
            if (len < 1) break;
            uint8_t ext_tag = payload[0];
            if (ext_tag == 6 && len >= 3) {
                // HDR Static Metadata
                HDRMetadata hdr{};
                parse_hdr_metadata(payload, len, hdr);
                info.hdr_metadata = hdr;
            }
            break;
        }
        default:
            break;
        }

        pos += 1 + len;
    }
}

// ─── HDMI VSDB ──────────────────────────────────────────────────────────────

void EDIDParser::parse_hdmi_vsdb(const uint8_t* d, size_t len,
                                  Connector::HDMICapabilities& caps) {
    if (len >= 7) {
        caps.max_tmds_clock_mhz = d[6] * 5;
    }
    // deeper fields (AI content type, 3D, etc.) omitted for brevity
}

// ─── HDMI Forum VSDB (2.1) ─────────────────────────────────────────────────

void EDIDParser::parse_hdmi_hf_vsdb(const uint8_t* d, size_t len,
                                     Connector::HDMICapabilities& caps) {
    if (len < 5) return;

    uint8_t max_frl_byte = d[4];
    // bits [3:0] indicate max FRL rate
    uint8_t frl_bits = max_frl_byte & 0x0F;
    if (frl_bits > 0) {
        caps.frl_support = true;
        caps.version = Connector::HDMICapabilities::Version::HDMI_2_1;

        // Map FRL level to rate
        static const uint8_t frl_rates[] = {0, 3, 6, 6, 8, 10, 12};
        if (frl_bits < sizeof(frl_rates))
            caps.max_frl_rate = frl_rates[frl_bits];
    }

    if (len >= 6) {
        caps.vrr_support  = (d[5] >> 6) & 1;
        caps.allm_support = (d[5] >> 1) & 1;
    }
}

// ─── HDR Metadata ───────────────────────────────────────────────────────────

void EDIDParser::parse_hdr_metadata(const uint8_t* d, size_t len,
                                     HDRMetadata& hdr) {
    if (len < 3) return;
    uint8_t eotf = d[1];
    hdr.sdr_support          = (eotf >> 0) & 1;
    hdr.hdr10_support        = (eotf >> 1) & 1;
    hdr.hlg_support          = (eotf >> 2) & 1;
    // bit 3 reserved, bit 4 sometimes Dolby Vision
    hdr.dolby_vision_support = (eotf >> 3) & 1;
    hdr.hdr10_plus_support   = false;

    if (len >= 4) hdr.max_luminance_nits        = d[3];  // coded value
    if (len >= 5) hdr.max_frame_avg_luminance    = d[4];
    if (len >= 6) hdr.min_luminance_nits         = d[5] / 255.0f;
}

// ─── DisplayID (stub) ───────────────────────────────────────────────────────

void EDIDParser::parse_displayid_extension(const uint8_t*, EDIDInfo&) {
    // DisplayID 2.0 parsing – not common yet, placeholder
}

} // namespace displayswitch
