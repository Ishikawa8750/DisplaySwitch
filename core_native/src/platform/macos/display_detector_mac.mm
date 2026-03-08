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
#include <dlfcn.h>

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

// ─── Private API resolution (Apple Silicon DDC/CI + DisplayServices brightness) ──

// IOAVService APIs use an opaque CFTypeRef (IOAVServiceRef) on Apple Silicon,
// not a raw io_service_t. IOAVServiceCreateWithService wraps the io_service_t.
typedef IOReturn (*IOAVServiceWriteI2C_fn)(CFTypeRef avService, uint32_t chipAddress,
                                            uint32_t dataAddress, void *inputBuffer,
                                            uint32_t inputBufferSize);
typedef IOReturn (*IOAVServiceReadI2C_fn)(CFTypeRef avService, uint32_t chipAddress,
                                           uint32_t dataAddress, void *outputBuffer,
                                           uint32_t outputBufferSize);
typedef CFTypeRef (*IOAVServiceCreateWithService_fn)(CFAllocatorRef allocator,
                                                      io_service_t service);

static IOAVServiceWriteI2C_fn g_IOAVServiceWriteI2C = nullptr;
static IOAVServiceReadI2C_fn g_IOAVServiceReadI2C = nullptr;
static IOAVServiceCreateWithService_fn g_IOAVServiceCreateWithService = nullptr;

typedef int (*DSGetBrightness_fn)(CGDirectDisplayID display, float *brightness);
typedef int (*DSSetBrightness_fn)(CGDirectDisplayID display, float brightness);

static DSGetBrightness_fn g_DSGetBrightness = nullptr;
static DSSetBrightness_fn g_DSSetBrightness = nullptr;

static bool g_private_apis_resolved = false;

static void resolve_private_apis() {
    if (g_private_apis_resolved) return;
    g_private_apis_resolved = true;

    void *iokit = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_LAZY);
    if (iokit) {
        g_IOAVServiceWriteI2C = (IOAVServiceWriteI2C_fn)dlsym(iokit, "IOAVServiceWriteI2C");
        g_IOAVServiceReadI2C = (IOAVServiceReadI2C_fn)dlsym(iokit, "IOAVServiceReadI2C");
        g_IOAVServiceCreateWithService = (IOAVServiceCreateWithService_fn)dlsym(iokit, "IOAVServiceCreateWithService");
    }

    void *ds = dlopen("/System/Library/PrivateFrameworks/DisplayServices.framework/DisplayServices", RTLD_LAZY);
    if (ds) {
        g_DSGetBrightness = (DSGetBrightness_fn)dlsym(ds, "DisplayServicesGetBrightness");
        g_DSSetBrightness = (DSSetBrightness_fn)dlsym(ds, "DisplayServicesSetBrightness");
    }
}

static bool avservice_available() {
    resolve_private_apis();
    return g_IOAVServiceWriteI2C != nullptr && g_IOAVServiceReadI2C != nullptr
           && g_IOAVServiceCreateWithService != nullptr;
}

// ─── EDID reading via IOKit ─────────────────────────────────────────────────

