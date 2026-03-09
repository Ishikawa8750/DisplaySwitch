/**
 * DisplaySwitch Native — Linux GPU Detection
 *
 * Uses /sys/class/drm and /proc/driver for GPU enumeration.
 * Reads PCI device info from sysfs for vendor/device identification.
 */
#ifdef __linux__

#include "displayswitch/display_detector.h"

#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace displayswitch {

static std::string read_sysfs_string(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::string val;
    std::getline(f, val);
    // Trim whitespace
    while (!val.empty() && (val.back() == '\n' || val.back() == '\r' || val.back() == ' '))
        val.pop_back();
    return val;
}

static uint32_t read_sysfs_hex(const std::string& path) {
    std::string val = read_sysfs_string(path);
    if (val.empty()) return 0;
    // sysfs values are often in 0xNNNN format
    try {
        return static_cast<uint32_t>(std::stoul(val, nullptr, 16));
    } catch (...) {
        return 0;
    }
}

static std::string vendor_name_from_id(uint32_t vid) {
    switch (vid) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        case 0x1414: return "Microsoft";
        case 0x5143: return "Qualcomm";
        default:     return "Unknown";
    }
}

class LinuxGPUDetector : public GPUDetector {
public:
    LinuxGPUDetector() { enumerate(); }

    std::vector<GPUInfo> get_all_gpus() override {
        return gpus_;
    }

    GPUInfo get_gpu_for_adapter(const std::string& adapter_string) override {
        for (const auto& gpu : gpus_) {
            if (gpu.name.find(adapter_string) != std::string::npos) {
                return gpu;
            }
        }
        return gpus_.empty() ? GPUInfo{} : gpus_[0];
    }

private:
    std::vector<GPUInfo> gpus_;

    void enumerate() {
        // Scan /sys/class/drm/card*/device for PCI GPU info
        try {
            for (const auto& entry : fs::directory_iterator("/sys/class/drm")) {
                std::string name = entry.path().filename().string();
                // Match "card0", "card1", etc. (not card0-HDMI-A-1)
                if (name.find("card") != 0 || name.find('-') != std::string::npos)
                    continue;

                std::string device_path = entry.path().string() + "/device";
                if (!fs::exists(device_path)) continue;

                GPUInfo gpu;

                uint32_t vendor_id = read_sysfs_hex(device_path + "/vendor");
                uint32_t device_id = read_sysfs_hex(device_path + "/device");
                gpu.vendor_id = vendor_id;
                gpu.device_id = device_id;
                gpu.vendor_name = vendor_name_from_id(vendor_id);

                // Try to read the device name
                gpu.name = read_sysfs_string(device_path + "/label");
                if (gpu.name.empty()) {
                    // Fallback: use vendor + device ID
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%s GPU [%04X:%04X]",
                        gpu.vendor_name.c_str(), vendor_id, device_id);
                    gpu.name = buf;
                }

                // Try to read VRAM from /sys/class/drm/cardX/device/mem_info_vram_total
                // (AMD GPUs expose this)
                std::string vram_path = device_path + "/mem_info_vram_total";
                if (fs::exists(vram_path)) {
                    std::ifstream f(vram_path);
                    if (f) f >> gpu.dedicated_vram_bytes;
                }

                // Try DRM-specific memory info
                if (gpu.dedicated_vram_bytes == 0) {
                    std::string drm_mem = entry.path().string() + "/device/resource";
                    // NVIDIA: try nvidia-smi output or resource file
                    // For now, leave at 0 — it's optional
                }

                // Driver version
                gpu.driver_version = read_sysfs_string(device_path + "/driver/module/version");
                if (gpu.driver_version.empty()) {
                    // Try kernel module version
                    std::string uevent = read_sysfs_string(device_path + "/uevent");
                    if (uevent.find("DRIVER=") != std::string::npos) {
                        auto pos = uevent.find("DRIVER=");
                        auto end = uevent.find('\n', pos);
                        gpu.driver_version = uevent.substr(pos + 7,
                            end == std::string::npos ? std::string::npos : end - pos - 7);
                    }
                }

                gpu.adapter_index = static_cast<int>(gpus_.size());
                gpus_.push_back(gpu);
            }
        } catch (const std::exception& e) {
            std::cerr << "[GPUDetector] Error enumerating GPUs: " << e.what() << std::endl;
        }

        if (gpus_.empty()) {
            GPUInfo fallback;
            fallback.name = "Unknown GPU";
            fallback.vendor_name = "Unknown";
            gpus_.push_back(fallback);
        }
    }
};

std::unique_ptr<GPUDetector> create_gpu_detector() {
    return std::make_unique<LinuxGPUDetector>();
}

} // namespace displayswitch

#endif // __linux__
