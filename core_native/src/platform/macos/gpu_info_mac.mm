/**
 * DisplaySwitch Native — macOS GPU Detection
 *
 * Uses IOKit to detect GPU information on macOS.
 */
#ifdef __APPLE__

#include "displayswitch/display_detector.h"

#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>

#include <string>
#include <vector>
#include <cstring>

namespace displayswitch {

static std::string cfstring_to_str(CFStringRef cf) {
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

static uint64_t cfnumber_to_u64(CFTypeRef ref) {
    if (!ref || CFGetTypeID(ref) != CFNumberGetTypeID()) return 0;
    uint64_t val = 0;
    CFNumberGetValue((CFNumberRef)ref, kCFNumberSInt64Type, &val);
    return val;
}

class MacGPUDetector : public GPUDetector {
public:
    MacGPUDetector() { detect_gpus(); }

    std::vector<GPUInfo> get_all_gpus() override { return gpus_; }

    GPUInfo get_gpu_for_adapter(const std::string& adapter_string) override {
        if (gpus_.empty()) return fallback();

        // Try to match by name
        for (auto& g : gpus_) {
            if (g.name.find(adapter_string) != std::string::npos ||
                adapter_string.find(g.name) != std::string::npos) {
                return g;
            }
        }

        return gpus_.front(); // Return first GPU (usually the only one on Mac)
    }

private:
    std::vector<GPUInfo> gpus_;

    GPUInfo fallback() const {
        GPUInfo g;
        g.name = "Apple GPU";
        g.vendor_name = "Apple";
        return g;
    }

    void detect_gpus() {
        // Match IOAccelerator entries (GPU)
        CFMutableDictionaryRef matching = IOServiceMatching("IOAccelerator");
        io_iterator_t iter;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS) {
            gpus_.push_back(fallback());
            return;
        }

        io_service_t service;
        while ((service = IOIteratorNext(iter)) != 0) {
            GPUInfo gpu;

            // Get GPU model name
            CFTypeRef nameRef = IORegistryEntrySearchCFProperty(
                service, kIOServicePlane, CFSTR("model"),
                kCFAllocatorDefault,
                kIORegistryIterateRecursively | kIORegistryIterateParents);

            if (nameRef) {
                if (CFGetTypeID(nameRef) == CFDataGetTypeID()) {
                    CFDataRef data = (CFDataRef)nameRef;
                    const UInt8* bytes = CFDataGetBytePtr(data);
                    CFIndex length = CFDataGetLength(data);
                    gpu.name = std::string(reinterpret_cast<const char*>(bytes), length);
                    // Remove trailing null if present
                    while (!gpu.name.empty() && gpu.name.back() == '\0')
                        gpu.name.pop_back();
                } else if (CFGetTypeID(nameRef) == CFStringGetTypeID()) {
                    gpu.name = cfstring_to_str((CFStringRef)nameRef);
                }
                CFRelease(nameRef);
            }

            // Try IOName as fallback
            if (gpu.name.empty()) {
                io_name_t name;
                if (IORegistryEntryGetName(service, name) == KERN_SUCCESS) {
                    gpu.name = name;
                }
            }

            // Get VRAM size
            CFTypeRef vramRef = IORegistryEntrySearchCFProperty(
                service, kIOServicePlane, CFSTR("VRAM,totalMB"),
                kCFAllocatorDefault,
                kIORegistryIterateRecursively | kIORegistryIterateParents);
            if (vramRef) {
                uint64_t vramMB = cfnumber_to_u64(vramRef);
                gpu.dedicated_vram_bytes = vramMB * 1024 * 1024;
                CFRelease(vramRef);
            }

            // Vendor detection
            CFTypeRef vendorRef = IORegistryEntrySearchCFProperty(
                service, kIOServicePlane, CFSTR("vendor-id"),
                kCFAllocatorDefault,
                kIORegistryIterateRecursively | kIORegistryIterateParents);
            if (vendorRef) {
                if (CFGetTypeID(vendorRef) == CFDataGetTypeID()) {
                    CFDataRef data = (CFDataRef)vendorRef;
                    if (CFDataGetLength(data) >= 4) {
                        const UInt8* bytes = CFDataGetBytePtr(data);
                        gpu.vendor_id = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
                    }
                }
                CFRelease(vendorRef);
            }

            // Set vendor name
            if (gpu.name.find("Apple") != std::string::npos) {
                gpu.vendor_name = "Apple";
            } else if (gpu.vendor_id == 0x10DE) {
                gpu.vendor_name = "NVIDIA";
            } else if (gpu.vendor_id == 0x1002) {
                gpu.vendor_name = "AMD";
            } else if (gpu.vendor_id == 0x8086) {
                gpu.vendor_name = "Intel";
            } else {
                gpu.vendor_name = "Apple";
            }

            if (!gpu.name.empty()) {
                gpus_.push_back(std::move(gpu));
            }

            IOObjectRelease(service);
        }
        IOObjectRelease(iter);

        if (gpus_.empty()) {
            gpus_.push_back(fallback());
        }
    }
};

std::unique_ptr<GPUDetector> create_gpu_detector() {
    return std::make_unique<MacGPUDetector>();
}

} // namespace displayswitch

#endif // __APPLE__