static std::vector<uint8_t> get_edid_for_display(CGDirectDisplayID displayID) {
    std::vector<uint8_t> result;

    // Method 1: Try CGDisplayIOServicePort (deprecated but works on Intel)
    io_service_t service = CGDisplayIOServicePort(displayID);
    if (service != MACH_PORT_NULL && service != 0) {
        CFDataRef edidData = (CFDataRef)IORegistryEntryCreateCFProperty(
            service, CFSTR(kIODisplayEDIDKey), kCFAllocatorDefault, 0);
        if (edidData) {
            const UInt8* bytes = CFDataGetBytePtr(edidData);
            CFIndex length = CFDataGetLength(edidData);
            result.assign(bytes, bytes + length);
            CFRelease(edidData);
            return result;
        }
    }

    uint32_t targetVendor = CGDisplayVendorNumber(displayID);
    uint32_t targetModel  = CGDisplayModelNumber(displayID);

    // Method 2: Try IODisplayConnect (Intel Macs)
    {
        CFMutableDictionaryRef matching = IOServiceMatching("IODisplayConnect");
        io_iterator_t iter;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) == KERN_SUCCESS) {
            io_service_t serv;
            while ((serv = IOIteratorNext(iter)) != 0) {
                CFDictionaryRef infoDict = (CFDictionaryRef)IODisplayCreateInfoDictionary(
                    serv, kIODisplayOnlyPreferredName);
                if (infoDict) {
                    CFNumberRef vendorRef = (CFNumberRef)CFDictionaryGetValue(infoDict, CFSTR(kDisplayVendorID));
                    CFNumberRef productRef = (CFNumberRef)CFDictionaryGetValue(infoDict, CFSTR(kDisplayProductID));
                    uint32_t vendor = 0, product = 0;
                    if (vendorRef) CFNumberGetValue(vendorRef, kCFNumberIntType, &vendor);
                    if (productRef) CFNumberGetValue(productRef, kCFNumberIntType, &product);
                    if (vendor == targetVendor && product == targetModel) {
                        CFDataRef edidData = (CFDataRef)IORegistryEntryCreateCFProperty(
                            serv, CFSTR(kIODisplayEDIDKey), kCFAllocatorDefault, 0);
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
            if (!result.empty()) return result;
        }
    }

    // Method 3: Apple Silicon — search IOMobileFramebufferShim subtrees for EDID
    // Match by DisplayAttributes.ProductAttributes.LegacyManufacturerID
    {
        CFMutableDictionaryRef matching = IOServiceMatching("IOMobileFramebufferShim");
        io_iterator_t iter;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) == KERN_SUCCESS) {
            io_service_t serv;
            while ((serv = IOIteratorNext(iter)) != 0) {
                CFTypeRef attrs = IORegistryEntryCreateCFProperty(
                    serv, CFSTR("DisplayAttributes"), kCFAllocatorDefault, 0);
                if (attrs && CFGetTypeID(attrs) == CFDictionaryGetTypeID()) {
                    CFDictionaryRef prodAttrs = (CFDictionaryRef)CFDictionaryGetValue(
                        (CFDictionaryRef)attrs, CFSTR("ProductAttributes"));
                    if (prodAttrs && CFGetTypeID(prodAttrs) == CFDictionaryGetTypeID()) {
                        CFNumberRef mfrRef = (CFNumberRef)CFDictionaryGetValue(
                            prodAttrs, CFSTR("LegacyManufacturerID"));
                        int mfr = 0;
                        if (mfrRef) CFNumberGetValue(mfrRef, kCFNumberIntType, &mfr);
                        if ((uint32_t)mfr == targetVendor) {
                            // Found matching framebuffer — search subtree for EDID
                            CFTypeRef edid = IORegistryEntrySearchCFProperty(
                                serv, kIOServicePlane, CFSTR("EDID"),
                                kCFAllocatorDefault, kIORegistryIterateRecursively);
                            if (edid && CFGetTypeID(edid) == CFDataGetTypeID()) {
                                const UInt8* bytes = CFDataGetBytePtr((CFDataRef)edid);
                                CFIndex length = CFDataGetLength((CFDataRef)edid);
                                result.assign(bytes, bytes + length);
                                CFRelease(edid);
                            }
                            if (edid && CFGetTypeID(edid) != CFDataGetTypeID()) CFRelease(edid);
                        }
                    }
                }
                if (attrs) CFRelease(attrs);
                IOObjectRelease(serv);
                if (!result.empty()) break;
            }
            IOObjectRelease(iter);
        }
    }

    return result;
}

// ─── Display name from IOKit ────────────────────────────────────────────────

