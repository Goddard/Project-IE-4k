#ifdef _WIN32

#include "core/OperationsMonitor/WindowsDxgiGpuProvider.h"
#include "core/Logging/Logging.h"

namespace ProjectIE4k {

bool WindowsDxgiGpuProvider::initialize() {
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        Log(DEBUG, "ResourceMonitor", "DXGI: failed to create factory");
        return false;
    }

    for (UINT i = 0; ; ++i) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
        if (factory->EnumAdapters1(i, &adapter1) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        adapter1->GetDesc1(&desc);
        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        Microsoft::WRL::ComPtr<IDXGIAdapter3> a3;
        if (SUCCEEDED(adapter1.As(&a3))) {
            adapter3_ = a3;
            available_ = true;
            Log(DEBUG, "ResourceMonitor", "DXGI: selected adapter: {}", (int)desc.AdapterLuid.LowPart);
            break;
        }
    }
    return available_;
}

bool WindowsDxgiGpuProvider::query(double &gpuUsagePercent, uint64_t &totalVRAM, uint64_t &usedVRAM) {
    if (!available_ || !adapter3_) return false;
    // VRAM budget info
    DXGI_QUERY_VIDEO_MEMORY_INFO local{};
    if (FAILED(adapter3_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local))) {
        return false;
    }
    totalVRAM = local.Budget; // approximate total available to this process
    usedVRAM = local.CurrentUsage;
    gpuUsagePercent = 0.0; // Without GPU counters; can be extended via D3D12 pipeline stats
    return true;
}

} // namespace ProjectIE4k

#endif // _WIN32


