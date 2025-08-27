#include "core/OperationsMonitor/IntelL0GpuProvider.h"
#include "core/Logging/Logging.h"
#include <vector>

namespace ProjectIE4k {

IntelL0GpuProvider::~IntelL0GpuProvider() {
#ifndef _WIN32
    if (lib_) {
        // Avoid dlclose hazards
        // dlclose(lib_);
    }
#endif
}

bool IntelL0GpuProvider::initialize() {
    if (initialized_) return available_;
    initialized_ = true;

#ifdef _WIN32
    const char* candidates[] = { "ze_loader.dll" };
    for (auto name : candidates) {
        lib_ = LoadLibraryA(name);
        if (lib_) break;
    }
    auto loadSym = [&](const char* sym) -> FARPROC { return lib_ ? GetProcAddress(lib_, sym) : nullptr; };
#else
    const char* candidates[] = { "libze_loader.so.1", "libze_loader.so" };
    for (auto name : candidates) {
        lib_ = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (lib_) {
            Log(DEBUG, "ResourceMonitor", "Intel Level Zero loader found at '{}'", name);
            break;
        } else {
            const char* e = dlerror();
            Log(DEBUG, "ResourceMonitor", "Intel L0 dlopen('{}') failed: {}", name, (e ? e : "(no dlerror)"));
        }
    }
    auto loadSym = [&](const char* sym) -> void* { return lib_ ? dlsym(lib_, sym) : nullptr; };
#endif

    if (!lib_) {
        Log(DEBUG, "ResourceMonitor", "Intel Level Zero loader unavailable");
        return false;
    }

    zeInit_ = reinterpret_cast<PFN_zeInit>(loadSym("zeInit"));
    zeDriverGet_ = reinterpret_cast<PFN_zeDriverGet>(loadSym("zeDriverGet"));
    zeDeviceGet_ = reinterpret_cast<PFN_zeDeviceGet>(loadSym("zeDeviceGet"));
    zesInit_ = reinterpret_cast<PFN_zesInit>(loadSym("zesInit"));
    zesDeviceEnumMemoryModules_ = reinterpret_cast<PFN_zesDeviceEnumMemoryModules>(loadSym("zesDeviceEnumMemoryModules"));
    zesMemoryGetState_ = reinterpret_cast<PFN_zesMemoryGetState>(loadSym("zesMemoryGetState"));

    if (!zeInit_ || !zeDriverGet_ || !zeDeviceGet_ || !zesInit_ || !zesDeviceEnumMemoryModules_ || !zesMemoryGetState_) {
        Log(DEBUG, "ResourceMonitor", "Intel L0 missing symbols");
        return false;
    }

    if (zeInit_(0) != ZE_RESULT_SUCCESS) return false;
    if (zesInit_(0) != ZE_RESULT_SUCCESS) return false;

    // If we got here, provider is usable; actual device/memory queries happen in query()
    available_ = true;
    return true;
}

bool IntelL0GpuProvider::query(double &gpuUsagePercent, uint64_t &totalVRAM, uint64_t &usedVRAM) {
    if (!available_) return false;

    uint32_t driverCount = 0;
    if (zeDriverGet_(&driverCount, nullptr) != ZE_RESULT_SUCCESS || driverCount == 0) return false;
    std::vector<ze_driver_handle_t> drivers(driverCount);
    if (zeDriverGet_(&driverCount, drivers.data()) != ZE_RESULT_SUCCESS) return false;

    for (auto drv : drivers) {
        uint32_t devCount = 0;
        if (zeDeviceGet_(drv, &devCount, nullptr) != ZE_RESULT_SUCCESS || devCount == 0) continue;
        std::vector<ze_device_handle_t> devs(devCount);
        if (zeDeviceGet_(drv, &devCount, devs.data()) != ZE_RESULT_SUCCESS) continue;
        for (auto dev : devs) {
            // Enumerate memory modules and sum
            uint32_t memCount = 0;
            if (zesDeviceEnumMemoryModules_(dev, &memCount, nullptr) != ZE_RESULT_SUCCESS || memCount == 0) continue;
            std::vector<zes_mem_handle_t> mems(memCount);
            if (zesDeviceEnumMemoryModules_(dev, &memCount, mems.data()) != ZE_RESULT_SUCCESS) continue;

            uint64_t total = 0, used = 0;
            for (auto mh : mems) {
                zes_mem_state_t_dyn state{};
                if (zesMemoryGetState_(mh, &state) != ZE_RESULT_SUCCESS) continue;
                total += state.size;
                used += (state.size - state.free);
            }

            if (total > 0) {
                totalVRAM = total;
                usedVRAM = used;
                gpuUsagePercent = 0.0; // Engine utilization requires extra Sysman calls; leave 0 for now
                return true;
            }
        }
    }

    return false;
}

} // namespace ProjectIE4k


