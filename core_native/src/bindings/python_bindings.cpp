/**
 * DisplaySwitch Native — PyBind11 Python Bindings
 *
 * Exposes the C++ core as a Python module: `displayswitch_native`
 *
 * Usage:
 *   from displayswitch_native import DisplayDetector
 *   det = DisplayDetector()
 *   for d in det.scan():
 *       print(d.name, d.resolution_str, d.gpu.formatted_name)
 */
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "displayswitch/display_detector.h"
#include "displayswitch/edid_parser.h"

namespace py = pybind11;

using namespace displayswitch;

PYBIND11_MODULE(displayswitch_native, m) {
    m.doc() = "DisplaySwitch Native – high-performance display detection";

    // ── GPUInfo ─────────────────────────────────────────────────────────
    py::class_<GPUInfo>(m, "GPUInfo")
        .def(py::init<>())
        .def_readwrite("name",              &GPUInfo::name)
        .def_readwrite("vendor_name",       &GPUInfo::vendor_name)
        .def_readwrite("vendor_id",         &GPUInfo::vendor_id)
        .def_readwrite("device_id",         &GPUInfo::device_id)
        .def_readwrite("dedicated_vram_bytes", &GPUInfo::dedicated_vram_bytes)
        .def_readwrite("shared_system_bytes",  &GPUInfo::shared_system_bytes)
        .def_readwrite("driver_version",    &GPUInfo::driver_version)
        .def_readwrite("adapter_index",     &GPUInfo::adapter_index)
        .def("formatted_name",             &GPUInfo::formatted_name);

    // ── BandwidthInfo ───────────────────────────────────────────────────
    py::class_<BandwidthInfo>(m, "BandwidthInfo")
        .def(py::init<>())
        .def_readwrite("max_bandwidth_gbps", &BandwidthInfo::max_bandwidth_gbps)
        .def_readwrite("bandwidth_str",      &BandwidthInfo::bandwidth_str)
        .def_readwrite("can_support_4k60",   &BandwidthInfo::can_support_4k60)
        .def_readwrite("can_support_4k120",  &BandwidthInfo::can_support_4k120)
        .def_readwrite("can_support_8k60",   &BandwidthInfo::can_support_8k60);

    // ── DisplayInfo ─────────────────────────────────────────────────────
    py::class_<DisplayInfo>(m, "DisplayInfo")
        .def(py::init<>())
        .def_readwrite("name",              &DisplayInfo::name)
        .def_readwrite("device_path",       &DisplayInfo::device_path)
        .def_readwrite("manufacturer_id",   &DisplayInfo::manufacturer_id)
        .def_readwrite("product_code",      &DisplayInfo::product_code)
        .def_readwrite("is_internal",       &DisplayInfo::is_internal)
        .def_readwrite("gpu",               &DisplayInfo::gpu)
        .def_readwrite("connection_type",   &DisplayInfo::connection_type)
        .def_readwrite("refresh_rate",      &DisplayInfo::refresh_rate)
        .def_readwrite("hdmi_version",      &DisplayInfo::hdmi_version)
        .def_readwrite("hdmi_frl_rate",     &DisplayInfo::hdmi_frl_rate)
        .def_readwrite("max_tmds_clock_mhz", &DisplayInfo::max_tmds_clock_mhz)
        .def_readwrite("supports_hdr",      &DisplayInfo::supports_hdr)
        .def_readwrite("hdr_formats",       &DisplayInfo::hdr_formats)
        .def_readwrite("screen_width_mm",   &DisplayInfo::screen_width_mm)
        .def_readwrite("screen_height_mm",  &DisplayInfo::screen_height_mm)
        .def_readwrite("resolution_width",  &DisplayInfo::resolution_width)
        .def_readwrite("resolution_height", &DisplayInfo::resolution_height)
        .def_readwrite("resolution_str",    &DisplayInfo::resolution_str)
        .def_readwrite("bits_per_pixel",    &DisplayInfo::bits_per_pixel)
        .def_readwrite("bandwidth",         &DisplayInfo::bandwidth)
        .def_readwrite("current_input",     &DisplayInfo::current_input)
        .def_readwrite("supported_inputs",  &DisplayInfo::supported_inputs);

    // ── DisplayDetector ─────────────────────────────────────────────────
    py::class_<DisplayDetector>(m, "DisplayDetector")
        .def(py::init([]() { return create_detector(); }),
             "Create a platform-specific display detector")
        .def("scan", &DisplayDetector::scan,
             "Scan all connected monitors")
        .def("set_brightness", &DisplayDetector::set_brightness,
             py::arg("display"), py::arg("level"),
             "Set brightness (0-100)")
        .def("get_brightness", &DisplayDetector::get_brightness,
             py::arg("display"),
             "Get current brightness")
        .def("set_input", &DisplayDetector::set_input,
             py::arg("display"), py::arg("input_code"),
             "Set input source")
        .def("get_input", &DisplayDetector::get_input,
             py::arg("display"),
             "Get current input source")
        .def("close", &DisplayDetector::close,
             "Release physical monitor handles");

    // ── Free functions ──────────────────────────────────────────────────
    m.def("calculate_bandwidth", &calculate_bandwidth,
          py::arg("connection_type"), py::arg("hdmi_version"),
          py::arg("max_tmds_mhz"), py::arg("frl_rate"),
          "Calculate theoretical bandwidth for a connection");
}