static std::string get_display_name(CGDirectDisplayID displayID) {
    uint32_t targetVendor = CGDisplayVendorNumber(displayID);
    uint32_t targetModel  = CGDisplayModelNumber(displayID);

    // Method 1: IODisplayConnect (Intel Macs)
    {
        CFMutableDictionaryRef matching = IOServiceMatching("IODisplayConnect");
        io_iterator_t iter;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) == KERN_SUCCESS) {
            io_service_t serv;
            while ((serv = IOIteratorNext(iter)) != 0) {
                CFDictionaryRef infoDict = (CFDictionaryRef)IODisplayCreateInfoDictionary(
                    serv, kIODisplayOnlyPreferredName);
                if (infoDict) {
                    CFNumberRef vendorRef = (CFNumberRef)CFDictionaryGetValue(infoDict, CFSTR(kDisplayVendorID));
                    CFNumberRef productRef = (CFNumberRef)CFDictionaryGetValue(infoDict, CFSTR(kDisplayProductID));
                    uint32_t vendor = 0, product = 0;
                    if (vendorRef) CFNumberGetValue(vendorRef, kCFNumberIntType, &vendor);
                    if (productRef) CFNumberGetValue(productRef, kCFNumberIntType, &product);
                    if (vendor == targetVendor && product == targetModel) {
                        CFDictionaryRef names = (CFDictionaryRef)CFDictionaryGetValue(
                            infoDict, CFSTR(kDisplayProductName));
                        if (names && CFDictionaryGetCount(names) > 0) {
                            CFIndex count = CFDictionaryGetCount(names);
                            std::vector<const void*> keys(count), values(count);
                            CFDictionaryGetKeysAndValues(names, keys.data(), values.data());
                            std::string name = cfstring_to_string((CFStringRef)values[0]);
                            if (!name.empty()) {
                                CFRelease(infoDict);
                                IOObjectRelease(serv);
                                IOObjectRelease(iter);
                                return name;
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
        }
    }

    // Method 2: Apple Silicon — search IOMobileFramebufferShim subtree for ProductName
    {
        CFMutableDictionaryRef matching = IOServiceMatching("IOMobileFramebufferShim");
        io_iterator_t iter;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) == KERN_SUCCESS) {
            io_service_t serv;
            while ((serv = IOIteratorNext(iter)) != 0) {
                CFTypeRef attrs = IORegistryEntryCreateCFProperty(
                    serv, CFSTR("DisplayAttributes"), kCFAllocatorDefault, 0);
                if (attrs && CFGetTypeID(attrs) == CFDictionaryGetTypeID()) {
                    CFDictionaryRef prodAttrs = (CFDictionaryRef)CFDictionaryGetValue(
                        (CFDictionaryRef)attrs, CFSTR("ProductAttributes"));
                    if (prodAttrs && CFGetTypeID(prodAttrs) == CFDictionaryGetTypeID()) {
                        CFNumberRef mfrRef = (CFNumberRef)CFDictionaryGetValue(
                            prodAttrs, CFSTR("LegacyManufacturerID"));
                        int mfr = 0;
                        if (mfrRef) CFNumberGetValue(mfrRef, kCFNumberIntType, &mfr);
                        if ((uint32_t)mfr == targetVendor) {
                            // Search subtree for ProductName
                            CFTypeRef pn = IORegistryEntrySearchCFProperty(
                                serv, kIOServicePlane, CFSTR("ProductName"),
                                kCFAllocatorDefault, kIORegistryIterateRecursively);
                            if (pn && CFGetTypeID(pn) == CFStringGetTypeID()) {
                                std::string name = cfstring_to_string((CFStringRef)pn);
                                CFRelease(pn);
                                if (!name.empty()) {
                                    CFRelease(attrs);
                                    IOObjectRelease(serv);
                                    IOObjectRelease(iter);
                                    return name;
                                }
                            }
                            if (pn) CFRelease(pn);
                        }
                    }
                }
                if (attrs) CFRelease(attrs);
                IOObjectRelease(serv);
            }
            IOObjectRelease(iter);
        }
    }

    return "Unknown Display";
}

// ─── Connection type detection ──────────────────────────────────────────────────────

static bool str_contains_ci(const std::string& str, const std::string& sub) {
    if (sub.empty()) return true;
    auto it = std::search(str.begin(), str.end(), sub.begin(), sub.end(),
        [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    return it != str.end();
}

static std::string parse_transport_description(const std::string& transport) {
    // Parse strings like "Port-USB-C@2/DisplayPort", "HDMI", "DisplayPort", etc.
    if (str_contains_ci(transport, "HDMI")) return "HDMI";
    if (str_contains_ci(transport, "DisplayPort") || str_contains_ci(transport, "/DP"))
        return "DisplayPort";
    if (str_contains_ci(transport, "Thunderbolt")) return "Thunderbolt";
    if (str_contains_ci(transport, "USB-C") || str_contains_ci(transport, "USB_C"))
        return "USB-C";
    if (str_contains_ci(transport, "DVI")) return "DVI";
    if (str_contains_ci(transport, "VGA")) return "VGA";
    return "";
}

static std::string detect_connection_type(CGDirectDisplayID displayID, bool is_internal) {
    if (is_internal) return "Internal LCD";

    uint32_t targetVendor = CGDisplayVendorNumber(displayID);
    uint32_t targetModel  = CGDisplayModelNumber(displayID);

    // Method 1: Apple Silicon — IOMobileFramebufferShim subtree properties
    {
        CFMutableDictionaryRef matching = IOServiceMatching("IOMobileFramebufferShim");
        io_iterator_t iter;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) == KERN_SUCCESS) {
            io_service_t serv;
            while ((serv = IOIteratorNext(iter)) != 0) {
                CFTypeRef attrs = IORegistryEntryCreateCFProperty(
                    serv, CFSTR("DisplayAttributes"), kCFAllocatorDefault, 0);
                if (attrs && CFGetTypeID(attrs) == CFDictionaryGetTypeID()) {
                    CFDictionaryRef prodAttrs = (CFDictionaryRef)CFDictionaryGetValue(
                        (CFDictionaryRef)attrs, CFSTR("ProductAttributes"));
                    if (prodAttrs && CFGetTypeID(prodAttrs) == CFDictionaryGetTypeID()) {
                        CFNumberRef mfrRef = (CFNumberRef)CFDictionaryGetValue(
                            prodAttrs, CFSTR("LegacyManufacturerID"));
                        int mfr = 0;
                        if (mfrRef) CFNumberGetValue(mfrRef, kCFNumberIntType, &mfr);
                        if ((uint32_t)mfr == targetVendor) {
                            // Try TransportDescription (e.g. "Port-USB-C@2/DisplayPort")
                            CFTypeRef td = IORegistryEntrySearchCFProperty(
                                serv, kIOServicePlane, CFSTR("TransportDescription"),
                                kCFAllocatorDefault, kIORegistryIterateRecursively);
                            if (td && CFGetTypeID(td) == CFStringGetTypeID()) {
                                std::string transport = cfstring_to_string((CFStringRef)td);
                                CFRelease(td);
                                std::string result = parse_transport_description(transport);
                                if (!result.empty()) {
                                    CFRelease(attrs);
                                    IOObjectRelease(serv);
                                    IOObjectRelease(iter);
                                    return result;
                                }
                            }
                            if (td) CFRelease(td);

                            // Try DFP Type Description (e.g. "DP", "HDMI")
                            CFTypeRef dfp = IORegistryEntrySearchCFProperty(
                                serv, kIOServicePlane, CFSTR("DFP Type Description"),
                                kCFAllocatorDefault, kIORegistryIterateRecursively);
                            if (dfp && CFGetTypeID(dfp) == CFStringGetTypeID()) {
                                std::string dfpStr = cfstring_to_string((CFStringRef)dfp);
                                CFRelease(dfp);
                                std::string result = parse_transport_description(dfpStr);
                                if (!result.empty()) {
                                    CFRelease(attrs);
                                    IOObjectRelease(serv);
                                    IOObjectRelease(iter);
                                    return result;
                                }
                            }
                            if (dfp) CFRelease(dfp);

                            // Try ParentBuiltInPortTypeDescription (e.g. "USB-C")
                            CFTypeRef pbt = IORegistryEntrySearchCFProperty(
                                serv, kIOServicePlane, CFSTR("ParentBuiltInPortTypeDescription"),
                                kCFAllocatorDefault, kIORegistryIterateRecursively);
                            if (pbt && CFGetTypeID(pbt) == CFStringGetTypeID()) {
                                std::string pbtStr = cfstring_to_string((CFStringRef)pbt);
                                CFRelease(pbt);
                                std::string result = parse_transport_description(pbtStr);
                                if (!result.empty()) {
                                    CFRelease(attrs);
                                    IOObjectRelease(serv);
                                    IOObjectRelease(iter);
                                    return result;
                                }
                            }
                            if (pbt) CFRelease(pbt);
                        }
                    }
                }
                if (attrs) CFRelease(attrs);
                IOObjectRelease(serv);
            }
            IOObjectRelease(iter);
        }
    }

    // Method 2: IODisplayConnect parent tree walk (Intel Macs)
    {
        CFMutableDictionaryRef matching = IOServiceMatching("IODisplayConnect");
        io_iterator_t iter;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) == KERN_SUCCESS) {
            io_service_t serv;
            io_service_t displayService = 0;
            while ((serv = IOIteratorNext(iter)) != 0) {
                CFDictionaryRef infoDict = (CFDictionaryRef)IODisplayCreateInfoDictionary(
                    serv, kIODisplayOnlyPreferredName);
                if (infoDict) {
                    CFNumberRef vendorRef = (CFNumberRef)CFDictionaryGetValue(
                        infoDict, CFSTR(kDisplayVendorID));
                    CFNumberRef productRef = (CFNumberRef)CFDictionaryGetValue(
                        infoDict, CFSTR(kDisplayProductID));
                    uint32_t vendor = 0, product = 0;
                    if (vendorRef) CFNumberGetValue(vendorRef, kCFNumberIntType, &vendor);
                    if (productRef) CFNumberGetValue(productRef, kCFNumberIntType, &product);
                    CFRelease(infoDict);
                    if (vendor == targetVendor && product == targetModel) {
                        displayService = serv;
                        break;
                    }
                }
                IOObjectRelease(serv);
            }
            IOObjectRelease(iter);

            if (displayService) {
                io_service_t current = displayService;
                std::string result;
                for (int depth = 0; depth < 12 && result.empty(); ++depth) {
                    io_name_t className, ioName;
                    IOObjectGetClass(current, className);
                    IORegistryEntryGetName(current, ioName);
                    std::string cls(className), nameStr(ioName);

                    if (str_contains_ci(cls, "HDMI") || str_contains_ci(nameStr, "HDMI"))
                        { result = "HDMI"; break; }
                    if ((str_contains_ci(cls, "DisplayPort") || str_contains_ci(nameStr, "DisplayPort"))
                        && cls != "IODisplayConnect")
                        { result = "DisplayPort"; break; }
                    if (str_contains_ci(cls, "Thunderbolt") || str_contains_ci(nameStr, "Thunderbolt"))
                        { result = "Thunderbolt"; break; }
                    if (str_contains_ci(cls, "TypeC") || str_contains_ci(nameStr, "TypeC"))
                        { result = "USB-C"; break; }
                    if (str_contains_ci(cls, "DVI") || str_contains_ci(nameStr, "DVI"))
                        { result = "DVI"; break; }

                    CFTypeRef connProp = IORegistryEntryCreateCFProperty(
                        current, CFSTR("connector-type"), kCFAllocatorDefault, 0);
                    if (connProp) {
                        if (CFGetTypeID(connProp) == CFDataGetTypeID()) {
                            CFDataRef data = (CFDataRef)connProp;
                            if (CFDataGetLength(data) >= 4) {
                                const UInt8* bytes = CFDataGetBytePtr(data);
                                uint32_t ct = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
                                switch (ct) {
                                    case 0x02: result = "VGA"; break;
                                    case 0x04: result = "DVI"; break;
                                    case 0x0A: result = "DisplayPort"; break;
                                    case 0x0B: result = "HDMI"; break;
                                }
                            }
                        } else if (CFGetTypeID(connProp) == CFStringGetTypeID()) {
                            std::string ct = cfstring_to_string((CFStringRef)connProp);
                            if (str_contains_ci(ct, "HDMI")) result = "HDMI";
                            else if (str_contains_ci(ct, "DisplayPort") || str_contains_ci(ct, "DP"))
                                result = "DisplayPort";
                            else if (!ct.empty()) result = ct;
                        }
                        CFRelease(connProp);
                    }

                    if (!result.empty()) break;

                    io_service_t parent = 0;
                    if (IORegistryEntryGetParentEntry(current, kIOServicePlane, &parent) != KERN_SUCCESS)
                        break;
                    if (current != displayService) IOObjectRelease(current);
                    current = parent;
                }
                if (current != displayService) IOObjectRelease(current);
                IOObjectRelease(displayService);
                if (!result.empty()) return result;
            }
        }
    }

    // Method 3: Try deprecated CGDisplayIOServicePort (Intel fallback)
    {
        io_service_t service = CGDisplayIOServicePort(displayID);
        if (service != MACH_PORT_NULL && service != 0) {
            io_service_t parent = 0;
            kern_return_t kr = IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent);
            if (kr == KERN_SUCCESS && parent) {
                CFTypeRef connectionType = IORegistryEntryCreateCFProperty(
                    parent, CFSTR("ConnectionType"), kCFAllocatorDefault, 0);
                if (connectionType) {
                    if (CFGetTypeID(connectionType) == CFStringGetTypeID()) {
                        std::string ct = cfstring_to_string((CFStringRef)connectionType);
                        CFRelease(connectionType);
                        IOObjectRelease(parent);
                        if (str_contains_ci(ct, "HDMI")) return "HDMI";
                        if (str_contains_ci(ct, "DisplayPort") || str_contains_ci(ct, "DP"))
                            return "DisplayPort";
                        if (str_contains_ci(ct, "USB")) return "USB-C";
                        if (str_contains_ci(ct, "Thunderbolt")) return "Thunderbolt";
                        return ct;
                    }
                    CFRelease(connectionType);
                }
                IOObjectRelease(parent);
            }
        }
    }

    return "External";
}

