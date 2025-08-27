#pragma once

#ifdef _WIN32

#include "core/OperationsMonitor/GpuProvider.h"
#include <wrl/client.h>
#include <dxgi1_6.h>

namespace ProjectIE4k {

class WindowsDxgiGpuProvider : public GpuProvider {
public:
    WindowsDxgiGpuProvider() = default;
    ~WindowsDxgiGpuProvider() override = default;

    bool initialize() override;
    bool isAvailable() const override { return available_; }
    bool query(double &gpuUsagePercent, uint64_t &totalVRAM, uint64_t &usedVRAM) override;

private:
    bool available_ = false;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3_;
};

} // namespace ProjectIE4k

#endif // _WIN32


