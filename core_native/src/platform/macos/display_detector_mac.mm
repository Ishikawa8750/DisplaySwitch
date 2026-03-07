/**
 * DisplaySwitch Native — macOS Display Detector
 *
 * Uses CoreGraphics + IOKit to detect displays and read EDID.
 * DDC/CI brightness control via IOKit I2C (external monitors).
 * Internal display brightness via IOKit backlight.
 */
#ifdef __APPLE__

#include "displayswitch/display_detector.h"
#include "displayswitch/edid_parser.h"

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/graphics/IOGraphicsLib.h>

// IOKit I2C headers declare C functions; must be wrapped for ObjC++ compilation
extern "C" {
#import <IOKit/i2c/IOI2CInterface.h>
}

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <cstring>
#include <thread>
#include <chrono>

namespace displayswitch {

// ─── Helpers ────────────────────────────────────────────────────────────────

static std::string cfstring_to_string(CFStringRef cf) {
    if (!cf) return {};
    CFIndex len = CFStringGetLength(cf);
    CFIndex maxSize = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::string result(maxSize, '\0');
    if (CFStringGetCString(cf, result.data(), maxSize, kCFStringEncodingUTF8)) {
        result.resize(std::strlen(result.c_str()));
        return result;
    }
    return {};
}

static std::string manufacturer_from_id(uint32_t vendor) {
    // EDID manufacturer ID is 3-letter PnP ID encoded in vendor number
    // CGDisplayVendorNumber returns the 16-bit manufacturer code
    if (vendor == 0) return "Unknown";

    // Decode PnP ID: bits 14-10 = first letter, 9-5 = second, 4-0 = third
    char id[4];
    id[0] = static_cast<char>(((vendor >> 10) & 0x1F) + 'A' - 1);
    id[1] = static_cast<char>(((vendor >> 5)  & 0x1F) + 'A' - 1);
    id[2] = static_cast<char>(((vendor)       & 0x1F) + 'A' - 1);
    id[3] = '\0';
    return std::string(id);
}

// ─── EDID reading via IOKit ─────────────────────────────────────────────────

static std::vector<uint8_t> get_edid_for_display(CGDirectDisplayID displayID) {
    std::vector<uint8_t> result;

    // Get IOKit service port for this display
    io_service_t service = CGDisplayIOServicePort(displayID);
    if (service == MACH_PORT_NULL || service == 0) {
        // CGDisplayIOServicePort is deprecated; try IOKit enumeration
        CFMutableDictionaryRef matching = IOServiceMatching("IODisplayConnect");
        io_iterator_t iter;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS)
            return result;

        io_service_t serv;
        uint32_t targetVendor = CGDisplayVendorNumber(displayID);
        uint32_t targetModel  = CGDisplayModelNumber(displayID);

        while ((serv = IOIteratorNext(iter)) != 0) {
            CFDictionaryRef infoDict = (CFDictionaryRef)IODisplayCreateInfoDictionary(serv, kIODisplayOnlyPreferredName);
            if (infoDict) {
                CFNumberRef vendorRef = (CFNumberRef)CFDictionaryGetValue(infoDict, CFSTR(kDisplayVendorID));
                CFNumberRef productRef = (CFNumberRef)CFDictionaryGetValue(infoDict, CFSTR(kDisplayProductID));

                uint32_t vendor = 0, product = 0;
                if (vendorRef) CFNumberGetValue(vendorRef, kCFNumberIntType, &vendor);
                if (productRef) CFNumberGetValue(productRef, kCFNumberIntType, &product);

                if (vendor == targetVendor && product == targetModel) {
                    // Found matching display - get EDID
                    CFDataRef edidData = (CFDataRef)IORegistryEntryCreateCFProperty(serv, CFSTR(kIODisplayEDIDKey), kCFAllocatorDefault, 0);
                    if (edidData) {
                        const UInt8* bytes = CFDataGetBytePtr(edidData);
                        CFIndex length = CFDataGetLength(edidData);
                        result.assign(bytes, bytes + length);
                        CFRelease(edidData);
                    }
                    CFRelease(infoDict);
                    IOObjectRelease(serv);
                    break;
                }
                CFRelease(infoDict);
            }
            IOObjectRelease(serv);
        }
        IOObjectRelease(iter);
        return result;
    }