// ─── Display brightness ───────────────────────────────────────────────────────────────

static bool set_display_brightness(CGDirectDisplayID displayID, float level) {
    resolve_private_apis();

    // DisplayServices private API (most reliable on modern macOS / Apple Silicon)
    if (g_DSSetBrightness) {
        if (g_DSSetBrightness(displayID, level) == 0) return true;
    }

    // Fallback: IOKit IODisplaySetFloatParameter
    io_iterator_t iter;
    CFMutableDictionaryRef matching = IOServiceMatching("IODisplayConnect");
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS)
        return false;

    io_service_t svc;
    bool success = false;
    while ((svc = IOIteratorNext(iter)) != 0) {
        kern_return_t kr = IODisplaySetFloatParameter(
            svc, kNilOptions, CFSTR(kIODisplayBrightnessKey), level);
        if (kr == KERN_SUCCESS) success = true;
        IOObjectRelease(svc);
        if (success) break;
    }
    IOObjectRelease(iter);
    return success;
}

static float get_display_brightness(CGDirectDisplayID displayID) {
    resolve_private_apis();

    // DisplayServices private API
    if (g_DSGetBrightness) {
        float brightness = 0;
        if (g_DSGetBrightness(displayID, &brightness) == 0) return brightness;
    }

    // Fallback: IOKit IODisplayGetFloatParameter
    io_iterator_t iter;
    CFMutableDictionaryRef matching = IOServiceMatching("IODisplayConnect");
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS)
        return -1.0f;

    io_service_t svc;
    float brightness = -1.0f;
    while ((svc = IOIteratorNext(iter)) != 0) {
        float val = 0;
        kern_return_t kr = IODisplayGetFloatParameter(
            svc, kNilOptions, CFSTR(kIODisplayBrightnessKey), &val);
        if (kr == KERN_SUCCESS) brightness = val;
        IOObjectRelease(svc);
        if (brightness >= 0) break;
    }
    IOObjectRelease(iter);
    return brightness;
}

