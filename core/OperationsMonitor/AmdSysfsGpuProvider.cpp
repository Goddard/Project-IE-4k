#include "core/OperationsMonitor/AmdSysfsGpuProvider.h"
#include "core/Logging/Logging.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace ProjectIE4k {

bool AmdSysfsGpuProvider::readUintFromFile(const std::string &path, uint64_t &value) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    unsigned long long v = 0;
    f >> v;
    if (!f.fail()) { value = static_cast<uint64_t>(v); return true; }
    return false;
}

bool AmdSysfsGpuProvider::readUintFromFile(const std::string &path, unsigned int &value) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    unsigned int v = 0;
    f >> v;
    if (!f.fail()) { value = v; return true; }
    return false;
}

bool AmdSysfsGpuProvider::initialize() {
    // Find first amdgpu device
    for (int card = 0; card < 16; ++card) {
        std::string base = "/sys/class/drm/card" + std::to_string(card) + "/device";
        if (!fs::exists(base)) continue;
        // Check vendor id 0x1002 for AMD
        std::ifstream vendorFile(base + "/vendor");
        std::string vendor;
        if (vendorFile.is_open()) {
            vendorFile >> vendor;
            if (vendor == "0x1002") {
                devicePath_ = base;
                break;
            }
        }
    }
    if (devicePath_.empty()) {
        Log(DEBUG, "ResourceMonitor", "AMD sysfs provider: no amdgpu device found");
        return false;
    }
    // Verify VRAM files exist
    if (!fs::exists(devicePath_ + "/mem_info_vram_total") ||
        !fs::exists(devicePath_ + "/mem_info_vram_used")) {
        Log(DEBUG, "ResourceMonitor", "AMD sysfs provider: VRAM files missing in {}", devicePath_);
        return false;
    }
    available_ = true;
    Log(DEBUG, "ResourceMonitor", "AMD sysfs provider initialized: {}", devicePath_);
    return true;
}

bool AmdSysfsGpuProvider::query(double &gpuUsagePercent, uint64_t &totalVRAM, uint64_t &usedVRAM) {
    if (!available_) return false;
    // VRAM metrics in bytes
    if (!readUintFromFile(devicePath_ + "/mem_info_vram_total", totalVRAM)) return false;
    if (!readUintFromFile(devicePath_ + "/mem_info_vram_used", usedVRAM)) return false;

    // GPU usage: optional from amdgpu_pm_info or busy percent when available
    // Try gpu_busy_percent in debugfs if enabled (root typically): /sys/kernel/debug/dri/X/amdgpu_pm_info
    // For unprivileged, leave as 0 if not available
    gpuUsagePercent = 0.0;
    return true;
}

} // namespace ProjectIE4k