    // Direct service port available
    CFDataRef edidData = (CFDataRef)IORegistryEntryCreateCFProperty(service, CFSTR(kIODisplayEDIDKey), kCFAllocatorDefault, 0);
    if (edidData) {
        const UInt8* bytes = CFDataGetBytePtr(edidData);
        CFIndex length = CFDataGetLength(edidData);
        result.assign(bytes, bytes + length);
        CFRelease(edidData);
    }

    return result;
}

// ─── Display name from IOKit ────────────────────────────────────────────────

static std::string get_display_name(CGDirectDisplayID displayID) {
    // Try IOKit display info dictionary
    CFMutableDictionaryRef matching = IOServiceMatching("IODisplayConnect");
    io_iterator_t iter;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS)
        return "Unknown Display";

    io_service_t serv;
    uint32_t targetVendor = CGDisplayVendorNumber(displayID);
    uint32_t targetModel  = CGDisplayModelNumber(displayID);

    std::string name;
    while ((serv = IOIteratorNext(iter)) != 0) {
        CFDictionaryRef infoDict = (CFDictionaryRef)IODisplayCreateInfoDictionary(serv, kIODisplayOnlyPreferredName);
        if (infoDict) {
            CFNumberRef vendorRef = (CFNumberRef)CFDictionaryGetValue(infoDict, CFSTR(kDisplayVendorID));
            CFNumberRef productRef = (CFNumberRef)CFDictionaryGetValue(infoDict, CFSTR(kDisplayProductID));

            uint32_t vendor = 0, product = 0;
            if (vendorRef) CFNumberGetValue(vendorRef, kCFNumberIntType, &vendor);
            if (productRef) CFNumberGetValue(productRef, kCFNumberIntType, &product);

            if (vendor == targetVendor && product == targetModel) {
                // Get localized name
                CFDictionaryRef names = (CFDictionaryRef)CFDictionaryGetValue(infoDict, CFSTR(kDisplayProductName));
                if (names && CFDictionaryGetCount(names) > 0) {
                    CFIndex count = CFDictionaryGetCount(names);
                    std::vector<const void*> keys(count), values(count);
                    CFDictionaryGetKeysAndValues(names, keys.data(), values.data());
                    if (count > 0) {
                        name = cfstring_to_string((CFStringRef)values[0]);
                    }
                }
                CFRelease(infoDict);
                IOObjectRelease(serv);
                break;
            }
            CFRelease(infoDict);
        }
        IOObjectRelease(serv);
    }
    IOObjectRelease(iter);

    return name.empty() ? "Unknown Display" : name;
}

// ─── Connection type detection ──────────────────────────────────────────────

static std::string detect_connection_type(CGDirectDisplayID displayID, bool is_internal) {
    if (is_internal) return "Internal LCD";

    // Try to determine from IOKit
    io_service_t service = CGDisplayIOServicePort(displayID);
    if (service != MACH_PORT_NULL && service != 0) {
        // Walk up the IOKit tree to find the framebuffer
        io_service_t parent = 0;
        kern_return_t kr = IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent);
        if (kr == KERN_SUCCESS && parent) {
            CFTypeRef connectionType = IORegistryEntryCreateCFProperty(parent, CFSTR("ConnectionType"), kCFAllocatorDefault, 0);
            if (connectionType) {
                if (CFGetTypeID(connectionType) == CFStringGetTypeID()) {
                    std::string ct = cfstring_to_string((CFStringRef)connectionType);
                    CFRelease(connectionType);
                    IOObjectRelease(parent);
                    if (ct.find("HDMI") != std::string::npos) return "HDMI";
                    if (ct.find("DisplayPort") != std::string::npos || ct.find("DP") != std::string::npos) return "DisplayPort";
                    if (ct.find("USB") != std::string::npos) return "USB-C";
                    if (ct.find("Thunderbolt") != std::string::npos) return "Thunderbolt";
                    return ct;
                }
                CFRelease(connectionType);
            }
            IOObjectRelease(parent);
        }
    }

    // Default for external: assume DisplayPort/USB-C (most common on modern Macs)
    return "External";
}