// ─── DDC/CI via IOAVService (Apple Silicon) ─────────────────────────────────────────

static io_service_t find_avservice_for_display(CGDirectDisplayID displayID) {
    if (!avservice_available()) return 0;

    bool is_builtin = CGDisplayIsBuiltin(displayID);
    uint32_t targetVendor = CGDisplayVendorNumber(displayID);
    uint32_t targetModel  = CGDisplayModelNumber(displayID);

    CFMutableDictionaryRef matching = IOServiceNameMatching("DCPAVServiceProxy");
    io_iterator_t iter;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS)
        return 0;

    io_service_t serv;
    io_service_t fallback = 0; // First external proxy as fallback
    while ((serv = IOIteratorNext(iter)) != 0) {
        // Method 1: Apple Silicon — match by Location property
        CFTypeRef locProp = IORegistryEntryCreateCFProperty(
            serv, CFSTR("Location"), kCFAllocatorDefault, 0);
        if (locProp && CFGetTypeID(locProp) == CFStringGetTypeID()) {
            std::string location = cfstring_to_string((CFStringRef)locProp);
            CFRelease(locProp);

            if (is_builtin && str_contains_ci(location, "Embedded")) {
                // Internal display — skip, DDC not used for internal
                IOObjectRelease(serv);
                continue;
            }
            if (!is_builtin && str_contains_ci(location, "External")) {
                if (!fallback) {
                    IOObjectRetain(serv);
                    fallback = serv;
                }
            }
        } else {
            if (locProp) CFRelease(locProp);
        }

        // Method 2: Intel fallback — match by IODisplayConnect child vendor/model
        io_iterator_t childIter;
        if (IORegistryEntryGetChildIterator(serv, kIOServicePlane, &childIter) == KERN_SUCCESS) {
            io_service_t child;
            bool matched = false;
            while ((child = IOIteratorNext(childIter)) != 0) {
                CFDictionaryRef infoDict = (CFDictionaryRef)IODisplayCreateInfoDictionary(
                    child, kIODisplayOnlyPreferredName);
                if (infoDict) {
                    CFNumberRef vendorRef = (CFNumberRef)CFDictionaryGetValue(
                        infoDict, CFSTR(kDisplayVendorID));
                    CFNumberRef productRef = (CFNumberRef)CFDictionaryGetValue(
                        infoDict, CFSTR(kDisplayProductID));
                    uint32_t vendor = 0, product = 0;
                    if (vendorRef) CFNumberGetValue(vendorRef, kCFNumberIntType, &vendor);
                    if (productRef) CFNumberGetValue(productRef, kCFNumberIntType, &product);
                    CFRelease(infoDict);
                    if (vendor == targetVendor && product == targetModel) matched = true;
                }
                IOObjectRelease(child);
                if (matched) break;
            }
            IOObjectRelease(childIter);
            if (matched) {
                if (fallback) IOObjectRelease(fallback);
                IOObjectRelease(iter);
                return serv;
            }
        }

        IOObjectRelease(serv);
    }
    IOObjectRelease(iter);

    // Return the Location-matched fallback (Apple Silicon path)
    return fallback;
}

