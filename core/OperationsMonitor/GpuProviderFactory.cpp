#include "core/OperationsMonitor/GpuProviderFactory.h"
#include "core/OperationsMonitor/NvmlGpuProvider.h"
#include "core/OperationsMonitor/AmdSysfsGpuProvider.h"
#include "core/OperationsMonitor/IntelL0GpuProvider.h"
#include "core/Logging/Logging.h"

namespace ProjectIE4k {

std::unique_ptr<GpuProvider> GpuProviderFactory::create() {
    // For now only NVML; future: AMD SMI, Intel L0
    auto nv = std::make_unique<NvmlGpuProvider>();
    if (nv->initialize() && nv->isAvailable()) {
        Log(DEBUG, "ResourceMonitor", "GpuProviderFactory selected NVML provider");
        return nv;
    }
    auto amd = std::make_unique<AmdSysfsGpuProvider>();
    if (amd->initialize() && amd->isAvailable()) {
        Log(DEBUG, "ResourceMonitor", "GpuProviderFactory selected AMD sysfs provider");
        return amd;
    }
    auto intel = std::make_unique<IntelL0GpuProvider>();
    if (intel->initialize() && intel->isAvailable()) {
        Log(DEBUG, "ResourceMonitor", "GpuProviderFactory selected Intel Level Zero provider");
        return intel;
    }
    Log(DEBUG, "ResourceMonitor", "GpuProviderFactory: no provider available, falling back to none");
    return nullptr;
}

} // namespace ProjectIE4k


