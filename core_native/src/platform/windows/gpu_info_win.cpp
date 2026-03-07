/**
 * DisplaySwitch Native — Windows GPU Detection via WMI
 *
 * Port of python_version/core/gpu_detector.py to C++.
 * Uses WMI Win32_VideoController (via COM) for reliable GPU info.
 */
#ifdef _WIN32

#include "displayswitch/display_detector.h"

#include <Windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>

#pragma comment(lib, "wbemuuid.lib")

namespace displayswitch {

static std::string bstr_to_utf8(BSTR bstr) {
    if (!bstr) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, bstr, -1, nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, bstr, -1, s.data(), len, nullptr, nullptr);
    return s;
}

static std::string to_lower_copy(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

static std::string vendor_name(uint32_t vid) {
    switch (vid) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        case 0x1414: return "Microsoft";
        case 0x5143: return "Qualcomm";
        default:     return "Unknown";
    }
}

class WindowsGPUDetector : public GPUDetector {
public:
    WindowsGPUDetector() { query_wmi(); }

    std::vector<GPUInfo> get_all_gpus() override { return gpus_; }

    GPUInfo get_gpu_for_adapter(const std::string& adapter_string) override {
        if (adapter_string.empty()) return fallback();

        std::string al = to_lower_copy(adapter_string);

        // Exact / substring match
        for (auto& g : gpus_) {
            std::string gl = to_lower_copy(g.name);
            if (gl == al || gl.find(al) != std::string::npos || al.find(gl) != std::string::npos)
                return g;
        }

        return fallback();
    }

private:
    std::vector<GPUInfo> gpus_;

    GPUInfo fallback() const {
        if (!gpus_.empty()) return gpus_.front();
        GPUInfo g; g.name = "Unknown GPU"; return g;
    }

    void query_wmi() {
        HRESULT hr;

        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool co_init = SUCCEEDED(hr);

        hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                                  RPC_C_AUTHN_LEVEL_DEFAULT,
                                  RPC_C_IMP_LEVEL_IMPERSONATE,
                                  nullptr, EOAC_NONE, nullptr);

        IWbemLocator* locator = nullptr;
        hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IWbemLocator, reinterpret_cast<void**>(&locator));
        if (FAILED(hr)) { if (co_init) CoUninitialize(); return; }

        IWbemServices* svc = nullptr;
        hr = locator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr,
                                     nullptr, 0, nullptr, nullptr, &svc);
        if (FAILED(hr)) { locator->Release(); if (co_init) CoUninitialize(); return; }

        hr = CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                               RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                               nullptr, EOAC_NONE);

        IEnumWbemClassObject* enumerator = nullptr;
        hr = svc->ExecQuery(
            _bstr_t(L"WQL"),
            _bstr_t(L"SELECT Name, AdapterRAM, DriverVersion, PNPDeviceID FROM Win32_VideoController"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &enumerator);

        if (SUCCEEDED(hr)) {
            IWbemClassObject* obj = nullptr;
            ULONG ret = 0;
            int idx = 0;
            while (enumerator->Next(WBEM_INFINITE, 1, &obj, &ret) == S_OK) {
                GPUInfo g;
                g.adapter_index = idx++;

                VARIANT v;
                if (SUCCEEDED(obj->Get(L"Name", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR)
                    g.name = bstr_to_utf8(v.bstrVal);
                VariantClear(&v);

                if (SUCCEEDED(obj->Get(L"AdapterRAM", 0, &v, nullptr, nullptr))) {
                    if (v.vt == VT_I4 || v.vt == VT_UI4)
                        g.dedicated_vram_bytes = static_cast<uint64_t>(static_cast<uint32_t>(v.lVal));
                }
                VariantClear(&v);

                if (SUCCEEDED(obj->Get(L"DriverVersion", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR)
                    g.driver_version = bstr_to_utf8(v.bstrVal);
                VariantClear(&v);

                if (SUCCEEDED(obj->Get(L"PNPDeviceID", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR) {
                    std::string pnp = bstr_to_utf8(v.bstrVal);
                    // parse VEN_ / DEV_
                    auto parse_hex = [&](const std::string& prefix) -> uint32_t {
                        auto pos = pnp.find(prefix);
                        if (pos == std::string::npos) return 0;
                        return static_cast<uint32_t>(std::stoul(pnp.substr(pos + prefix.size(), 4), nullptr, 16));
                    };
                    g.vendor_id = parse_hex("VEN_");
                    g.device_id = parse_hex("DEV_");
                    g.vendor_name = vendor_name(g.vendor_id);
                }
                VariantClear(&v);

                gpus_.push_back(g);
                obj->Release();
            }
            enumerator->Release();
        }

        svc->Release();
        locator->Release();
        if (co_init) CoUninitialize();
    }
};

std::unique_ptr<GPUDetector> create_gpu_detector() {
    return std::make_unique<WindowsGPUDetector>();
}

} // namespace displayswitch

#endif // _WIN32