static bool avservice_ddc_write(io_service_t service, uint8_t command, uint16_t value) {
    if (!g_IOAVServiceWriteI2C || !g_IOAVServiceCreateWithService) return false;

    // Create IOAVServiceRef from io_service_t
    CFTypeRef avServiceRef = g_IOAVServiceCreateWithService(kCFAllocatorDefault, service);
    if (!avServiceRef) return false;

    uint8_t data[6];
    data[0] = 0x84;
    data[1] = 0x03;
    data[2] = command;
    data[3] = (value >> 8) & 0xFF;
    data[4] = value & 0xFF;
    uint8_t checksum = 0x6E ^ 0x51;
    for (int i = 0; i < 5; i++) checksum ^= data[i];
    data[5] = checksum;
    bool ok = g_IOAVServiceWriteI2C(avServiceRef, 0x37, 0x51, data, 6) == KERN_SUCCESS;
    CFRelease(avServiceRef);
    return ok;
}

static int avservice_ddc_read(io_service_t service, uint8_t command) {
    if (!g_IOAVServiceWriteI2C || !g_IOAVServiceReadI2C || !g_IOAVServiceCreateWithService)
        return -1;

    // Create IOAVServiceRef from io_service_t
    CFTypeRef avServiceRef = g_IOAVServiceCreateWithService(kCFAllocatorDefault, service);
    if (!avServiceRef) return -1;

    uint8_t send[4];
    send[0] = 0x82;
    send[1] = 0x01;
    send[2] = command;
    uint8_t checksum = 0x6E ^ 0x51;
    for (int i = 0; i < 3; i++) checksum ^= send[i];
    send[3] = checksum;
    if (g_IOAVServiceWriteI2C(avServiceRef, 0x37, 0x51, send, 4) != KERN_SUCCESS) {
        CFRelease(avServiceRef);
        return -1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    uint8_t reply[11] = {};
    if (g_IOAVServiceReadI2C(avServiceRef, 0x37, 0x51, reply, 11) != KERN_SUCCESS) {
        CFRelease(avServiceRef);
        return -1;
    }
    CFRelease(avServiceRef);

    // Parse reply - scan for Get VCP Feature Reply opcode (0x02)
    for (int i = 2; i < 8; i++) {
        if (reply[i] == 0x02 && i + 5 < 11)
            return (reply[i + 4] << 8) | reply[i + 5];
    }
    return -1;
}

// ─── DDC/CI via IOFramebuffer I2C (Intel Macs) ─────────────────────────────────────────

static io_service_t get_i2c_service_for_display(CGDirectDisplayID displayID) {
    CFMutableDictionaryRef matching = IOServiceMatching("IOFramebuffer");
    io_iterator_t iter;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS)
        return 0;
    io_service_t fb, result = 0;
    while ((fb = IOIteratorNext(iter)) != 0) {
        IOItemCount busCount = 0;
        if (IOFBGetI2CInterfaceCount(fb, &busCount) == KERN_SUCCESS && busCount > 0) {
            result = fb; break;
        }
        IOObjectRelease(fb);
    }
    IOObjectRelease(iter);
    return result;
}

