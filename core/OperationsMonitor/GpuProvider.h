#pragma once

#include <cstdint>

namespace ProjectIE4k {

class GpuProvider {
public:
    virtual ~GpuProvider() = default;

    // Initialize underlying API. Returns true if provider is usable.
    virtual bool initialize() = 0;

    // Returns true if initialization succeeded and queries can be made.
    virtual bool isAvailable() const = 0;

    // Query current GPU utilization and VRAM metrics.
    // Returns true on success, false if metrics are unavailable.
    virtual bool query(double &gpuUsagePercent,
                       uint64_t &totalVRAM,
                       uint64_t &usedVRAM) = 0;
};

} // namespace ProjectIE4k


