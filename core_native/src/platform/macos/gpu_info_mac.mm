/**
 * DisplaySwitch Native — macOS GPU Info (stub)
 */
#ifdef __APPLE__

#include "displayswitch/display_detector.h"

namespace displayswitch {

class MacGPUDetector : public GPUDetector {
public:
    std::vector<GPUInfo> get_all_gpus() override { return {}; }
    GPUInfo get_gpu_for_adapter(const std::string&) override {
        GPUInfo g; g.name = "Apple GPU"; return g;
    }
};

std::unique_ptr<GPUDetector> create_gpu_detector() {
    return std::make_unique<MacGPUDetector>();
}

} // namespace displayswitch

#endif // __APPLE__
