/**
 * DisplaySwitch Native — macOS Display Detector (stub)
 */
#ifdef __APPLE__

#include "displayswitch/display_detector.h"
#include <iostream>

namespace displayswitch {

class MacDisplayDetector : public DisplayDetector {
public:
    std::vector<DisplayInfo> scan() override {
        // TODO: IOKit + CoreGraphics implementation
        std::cerr << "[DisplaySwitch] macOS display detection not yet implemented\n";
        return {};
    }
    bool set_brightness(DisplayInfo&, int) override { return false; }
    int  get_brightness(DisplayInfo&) override { return -1; }
    bool set_input(DisplayInfo&, int) override { return false; }
    int  get_input(DisplayInfo&) override { return -1; }
    void close() override {}
};

std::unique_ptr<DisplayDetector> create_detector() {
    return std::make_unique<MacDisplayDetector>();
}

} // namespace displayswitch

#endif // __APPLE__