// ─── Internal display brightness via IOKit ──────────────────────────────────

static bool set_internal_brightness(float level) {
    // level: 0.0 - 1.0
    io_iterator_t iter;
    CFMutableDictionaryRef matching = IOServiceMatching("IODisplayConnect");
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS)
        return false;

    io_service_t service;
    bool success = false;
    while ((service = IOIteratorNext(iter)) != 0) {
        kern_return_t kr = IODisplaySetFloatParameter(service, kNilOptions, CFSTR(kIODisplayBrightnessKey), level);
        if (kr == KERN_SUCCESS) {
            success = true;
        }
        IOObjectRelease(service);
        if (success) break;
    }
    IOObjectRelease(iter);
    return success;
}

static float get_internal_brightness() {
    io_iterator_t iter;
    CFMutableDictionaryRef matching = IOServiceMatching("IODisplayConnect");
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS)
        return -1.0f;

    io_service_t service;
    float brightness = -1.0f;
    while ((service = IOIteratorNext(iter)) != 0) {
        float val = 0;
        kern_return_t kr = IODisplayGetFloatParameter(service, kNilOptions, CFSTR(kIODisplayBrightnessKey), &val);
        if (kr == KERN_SUCCESS) {
            brightness = val;
        }
        IOObjectRelease(service);
        if (brightness >= 0) break;
    }
    IOObjectRelease(iter);
    return brightness;
}

// ─── DDC/CI via IOKit I2C (external monitors) ──────────────────────────────

static io_service_t get_i2c_service_for_display(CGDirectDisplayID displayID) {
    // Find the IOFramebuffer for this display
    CFMutableDictionaryRef matching = IOServiceMatching("IOFramebuffer");
    io_iterator_t iter;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS)
        return 0;

    io_service_t fb;
    io_service_t result = 0;
    while ((fb = IOIteratorNext(iter)) != 0) {
        // Check display ID match
        CFTypeRef propDisplayID = IORegistryEntryCreateCFProperty(fb, CFSTR("IOFBDependentIndex"), kCFAllocatorDefault, 0);
        if (propDisplayID) {
            CFRelease(propDisplayID);
        }

        // Check if this framebuffer has I2C
        IOItemCount busCount = 0;
        if (IOFBGetI2CInterfaceCount(fb, &busCount) == KERN_SUCCESS && busCount > 0) {
            result = fb;
            break;
        }
        IOObjectRelease(fb);
    }
    IOObjectRelease(iter);
    return result;
}

static bool ddc_write(CGDirectDisplayID displayID, uint8_t command, uint8_t value) {
    io_service_t fb = get_i2c_service_for_display(displayID);
    if (!fb) return false;

    io_service_t interface;
    IOI2CConnectRef connect;

    if (IOFBCopyI2CInterfaceForBus(fb, 0, &interface) != KERN_SUCCESS) {
        IOObjectRelease(fb);
        return false;
    }

    if (IOI2CInterfaceOpen(interface, kNilOptions, &connect) != KERN_SUCCESS) {
        IOObjectRelease(interface);
        IOObjectRelease(fb);
        return false;
    }

    // DDC/CI write: addr 0x37, command in data
    uint8_t data[7];
    data[0] = 0x51;  // DDC/CI source address
    data[1] = 0x84;  // Length: 4 bytes follow
    data[2] = 0x03;  // Set VCP Feature opcode
    data[3] = command;
    data[4] = (value >> 8) & 0xFF;  // value high byte
    data[5] = value & 0xFF;         // value low byte
    // Checksum
    uint8_t checksum = 0x6E;  // destination address XOR
    for (int i = 0; i < 6; i++) checksum ^= data[i];
    data[6] = checksum;

    IOI2CRequest request = {};
    request.commFlags = 0;
    request.sendAddress = 0x6E;  // DDC/CI display address (write)
    request.sendTransactionType = kIOI2CSimpleTransactionType;
    request.sendBuffer = (vm_address_t)data;
    request.sendBytes = 7;

    bool success = IOI2CSendRequest(connect, kNilOptions, &request) == KERN_SUCCESS
                   && request.result == KERN_SUCCESS;

    IOI2CInterfaceClose(connect, kNilOptions);
    IOObjectRelease(interface);
    IOObjectRelease(fb);
    return success;
}

