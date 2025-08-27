#pragma once

#include "core/OperationsMonitor/GpuProvider.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ProjectIE4k {

class IntelL0GpuProvider : public GpuProvider {
public:
    IntelL0GpuProvider() = default;
    ~IntelL0GpuProvider() override;

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

    // Minimal Level Zero and Sysman dynamic signatures
    using ze_result_t = int;
    static constexpr int ZE_RESULT_SUCCESS = 0;

    using ze_driver_handle_t = void*;
    using ze_device_handle_t = void*;
    using zes_mem_handle_t = void*;

    struct zes_mem_state_t_dyn {
        uint32_t type; // placeholder
        uint32_t reserved; // padding
        uint64_t free;
        uint64_t size;
        uint32_t health; // placeholder
    };

    using PFN_zeInit = ze_result_t (*)(uint32_t);
    using PFN_zeDriverGet = ze_result_t (*)(uint32_t*, ze_driver_handle_t*);
    using PFN_zeDeviceGet = ze_result_t (*)(ze_driver_handle_t, uint32_t*, ze_device_handle_t*);

    using PFN_zesInit = ze_result_t (*)(uint32_t);
    using PFN_zesDeviceEnumMemoryModules = ze_result_t (*)(ze_device_handle_t, uint32_t*, zes_mem_handle_t*);
    using PFN_zesMemoryGetState = ze_result_t (*)(zes_mem_handle_t, zes_mem_state_t_dyn*);

    PFN_zeInit zeInit_ = nullptr;
    PFN_zeDriverGet zeDriverGet_ = nullptr;
    PFN_zeDeviceGet zeDeviceGet_ = nullptr;
    PFN_zesInit zesInit_ = nullptr;
    PFN_zesDeviceEnumMemoryModules zesDeviceEnumMemoryModules_ = nullptr;
    PFN_zesMemoryGetState zesMemoryGetState_ = nullptr;
};

} // namespace ProjectIE4k


