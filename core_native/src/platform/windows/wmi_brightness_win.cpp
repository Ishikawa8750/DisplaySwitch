/**
 * DisplaySwitch Native — WMI Brightness Control (Windows)
 *
 * Uses COM + WbemScripting to query WmiMonitorBrightness and invoke
 * WmiMonitorBrightnessMethods::WmiSetBrightness on internal displays.
 *
 * WMI namespace: root\wmi
 * Classes:
 *   - WmiMonitorBrightness       → CurrentBrightness (read-only property)
 *   - WmiMonitorBrightnessMethods → WmiSetBrightness(Timeout, Brightness)
 */

#ifdef _WIN32

#include "displayswitch/wmi_brightness.h"

#include <Windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <iostream>

#pragma comment(lib, "wbemuuid.lib")

namespace displayswitch {

// ─── COM helper: auto-release ───────────────────────────────────────────────

struct ComInit {
    bool owns;
    ComInit() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owns = SUCCEEDED(hr);
        // S_FALSE means already initialised — that's fine, don't uninit later
        if (hr == S_FALSE) owns = false;
    }
    ~ComInit() { if (owns) CoUninitialize(); }
};

// ─── Internal: connect to WMI root\wmi ──────────────────────────────────────

static IWbemServices* connect_wmi() {
    IWbemLocator* pLoc = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, reinterpret_cast<void**>(&pLoc));
    if (FAILED(hr) || !pLoc) return nullptr;

    IWbemServices* pSvc = nullptr;
    hr = pLoc->ConnectServer(
        _bstr_t(L"root\\wmi"), nullptr, nullptr, nullptr,
        0, nullptr, nullptr, &pSvc);
    pLoc->Release();
    if (FAILED(hr) || !pSvc) return nullptr;

    // Set proxy blanket for local calls
    CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                      nullptr, EOAC_NONE);

    return pSvc;
}

// ─── Public API ─────────────────────────────────────────────────────────────

bool wmi_brightness_available() {
    ComInit com;
    IWbemServices* pSvc = connect_wmi();
    if (!pSvc) return false;

    IEnumWbemClassObject* pEnum = nullptr;
    HRESULT hr = pSvc->ExecQuery(
        _bstr_t("WQL"),
        _bstr_t("SELECT CurrentBrightness FROM WmiMonitorBrightness"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &pEnum);

    bool available = false;
    if (SUCCEEDED(hr) && pEnum) {
        IWbemClassObject* pObj = nullptr;
        ULONG ret = 0;
        if (pEnum->Next(WBEM_INFINITE, 1, &pObj, &ret) == S_OK && ret > 0) {
            available = true;
            pObj->Release();
        }
        pEnum->Release();
    }

    pSvc->Release();
    return available;
}

int wmi_get_brightness() {
    ComInit com;
    IWbemServices* pSvc = connect_wmi();
    if (!pSvc) return -1;

    IEnumWbemClassObject* pEnum = nullptr;
    HRESULT hr = pSvc->ExecQuery(
        _bstr_t("WQL"),
        _bstr_t("SELECT CurrentBrightness FROM WmiMonitorBrightness"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &pEnum);

    int brightness = -1;
    if (SUCCEEDED(hr) && pEnum) {
        IWbemClassObject* pObj = nullptr;
        ULONG ret = 0;
        if (pEnum->Next(WBEM_INFINITE, 1, &pObj, &ret) == S_OK && ret > 0) {
            VARIANT val;
            VariantInit(&val);
            hr = pObj->Get(L"CurrentBrightness", 0, &val, nullptr, nullptr);
            if (SUCCEEDED(hr) && val.vt == VT_UI1) {
                brightness = static_cast<int>(val.bVal);
            } else if (SUCCEEDED(hr) && val.vt == VT_I4) {
                brightness = static_cast<int>(val.lVal);
            }
            VariantClear(&val);
            pObj->Release();
        }
        pEnum->Release();
    }

    pSvc->Release();
    return brightness;
}

bool wmi_set_brightness(int level) {
    if (level < 0) level = 0;
    if (level > 100) level = 100;

    ComInit com;
    IWbemServices* pSvc = connect_wmi();
    if (!pSvc) return false;

    // 1. Get the class definition for WmiMonitorBrightnessMethods
    IWbemClassObject* pClass = nullptr;
    HRESULT hr = pSvc->GetObject(
        _bstr_t("WmiMonitorBrightnessMethods"), 0, nullptr, &pClass, nullptr);
    if (FAILED(hr) || !pClass) {
        pSvc->Release();
        return false;
    }

    // 2. Get the method parameter definition (WmiSetBrightness)
    IWbemClassObject* pInParamsClass = nullptr;
    hr = pClass->GetMethod(L"WmiSetBrightness", 0, &pInParamsClass, nullptr);
    pClass->Release();
    if (FAILED(hr) || !pInParamsClass) {
        pSvc->Release();
        return false;
    }

    // 3. Spawn an instance of the input parameters
    IWbemClassObject* pInParams = nullptr;
    hr = pInParamsClass->SpawnInstance(0, &pInParams);
    pInParamsClass->Release();
    if (FAILED(hr) || !pInParams) {
        pSvc->Release();
        return false;
    }

    // 4. Set parameters: Timeout = 0, Brightness = level
    VARIANT vTimeout;
    VariantInit(&vTimeout);
    vTimeout.vt = VT_I4;
    vTimeout.lVal = 0;  // No fade transition
    pInParams->Put(L"Timeout", 0, &vTimeout, 0);
    VariantClear(&vTimeout);

    VARIANT vBrightness;
    VariantInit(&vBrightness);
    vBrightness.vt = VT_UI1;
    vBrightness.bVal = static_cast<BYTE>(level);
    pInParams->Put(L"Brightness", 0, &vBrightness, 0);
    VariantClear(&vBrightness);

    // 5. Find the first WmiMonitorBrightnessMethods instance path
    IEnumWbemClassObject* pEnum = nullptr;
    hr = pSvc->ExecQuery(
        _bstr_t("WQL"),
        _bstr_t("SELECT * FROM WmiMonitorBrightnessMethods"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &pEnum);

    bool success = false;
    if (SUCCEEDED(hr) && pEnum) {
        IWbemClassObject* pObj = nullptr;
        ULONG ret = 0;
        if (pEnum->Next(WBEM_INFINITE, 1, &pObj, &ret) == S_OK && ret > 0) {
            // Get the object path (e.g. WmiMonitorBrightnessMethods.InstanceName="...")
            VARIANT vPath;
            VariantInit(&vPath);
            pObj->Get(L"__PATH", 0, &vPath, nullptr, nullptr);

            if (vPath.vt == VT_BSTR) {
                IWbemClassObject* pOutParams = nullptr;
                hr = pSvc->ExecMethod(
                    vPath.bstrVal,
                    _bstr_t("WmiSetBrightness"),
                    0, nullptr, pInParams, &pOutParams, nullptr);

                if (SUCCEEDED(hr)) success = true;
                if (pOutParams) pOutParams->Release();
            }
            VariantClear(&vPath);
            pObj->Release();
        }
        pEnum->Release();
    }

    pInParams->Release();
    pSvc->Release();
    return success;
}

} // namespace displayswitch

#endif // _WIN32
