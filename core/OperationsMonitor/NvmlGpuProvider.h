#pragma once

#include "core/OperationsMonitor/GpuProvider.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ProjectIE4k {

class NvmlGpuProvider : public GpuProvider {
public:
    NvmlGpuProvider() = default;
    ~NvmlGpuProvider() override;

    bool initialize() override;
    bool isAvailable() const override { return available_; }

    bool query(double &gpuUsagePercent, uint64_t &totalVRAM, uint64_t &usedVRAM) override;

private:
    bool available_ = false;
    bool initialized_ = false;

#ifdef _WIN32
    HMODULE lib_ = nullptr;
#else
    void* lib_ = nullptr;
#endif

    // Minimal NVML dynamic signatures
    using nvmlDevice_t_dyn = void*;
    struct nvmlUtilization_t_dyn { unsigned int gpu; unsigned int memory; };
    struct nvmlMemory_t_dyn { unsigned long long total; unsigned long long free; unsigned long long used; };

    using PFN_nvmlInit = int (*)();
    using PFN_nvmlShutdown = int (*)();
    using PFN_nvmlErrorString = const char* (*)(int);
    using PFN_nvmlDeviceGetHandleByIndex = int (*)(unsigned int, nvmlDevice_t_dyn*);
    using PFN_nvmlDeviceGetUtilizationRates = int (*)(nvmlDevice_t_dyn, nvmlUtilization_t_dyn*);
    using PFN_nvmlDeviceGetMemoryInfo = int (*)(nvmlDevice_t_dyn, nvmlMemory_t_dyn*);

    static constexpr int NVML_SUCCESS_CODE = 0;

    nvmlDevice_t_dyn device_ = nullptr;
    PFN_nvmlInit nvmlInit_ = nullptr;
    PFN_nvmlShutdown nvmlShutdown_ = nullptr;
    PFN_nvmlErrorString nvmlErrorString_ = nullptr;
    PFN_nvmlDeviceGetHandleByIndex nvmlDeviceGetHandleByIndex_ = nullptr;
    PFN_nvmlDeviceGetUtilizationRates nvmlDeviceGetUtilizationRates_ = nullptr;
    PFN_nvmlDeviceGetMemoryInfo nvmlDeviceGetMemoryInfo_ = nullptr;

    const char* err(int code) const { return nvmlErrorString_ ? nvmlErrorString_(code) : "NVML error"; }
};

} // namespace ProjectIE4k


