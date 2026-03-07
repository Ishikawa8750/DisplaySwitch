/**
 * Unit test for EDID Parser
 */
#include "displayswitch/edid_parser.h"
#include "displayswitch/display_detector.h"
#include <iostream>
#include <cassert>
#include <cstring>

using namespace displayswitch;

// Minimal valid 128-byte EDID for testing
static uint8_t make_test_edid(uint8_t* buf) {
    std::memset(buf, 0, 128);
    // Header
    buf[0] = 0x00;
    for (int i = 1; i <= 6; ++i) buf[i] = 0xFF;
    buf[7] = 0x00;

    // Manufacturer "TST" = T(20) S(19) T(20) → big-endian compressed
    // T=20=10100, S=19=10011, T=20=10100
    // byte8 = 0101 0100 = 0x54,  byte9 = 1001 1101 00 ... 
    // Actually let's just use "AAA" = 1,1,1 → 00001 00001 00001 = 0x0421 big-endian
    buf[8] = 0x04; buf[9] = 0x21;

    // Product code (bytes 10-11)
    buf[10] = 0x50; buf[11] = 0x79;  // 0x7950 in LE

    // Manufacture year (byte 17) = 2023 - 1990 = 33
    buf[17] = 33;

    // EDID version 1.4
    buf[18] = 1; buf[19] = 4;

    // Screen size: 70cm x 39cm
    buf[21] = 70; buf[22] = 39;

    // Gamma (byte 23) = 220 → 2.20
    buf[23] = 120; // (120+100)/100 = 2.20

    // Extension count = 0
    buf[126] = 0;

    // Fix checksum
    uint8_t sum = 0;
    for (int i = 0; i < 127; ++i) sum += buf[i];
    buf[127] = static_cast<uint8_t>(256 - sum);

    return 128;
}

int main() {
    std::cout << "=== EDID Parser Unit Test ===\n\n";

    // Test 1: Checksum validation
    {
        uint8_t buf[128];
        make_test_edid(buf);
        assert(EDIDParser::validate_checksum(buf));
        std::cout << "[PASS] Checksum validation\n";
    }

    // Test 2: Parse basic block
    {
        uint8_t buf[128];
        make_test_edid(buf);
        auto info = EDIDParser::parse(buf, 128);

        assert(std::string(info.manufacturer) == "AAA");
        assert(info.screen_width_mm == 700);
        assert(info.screen_height_mm == 390);
        assert(info.manufacture_year == 2023);
        std::cout << "[PASS] Base block parsing (manufacturer=" << info.manufacturer
                  << ", size=" << info.screen_width_mm << "x" << info.screen_height_mm << "mm)\n";
    }

    // Test 3: Invalid header
    {
        uint8_t buf[128] = {};
        bool threw = false;
        try { EDIDParser::parse(buf, 128); } catch (...) { threw = true; }
        assert(threw);
        std::cout << "[PASS] Invalid header throws\n";
    }

    // Test 4: Too short
    {
        uint8_t buf[64] = {};
        bool threw = false;
        try { EDIDParser::parse(buf, 64); } catch (...) { threw = true; }
        assert(threw);
        std::cout << "[PASS] Short data throws\n";
    }

    // Test 5: Bandwidth calculation
    {
        auto bw1 = calculate_bandwidth("HDMI", "HDMI 1.4", 600, 0);
        assert(bw1.max_bandwidth_gbps == 18.0);
        assert(bw1.can_support_4k60 == true);
        assert(bw1.can_support_4k120 == false);

        auto bw2 = calculate_bandwidth("HDMI", "HDMI 2.1", 0, 6);
        assert(bw2.max_bandwidth_gbps == 48.0);
        assert(bw2.can_support_8k60 == true);

        auto bw3 = calculate_bandwidth("Internal LCD", "", 0, 0);
        assert(bw3.bandwidth_str == "eDP (Internal)");

        std::cout << "[PASS] Bandwidth calculation\n";
    }

    std::cout << "\n=== All tests passed! ===\n";
    return 0;
}