static int ddc_read(CGDirectDisplayID displayID, uint8_t command) {
    io_service_t fb = get_i2c_service_for_display(displayID);
    if (!fb) return -1;

    io_service_t interface;
    IOI2CConnectRef connect;

    if (IOFBCopyI2CInterfaceForBus(fb, 0, &interface) != KERN_SUCCESS) {
        IOObjectRelease(fb);
        return -1;
    }

    if (IOI2CInterfaceOpen(interface, kNilOptions, &connect) != KERN_SUCCESS) {
        IOObjectRelease(interface);
        IOObjectRelease(fb);
        return -1;
    }

    // Step 1: Send "Get VCP Feature" request
    uint8_t sendData[5];
    sendData[0] = 0x51;
    sendData[1] = 0x82;   // Length: 2 bytes follow
    sendData[2] = 0x01;   // Get VCP Feature opcode
    sendData[3] = command;
    uint8_t checksum = 0x6E;
    for (int i = 0; i < 4; i++) checksum ^= sendData[i];
    sendData[4] = checksum;

    IOI2CRequest request = {};
    request.commFlags = 0;
    request.sendAddress = 0x6E;
    request.sendTransactionType = kIOI2CSimpleTransactionType;
    request.sendBuffer = (vm_address_t)sendData;
    request.sendBytes = 5;

    kern_return_t kr = IOI2CSendRequest(connect, kNilOptions, &request);
    if (kr != KERN_SUCCESS || request.result != KERN_SUCCESS) {
        IOI2CInterfaceClose(connect, kNilOptions);
        IOObjectRelease(interface);
        IOObjectRelease(fb);
        return -1;
    }

    // Wait for display to process
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    // Step 2: Read reply
    uint8_t replyData[12] = {};
    IOI2CRequest readReq = {};
    readReq.commFlags = 0;
    readReq.replyAddress = 0x6F;  // DDC/CI display address (read)
    readReq.replyTransactionType = kIOI2CSimpleTransactionType;
    readReq.replyBuffer = (vm_address_t)replyData;
    readReq.replyBytes = 12;

    kr = IOI2CSendRequest(connect, kNilOptions, &readReq);

    IOI2CInterfaceClose(connect, kNilOptions);
    IOObjectRelease(interface);
    IOObjectRelease(fb);

    if (kr != KERN_SUCCESS || readReq.result != KERN_SUCCESS)
        return -1;

    // Parse reply: [source, length, result_code, opcode(0x02), type, max_hi, max_lo, cur_hi, cur_lo, checksum]
    if (readReq.replyBytes >= 10 && replyData[2] == 0x02) {
        // No error
    } else if (readReq.replyBytes >= 10 && replyData[3] == 0x02) {
        // Offset by 1
        return (replyData[8] << 8) | replyData[9];
    }

    if (readReq.replyBytes >= 9) {
        return (replyData[7] << 8) | replyData[8];
    }

    return -1;
}

// ─── macOS Display Detector ─────────────────────────────────────────────────

