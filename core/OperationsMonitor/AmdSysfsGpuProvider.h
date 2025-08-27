#pragma once

#include "core/OperationsMonitor/GpuProvider.h"
#include <string>

namespace ProjectIE4k {

class AmdSysfsGpuProvider : public GpuProvider {
public:
    AmdSysfsGpuProvider() = default;
    ~AmdSysfsGpuProvider() override = default;

    bool initialize() override;
    bool isAvailable() const override { return available_; }

    bool query(double &gpuUsagePercent, uint64_t &totalVRAM, uint64_t &usedVRAM) override;

private:
    bool available_ = false;
    std::string devicePath_; // e.g., /sys/class/drm/card0/device

    static bool readUintFromFile(const std::string &path, uint64_t &value);
    static bool readUintFromFile(const std::string &path, unsigned int &value);
};

} // namespace ProjectIE4k