static bool ioframebuffer_ddc_write(CGDirectDisplayID displayID, uint8_t command, uint16_t value) {
    io_service_t fb = get_i2c_service_for_display(displayID);
    if (!fb) return false;
    io_service_t interface;
    IOI2CConnectRef connect;
    if (IOFBCopyI2CInterfaceForBus(fb, 0, &interface) != KERN_SUCCESS) {
        IOObjectRelease(fb); return false;
    }
    if (IOI2CInterfaceOpen(interface, kNilOptions, &connect) != KERN_SUCCESS) {
        IOObjectRelease(interface); IOObjectRelease(fb); return false;
    }
    uint8_t data[7];
    data[0] = 0x51; data[1] = 0x84; data[2] = 0x03; data[3] = command;
    data[4] = (value >> 8) & 0xFF; data[5] = value & 0xFF;
    uint8_t checksum = 0x6E;
    for (int i = 0; i < 6; i++) checksum ^= data[i];
    data[6] = checksum;
    IOI2CRequest request = {};
    request.sendAddress = 0x6E;
    request.sendTransactionType = kIOI2CSimpleTransactionType;
    request.sendBuffer = (vm_address_t)data;
    request.sendBytes = 7;
    bool success = IOI2CSendRequest(connect, kNilOptions, &request) == KERN_SUCCESS
                   && request.result == KERN_SUCCESS;
    IOI2CInterfaceClose(connect, kNilOptions);
    IOObjectRelease(interface); IOObjectRelease(fb);
    return success;
}

