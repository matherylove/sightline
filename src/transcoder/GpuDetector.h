#pragma once
// GpuDetector.h
// Detects the primary GPU vendor via DXGI and returns the best
// FFmpeg hardware encoder/decoder codec names to use.
// Priority: NVIDIA > AMD > Intel > software fallback.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi.h>
#include <string>

#pragma comment(lib, "dxgi.lib")

namespace GpuDetector {

enum class Vendor { NVIDIA, AMD, Intel, Unknown };

struct HwProfile {
    Vendor      vendor;
    std::string vendorName;    // Human-readable GPU description

    // Decode (used inside FFmpeg -hwaccel)
    std::string hwaccel;       // e.g. "nvdec", "d3d11va", "dxva2"
    std::string hwaccel_device;// Optional device index ("auto" default)

    // Encode
    std::string encH264;       // e.g. "h264_nvenc", "h264_amf", "h264_qsv", "libx264"
    std::string encH265;       // e.g. "hevc_nvenc", "hevc_amf", "hevc_qsv", "libx265"
    std::string encAAC;        // always "aac" (no HW variant needed)
    std::string encMP3;        // always "libmp3lame"

    bool        hardwareAvailable;
};

inline Vendor DetectVendor(std::string& outDescription) {
    IDXGIFactory* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory)))
        return Vendor::Unknown;

    IDXGIAdapter* pAdapter = nullptr;
    DXGI_ADAPTER_DESC desc = {};
    Vendor vendor = Vendor::Unknown;

    if (SUCCEEDED(pFactory->EnumAdapters(0, &pAdapter))) {
        pAdapter->GetDesc(&desc);
        pAdapter->Release();

        // Convert wide description to narrow
        char buf[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, buf, 255, NULL, NULL);
        outDescription = buf;

        UINT vendorId = desc.VendorId;
        if      (vendorId == 0x10DE) vendor = Vendor::NVIDIA;
        else if (vendorId == 0x1002 || vendorId == 0x1022) vendor = Vendor::AMD;
        else if (vendorId == 0x8086) vendor = Vendor::Intel;
    }
    pFactory->Release();
    return vendor;
}

inline HwProfile Detect() {
    HwProfile p;
    p.encAAC = "aac";
    p.encMP3 = "libmp3lame";
    p.hwaccel_device = "auto";

    p.vendor = DetectVendor(p.vendorName);

    switch (p.vendor) {
    case Vendor::NVIDIA:
        p.hwaccel             = "nvdec";
        p.encH264             = "h264_nvenc";
        p.encH265             = "hevc_nvenc";
        p.hardwareAvailable   = true;
        break;
    case Vendor::AMD:
        p.hwaccel             = "d3d11va";
        p.encH264             = "h264_amf";
        p.encH265             = "hevc_amf";
        p.hardwareAvailable   = true;
        break;
    case Vendor::Intel:
        p.hwaccel             = "qsv";
        p.encH264             = "h264_qsv";
        p.encH265             = "hevc_qsv";
        p.hardwareAvailable   = true;
        break;
    default:
        // Software fallback - always works
        p.hwaccel             = "";
        p.encH264             = "libx264";
        p.encH265             = "libx265";
        p.hardwareAvailable   = false;
        break;
    }
    return p;
}

} // namespace GpuDetector