class MacDisplayDetector : public DisplayDetector {
public:
    std::vector<DisplayInfo> scan() override {
        displays_.clear();
        display_ids_.clear();

        // Get active display list
        uint32_t maxDisplays = 16;
        CGDirectDisplayID displayIDs[16];
        uint32_t displayCount = 0;

        CGError err = CGGetActiveDisplayList(maxDisplays, displayIDs, &displayCount);
        if (err != kCGErrorSuccess) {
            std::cerr << "[DisplaySwitch] CGGetActiveDisplayList failed: " << err << std::endl;
            return {};
        }

        std::cerr << "[DisplaySwitch] Found " << displayCount << " active display(s)" << std::endl;

        for (uint32_t i = 0; i < displayCount; ++i) {
            CGDirectDisplayID did = displayIDs[i];
            display_ids_.push_back(did);

            DisplayInfo d;
            bool is_builtin = CGDisplayIsBuiltin(did);
            d.is_internal = is_builtin;

            // ── Display name ────────────────────────────────────────────
            d.name = get_display_name(did);
            if (d.name == "Unknown Display" && is_builtin) {
                d.name = "Built-in Display";
            }

            // ── Manufacturer / product ──────────────────────────────────
            uint32_t vendorNumber = CGDisplayVendorNumber(did);
            uint32_t modelNumber  = CGDisplayModelNumber(did);
            d.manufacturer_id = manufacturer_from_id(vendorNumber);
            {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "0x%04X", modelNumber);
                d.product_code = buf;
            }

            // ── Device path ─────────────────────────────────────────────
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "display-%u", did);
                d.device_path = buf;
            }

            // ── Resolution ──────────────────────────────────────────────
            CGDisplayModeRef mode = CGDisplayCopyDisplayMode(did);
            if (mode) {
                d.resolution_width  = static_cast<uint16_t>(CGDisplayModeGetPixelWidth(mode));
                d.resolution_height = static_cast<uint16_t>(CGDisplayModeGetPixelHeight(mode));
                d.refresh_rate      = CGDisplayModeGetRefreshRate(mode);
                d.bits_per_pixel    = 32; // macOS always reports 32bpp for modern displays
                CGDisplayModeRelease(mode);
            } else {
                // Fallback to bounds
                CGRect bounds = CGDisplayBounds(did);
                d.resolution_width  = static_cast<uint16_t>(bounds.size.width);
                d.resolution_height = static_cast<uint16_t>(bounds.size.height);
            }

            // If refresh rate is 0, check if this is ProMotion (variable)
            if (d.refresh_rate < 1.0) {
                d.refresh_rate = 60.0; // Default assumption
            }

            d.resolution_str = std::to_string(d.resolution_width) + "x" + std::to_string(d.resolution_height);

            // ── Physical size ───────────────────────────────────────────
            CGSize size_mm = CGDisplayScreenSize(did);
            d.screen_width_mm  = static_cast<uint16_t>(size_mm.width);
            d.screen_height_mm = static_cast<uint16_t>(size_mm.height);

            // ── Connection type ─────────────────────────────────────────
            d.connection_type = detect_connection_type(did, is_builtin);

            // ── EDID ────────────────────────────────────────────────────
            auto edid_data = get_edid_for_display(did);
            if (!edid_data.empty() && edid_data.size() >= 128) {
                try {
                    d.edid = EDIDParser::parse(edid_data.data(), edid_data.size());

                    // Enrich from EDID
                    if (d.edid.manufacturer[0] != '\0') {
                        d.manufacturer_id = d.edid.manufacturer;
                    }
                    if (d.edid.product_code[0] != '\0') {
                        d.product_code = d.edid.product_code;
                    }
                    if (d.edid.screen_width_mm > 0) {
                        d.screen_width_mm  = d.edid.screen_width_mm;
                        d.screen_height_mm = d.edid.screen_height_mm;
                    }

                    // HDMI info from EDID
                    for (auto& conn : d.edid.connectors) {
                        if (conn.hdmi_caps) {
                            auto& hc = *conn.hdmi_caps;
                            d.max_tmds_clock_mhz = hc.max_tmds_clock_mhz;
                            switch (hc.version) {
                                case Connector::HDMICapabilities::Version::HDMI_2_1:
                                    d.hdmi_version = "HDMI 2.1"; break;
                                case Connector::HDMICapabilities::Version::HDMI_2_0:
                                    d.hdmi_version = "HDMI 2.0"; break;
                                default:
                                    d.hdmi_version = "HDMI 1.4"; break;
                            }
                            if (hc.frl_support && hc.max_frl_rate > 0) {
                                d.hdmi_frl_rate = "FRL " + std::to_string(hc.max_frl_rate);
                            }
                            break; // Use first HDMI connector
                        }
                    }

                    // HDR
                    if (d.edid.hdr_metadata) {
                        auto& hdr = *d.edid.hdr_metadata;
                        d.supports_hdr = hdr.hdr10_support || hdr.hlg_support || hdr.dolby_vision_support;
                        if (hdr.hdr10_support) d.hdr_formats.push_back("HDR10");
                        if (hdr.hlg_support)   d.hdr_formats.push_back("HLG");
                        if (hdr.dolby_vision_support) d.hdr_formats.push_back("Dolby Vision");
                        if (hdr.hdr10_plus_support)   d.hdr_formats.push_back("HDR10+");
                    }

                    // Update name from EDID if we got a better one
                    std::string edid_name;
                    // The EDID product_code field may contain the monitor name
                    if (d.name == "Unknown Display" || d.name == "Built-in Display") {
                        if (d.edid.manufacturer[0] != '\0') {
                            edid_name = std::string(d.edid.manufacturer) + " " + d.edid.product_code;
                            if (edid_name.length() > 4) {
                                d.name = edid_name;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[DisplaySwitch] EDID parse error for display " << did << ": " << e.what() << std::endl;
                }
            }

            // ── GPU info (placeholder — filled by GPU detector) ─────────
            d.gpu.name = "Apple GPU";
            d.gpu.vendor_name = "Apple";

            // ── Bandwidth ───────────────────────────────────────────────
            uint8_t frl = 0;
            if (!d.edid.connectors.empty()) {
                auto& c = d.edid.connectors.front();
                if (c.hdmi_caps) frl = c.hdmi_caps->max_frl_rate;
            }
            d.bandwidth = calculate_bandwidth(d.connection_type, d.hdmi_version,
                                              d.max_tmds_clock_mhz, frl);

            // ── Brightness ──────────────────────────────────────────────
            if (is_builtin) {
                float bri = get_internal_brightness();
                d.cached_brightness = (bri >= 0) ? static_cast<int>(bri * 100.0f) : -1;
            } else {
                // Try DDC/CI for external monitors
                int bri = ddc_read(did, 0x10); // VCP 0x10 = Luminance
                d.cached_brightness = bri;
            }

            // ── Input source ────────────────────────────────────────────
            if (!is_builtin) {
                int inp = ddc_read(did, 0x60); // VCP 0x60 = Input Select
                d.current_input = inp;
            }

            std::cerr << "[DisplaySwitch] Display " << i << ": " << d.name
                      << " (" << d.resolution_str << " @ " << d.refresh_rate << "Hz)"
                      << (is_builtin ? " [internal]" : " [external]")
                      << std::endl;

            displays_.push_back(std::move(d));
        }

        return displays_;
    }

    bool set_brightness(DisplayInfo& display, int level) override {
        auto idx = find_display_index(display);
        if (idx < 0 || idx >= (int)display_ids_.size()) return false;

        CGDirectDisplayID did = display_ids_[idx];

        if (display.is_internal) {
            return set_internal_brightness(static_cast<float>(level) / 100.0f);
        }

        // External: DDC/CI VCP 0x10
        return ddc_write(did, 0x10, static_cast<uint8_t>(level));
    }

    int get_brightness(DisplayInfo& display) override {
        auto idx = find_display_index(display);
        if (idx < 0 || idx >= (int)display_ids_.size()) return -1;

        CGDirectDisplayID did = display_ids_[idx];

        if (display.is_internal) {
            float bri = get_internal_brightness();
            return (bri >= 0) ? static_cast<int>(bri * 100.0f) : -1;
        }

        return ddc_read(did, 0x10);
    }

    bool set_input(DisplayInfo& display, int input_code) override {
        auto idx = find_display_index(display);
        if (idx < 0 || idx >= (int)display_ids_.size()) return false;

        if (display.is_internal) return false;

        CGDirectDisplayID did = display_ids_[idx];
        return ddc_write(did, 0x60, static_cast<uint8_t>(input_code));
    }

    int get_input(DisplayInfo& display) override {
        auto idx = find_display_index(display);
        if (idx < 0 || idx >= (int)display_ids_.size()) return -1;

        if (display.is_internal) return -1;

        CGDirectDisplayID did = display_ids_[idx];
        return ddc_read(did, 0x60);
    }

    void close() override {
        displays_.clear();
        display_ids_.clear();
    }

    ~MacDisplayDetector() override { close(); }

private:
    std::vector<DisplayInfo> displays_;
    std::vector<CGDirectDisplayID> display_ids_;

    int find_display_index(const DisplayInfo& display) const {
        for (size_t i = 0; i < displays_.size(); i++) {
            if (displays_[i].device_path == display.device_path) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

std::unique_ptr<DisplayDetector> create_detector() {
    return std::make_unique<MacDisplayDetector>();
}

} // namespace displayswitch

#endif // __APPLE__