static int ioframebuffer_ddc_read(CGDirectDisplayID displayID, uint8_t command) {
    io_service_t fb = get_i2c_service_for_display(displayID);
    if (!fb) return -1;
    io_service_t interface;
    IOI2CConnectRef connect;
    if (IOFBCopyI2CInterfaceForBus(fb, 0, &interface) != KERN_SUCCESS) {
        IOObjectRelease(fb); return -1;
    }
    if (IOI2CInterfaceOpen(interface, kNilOptions, &connect) != KERN_SUCCESS) {
        IOObjectRelease(interface); IOObjectRelease(fb); return -1;
    }
    uint8_t sendData[5];
    sendData[0] = 0x51; sendData[1] = 0x82; sendData[2] = 0x01; sendData[3] = command;
    uint8_t checksum = 0x6E;
    for (int i = 0; i < 4; i++) checksum ^= sendData[i];
    sendData[4] = checksum;
    IOI2CRequest request = {};
    request.sendAddress = 0x6E;
    request.sendTransactionType = kIOI2CSimpleTransactionType;
    request.sendBuffer = (vm_address_t)sendData;
    request.sendBytes = 5;
    kern_return_t kr = IOI2CSendRequest(connect, kNilOptions, &request);
    if (kr != KERN_SUCCESS || request.result != KERN_SUCCESS) {
        IOI2CInterfaceClose(connect, kNilOptions);
        IOObjectRelease(interface); IOObjectRelease(fb);
        return -1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    uint8_t replyData[12] = {};
    IOI2CRequest readReq = {};
    readReq.replyAddress = 0x6F;
    readReq.replyTransactionType = kIOI2CSimpleTransactionType;
    readReq.replyBuffer = (vm_address_t)replyData;
    readReq.replyBytes = 12;
    kr = IOI2CSendRequest(connect, kNilOptions, &readReq);
    IOI2CInterfaceClose(connect, kNilOptions);
    IOObjectRelease(interface); IOObjectRelease(fb);
    if (kr != KERN_SUCCESS || readReq.result != KERN_SUCCESS) return -1;
    // Parse reply - scan for Get VCP Feature Reply opcode (0x02)
    for (int i = 2; i < 8; i++) {
        if (replyData[i] == 0x02 && i + 5 < 12)
            return (replyData[i + 4] << 8) | replyData[i + 5];
    }
    return -1;
}

// ─── Unified DDC/CI (tries IOAVService first, then IOFramebuffer I2C) ────────────────────

static bool ddc_write(CGDirectDisplayID displayID, uint8_t command, uint16_t value) {
    // Apple Silicon: try IOAVService
    if (avservice_available()) {
        io_service_t avs = find_avservice_for_display(displayID);
        if (avs) {
            bool ok = avservice_ddc_write(avs, command, value);
            IOObjectRelease(avs);
            if (ok) return true;
        }
    }
    // Intel: try IOFramebuffer I2C
    return ioframebuffer_ddc_write(displayID, command, value);
}

static int ddc_read(CGDirectDisplayID displayID, uint8_t command) {
    // Apple Silicon: try IOAVService
    if (avservice_available()) {
        io_service_t avs = find_avservice_for_display(displayID);
        if (avs) {
            int val = avservice_ddc_read(avs, command);
            IOObjectRelease(avs);
            if (val >= 0) return val;
        }
    }
    // Intel: try IOFramebuffer I2C
    return ioframebuffer_ddc_read(displayID, command);
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
                    if (d.name == "Unknown Display" || d.name == "Built-in Display") {
                        // Check if product_code contains a readable monitor name (from 0xFC descriptor)
                        bool has_readable_name = false;
                        if (d.edid.product_code[0] != '\0') {
                            // If product_code does NOT start with "0x", it's a name from 0xFC
                            std::string pc = d.edid.product_code;
                            if (pc.substr(0, 2) != "0x" && pc.length() > 1) {
                                has_readable_name = true;
                            }
                        }

                        if (has_readable_name && d.edid.manufacturer[0] != '\0') {
                            d.name = std::string(d.edid.manufacturer) + " " + d.edid.product_code;
                        } else if (d.edid.manufacturer[0] != '\0') {
                            d.name = std::string(d.edid.manufacturer) + " " + d.edid.product_code;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[DisplaySwitch] EDID parse error for display " << did << ": " << e.what() << std::endl;
                }
            }

            // ── Enrich connection type from EDID ────────────────────────
            if (d.connection_type == "External" && !d.edid.connectors.empty()) {
                for (auto& conn : d.edid.connectors) {
                    if (conn.type == Connector::Type::HDMI) { d.connection_type = "HDMI"; break; }
                    else if (conn.type == Connector::Type::DisplayPort) { d.connection_type = "DisplayPort"; break; }
                    else if (conn.type == Connector::Type::DVI_Single || conn.type == Connector::Type::DVI_Dual)
                        { d.connection_type = "DVI"; break; }
                    else if (conn.type == Connector::Type::USB_C || conn.type == Connector::Type::Thunderbolt)
                        { d.connection_type = "USB-C"; break; }
                }
            }
            if (d.connection_type == "HDMI" && !d.hdmi_version.empty())
                d.connection_type = d.hdmi_version;

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
                float bri = get_display_brightness(did);
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

                // Standard MCCS VCP 0x60 input source codes for modern monitors
                d.supported_inputs = {
                    15,  // DisplayPort-1 (0x0F)
                    16,  // DisplayPort-2 (0x10)
                    17,  // HDMI-1 (0x11)
                    18,  // HDMI-2 (0x12)
                };
                // Ensure current input is in the list
                if (inp > 0) {
                    bool found = false;
                    for (int si : d.supported_inputs) { if (si == inp) { found = true; break; } }
                    if (!found) d.supported_inputs.push_back(inp);
                }
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
            return set_display_brightness(did, static_cast<float>(level) / 100.0f);
        }

        // External: DDC/CI VCP 0x10
        return ddc_write(did, 0x10, static_cast<uint16_t>(level));
    }

    int get_brightness(DisplayInfo& display) override {
        auto idx = find_display_index(display);
        if (idx < 0 || idx >= (int)display_ids_.size()) return -1;

        CGDirectDisplayID did = display_ids_[idx];

        if (display.is_internal) {
            float bri = get_display_brightness(did);
            return (bri >= 0) ? static_cast<int>(bri * 100.0f) : -1;
        }

        return ddc_read(did, 0x10);
    }

    bool set_input(DisplayInfo& display, int input_code) override {
        auto idx = find_display_index(display);
        if (idx < 0 || idx >= (int)display_ids_.size()) return false;

        if (display.is_internal) return false;

        CGDirectDisplayID did = display_ids_[idx];

        // Try writing VCP 0x60 with retries (some monitors need multiple attempts)
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (ddc_write(did, 0x60, static_cast<uint16_t>(input_code))) {
                // Wait for monitor to process the command
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Verify by reading back
                int current = ddc_read(did, 0x60);
                if (current == input_code) return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Final attempt without verification
        return ddc_write(did, 0x60, static_cast<uint16_t>(input_code));
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
