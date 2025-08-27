#include "core/OperationsMonitor/NvmlGpuProvider.h"
#include "core/Logging/Logging.h"

namespace ProjectIE4k {

NvmlGpuProvider::~NvmlGpuProvider() {
    if (nvmlShutdown_) {
        nvmlShutdown_();
    }
#ifndef _WIN32
    if (lib_) {
        // Do not dlclose to avoid potential unload hazards during shutdown
        // dlclose(lib_);
    }
#endif
}

bool NvmlGpuProvider::initialize() {
    if (initialized_) return available_;
    initialized_ = true;

#ifdef _WIN32
    const char* candidates[] = { "nvml.dll" };
    for (auto name : candidates) {
        lib_ = LoadLibraryA(name);
        if (lib_) break;
    }
    auto loadSym = [&](const char* sym) -> FARPROC { return lib_ ? GetProcAddress(lib_, sym) : nullptr; };
#else
    const char* candidates[] = { "libnvidia-ml.so.1", "libnvidia-ml.so" };
    for (auto name : candidates) {
        lib_ = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (lib_) {
            Log(DEBUG, "ResourceMonitor", "NVML (provider) loaded from '{}'", name);
            break;
        } else {
            const char* e = dlerror();
            Log(DEBUG, "ResourceMonitor", "NVML (provider) dlopen('{}') failed: {}", name, (e ? e : "(no dlerror)"));
        }
    }
    auto loadSym = [&](const char* sym) -> void* { return lib_ ? dlsym(lib_, sym) : nullptr; };
#endif

    if (!lib_) {
        Log(DEBUG, "ResourceMonitor", "NVML (provider) library unavailable");
        return false;
    }

    nvmlInit_ = reinterpret_cast<PFN_nvmlInit>(loadSym("nvmlInit_v2"));
    if (!nvmlInit_) nvmlInit_ = reinterpret_cast<PFN_nvmlInit>(loadSym("nvmlInit"));
    nvmlShutdown_ = reinterpret_cast<PFN_nvmlShutdown>(loadSym("nvmlShutdown"));
    nvmlErrorString_ = reinterpret_cast<PFN_nvmlErrorString>(loadSym("nvmlErrorString"));
    nvmlDeviceGetHandleByIndex_ = reinterpret_cast<PFN_nvmlDeviceGetHandleByIndex>(loadSym("nvmlDeviceGetHandleByIndex"));
    nvmlDeviceGetUtilizationRates_ = reinterpret_cast<PFN_nvmlDeviceGetUtilizationRates>(loadSym("nvmlDeviceGetUtilizationRates"));
    nvmlDeviceGetMemoryInfo_ = reinterpret_cast<PFN_nvmlDeviceGetMemoryInfo>(loadSym("nvmlDeviceGetMemoryInfo"));

    if (!nvmlInit_ || !nvmlDeviceGetHandleByIndex_ || !nvmlDeviceGetUtilizationRates_ || !nvmlDeviceGetMemoryInfo_) {
        Log(DEBUG, "ResourceMonitor", "NVML (provider) missing symbols: init={}, handle={}, util={}, mem={}",
            (void*)nvmlInit_ != nullptr, (void*)nvmlDeviceGetHandleByIndex_ != nullptr,
            (void*)nvmlDeviceGetUtilizationRates_ != nullptr, (void*)nvmlDeviceGetMemoryInfo_ != nullptr);
        return false;
    }

    int r = nvmlInit_();
    if (r != NVML_SUCCESS_CODE) {
        Log(DEBUG, "ResourceMonitor", "NVML (provider) init failed: code={} msg='{}'", r, err(r));
        return false;
    }
    if (nvmlDeviceGetHandleByIndex_(0, &device_) == NVML_SUCCESS_CODE && device_) {
        available_ = true;
    }
    return available_;
}

bool NvmlGpuProvider::query(double &gpuUsagePercent, uint64_t &totalVRAM, uint64_t &usedVRAM) {
    if (!available_ || !device_) return false;
    nvmlUtilization_t_dyn util{};
    if (nvmlDeviceGetUtilizationRates_(device_, &util) != NVML_SUCCESS_CODE) return false;
    nvmlMemory_t_dyn mem{};
    if (nvmlDeviceGetMemoryInfo_(device_, &mem) != NVML_SUCCESS_CODE) return false;
    gpuUsagePercent = static_cast<double>(util.gpu);
    totalVRAM = mem.total;
    usedVRAM = mem.used;
    return true;
}

} // namespace ProjectIE4k


