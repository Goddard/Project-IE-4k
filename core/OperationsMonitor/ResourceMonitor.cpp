#include "ResourceMonitor.h"
#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>

#ifdef __linux__
    #include <sys/sysinfo.h>
    #include <sys/stat.h>
    #include <sys/sysmacros.h>
    #include <unistd.h>
#endif

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace ProjectIE4k {

namespace {
// Minimal NVML API shims for runtime dynamic loading
// We avoid requiring nvml.h at build time and do not link against NVML.

// Basic NVML status success code
constexpr int NVML_SUCCESS_CODE = 0;

// Mirror essential NVML structs and opaque handle
using nvmlDevice_t_dyn = void*;
struct nvmlUtilization_t_dyn { unsigned int gpu; unsigned int memory; };
struct nvmlMemory_t_dyn { unsigned long long total; unsigned long long free; unsigned long long used; };

// Function pointer types
using PFN_nvmlInit = int (*)();
using PFN_nvmlShutdown = int (*)();
using PFN_nvmlErrorString = const char* (*)(int);
using PFN_nvmlDeviceGetHandleByIndex = int (*)(unsigned int, nvmlDevice_t_dyn*);
using PFN_nvmlDeviceGetUtilizationRates = int (*)(nvmlDevice_t_dyn, nvmlUtilization_t_dyn*);
using PFN_nvmlDeviceGetMemoryInfo = int (*)(nvmlDevice_t_dyn, nvmlMemory_t_dyn*);

struct NVMLApi {
    bool initialized = false;
    bool available = false;
    nvmlDevice_t_dyn device = nullptr;

#ifdef _WIN32
    HMODULE lib = nullptr;
#else
    void* lib = nullptr;
#endif

    PFN_nvmlInit nvmlInit = nullptr;           // try _v2 first then fallback
    PFN_nvmlShutdown nvmlShutdown = nullptr;
    PFN_nvmlErrorString nvmlErrorString = nullptr;
    PFN_nvmlDeviceGetHandleByIndex nvmlDeviceGetHandleByIndex = nullptr;
    PFN_nvmlDeviceGetUtilizationRates nvmlDeviceGetUtilizationRates = nullptr;
    PFN_nvmlDeviceGetMemoryInfo nvmlDeviceGetMemoryInfo = nullptr;

    const char* err(int code) const {
        return nvmlErrorString ? nvmlErrorString(code) : "NVML error";
    }
};

NVMLApi& getNVML() {
    static NVMLApi api;
    if (api.initialized) return api;
    api.initialized = true;

    // Load library
#ifdef _WIN32
    const char* candidates[] = { "nvml.dll" };
    for (auto name : candidates) {
        api.lib = LoadLibraryA(name);
        if (api.lib) break;
    }
    auto loadSym = [&](const char* sym) -> FARPROC { return api.lib ? GetProcAddress(api.lib, sym) : nullptr; };
#else
    const char* candidates[] = { "libnvidia-ml.so.1", "libnvidia-ml.so" };
    for (auto name : candidates) {
        api.lib = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (api.lib) break;
    }
    auto loadSym = [&](const char* sym) -> void* { return api.lib ? dlsym(api.lib, sym) : nullptr; };
#endif

    if (!api.lib) {
        return api; // unavailable
    }

    // Resolve functions (init v2 or init)
#ifdef _WIN32
    api.nvmlInit = reinterpret_cast<PFN_nvmlInit>(loadSym("nvmlInit_v2"));
    if (!api.nvmlInit) api.nvmlInit = reinterpret_cast<PFN_nvmlInit>(loadSym("nvmlInit"));
    api.nvmlShutdown = reinterpret_cast<PFN_nvmlShutdown>(loadSym("nvmlShutdown"));
    api.nvmlErrorString = reinterpret_cast<PFN_nvmlErrorString>(loadSym("nvmlErrorString"));
    api.nvmlDeviceGetHandleByIndex = reinterpret_cast<PFN_nvmlDeviceGetHandleByIndex>(loadSym("nvmlDeviceGetHandleByIndex"));
    api.nvmlDeviceGetUtilizationRates = reinterpret_cast<PFN_nvmlDeviceGetUtilizationRates>(loadSym("nvmlDeviceGetUtilizationRates"));
    api.nvmlDeviceGetMemoryInfo = reinterpret_cast<PFN_nvmlDeviceGetMemoryInfo>(loadSym("nvmlDeviceGetMemoryInfo"));
#else
    api.nvmlInit = reinterpret_cast<PFN_nvmlInit>(loadSym("nvmlInit_v2"));
    if (!api.nvmlInit) api.nvmlInit = reinterpret_cast<PFN_nvmlInit>(loadSym("nvmlInit"));
    api.nvmlShutdown = reinterpret_cast<PFN_nvmlShutdown>(loadSym("nvmlShutdown"));
    api.nvmlErrorString = reinterpret_cast<PFN_nvmlErrorString>(loadSym("nvmlErrorString"));
    api.nvmlDeviceGetHandleByIndex = reinterpret_cast<PFN_nvmlDeviceGetHandleByIndex>(loadSym("nvmlDeviceGetHandleByIndex"));
    api.nvmlDeviceGetUtilizationRates = reinterpret_cast<PFN_nvmlDeviceGetUtilizationRates>(loadSym("nvmlDeviceGetUtilizationRates"));
    api.nvmlDeviceGetMemoryInfo = reinterpret_cast<PFN_nvmlDeviceGetMemoryInfo>(loadSym("nvmlDeviceGetMemoryInfo"));
#endif

    if (!api.nvmlInit || !api.nvmlDeviceGetHandleByIndex ||
        !api.nvmlDeviceGetUtilizationRates || !api.nvmlDeviceGetMemoryInfo) {
        return api; // missing symbols
    }

    // Initialize and get primary device 0
    int r = api.nvmlInit();
    if (r != NVML_SUCCESS_CODE) {
        return api;
    }
    if (api.nvmlDeviceGetHandleByIndex(0, &api.device) == NVML_SUCCESS_CODE && api.device) {
        api.available = true;
    }
    return api;
}
} // namespace

ResourceMonitor::ResourceMonitor() {
    Log(DEBUG, "ResourceMonitor", "ResourceMonitor created");
}

ResourceMonitor::~ResourceMonitor() {
    shutdown();
}

void ResourceMonitor::initialize() {
    if (initialized_) return;
    
    Log(MESSAGE, "ResourceMonitor", "Initializing ResourceMonitor");
    
    // Initialize metrics with default values
    currentMetrics_ = SystemMetrics{};
    previousMetrics_ = SystemMetrics{};
    lastUpdate_ = std::chrono::steady_clock::now();

    // Start metrics collection thread
    // metricsThread_ = std::thread(&ResourceMonitor::metricsUpdateLoop, this);

    initialized_ = true;
    Log(MESSAGE, "ResourceMonitor", "ResourceMonitor initialized successfully");
}

void ResourceMonitor::shutdown() {
    if (!initialized_ || shutdown_) return;
    
    Log(MESSAGE, "ResourceMonitor", "Shutting down ResourceMonitor");
    
    shutdown_ = true;
    
    if (metricsThread_.joinable()) {
        metricsThread_.join();
    }
    
    Log(MESSAGE, "ResourceMonitor", "ResourceMonitor shutdown complete");
}

SystemMetrics ResourceMonitor::getCurrentMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return currentMetrics_;
}

SystemMetrics ResourceMonitor::getFreshMetrics() {
    // Force immediate update
    SystemMetrics metrics;
    updateCPUMetrics(metrics);
    updateMemoryMetrics(metrics);
    updateGPUMetrics(metrics);
    updateDiskIOMetrics(metrics);
    validateMetrics(metrics);
    
    metrics.timestamp = std::chrono::steady_clock::now();
    metrics.valid = true;
    
    // Update cached metrics
    {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        previousMetrics_ = currentMetrics_;
        currentMetrics_ = metrics;
        lastUpdate_ = metrics.timestamp;
    }
    
    // Trigger callback if set
    {
        std::lock_guard<std::mutex> callbackLock(callbackMutex_);
        if (metricsCallback_) {
            metricsCallback_(metrics);
        }
    }
    
    return metrics;
}

void ResourceMonitor::setUpdateInterval(std::chrono::milliseconds interval) {
    updateInterval_ = interval;
    Log(DEBUG, "ResourceMonitor", "Update interval set to {}ms", interval.count());
}

void ResourceMonitor::setMetricsCallback(MetricsCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    metricsCallback_ = callback;
}

bool ResourceMonitor::isVRAMCritical() const {
    return vramCritical_.load();
}

bool ResourceMonitor::isVRAMDangerous() const {
    return vramDangerous_.load();
}

void ResourceMonitor::metricsUpdateLoop() {
    Log(DEBUG, "ResourceMonitor", "Metrics update loop started");
    
    while (!shutdown_) {
        auto startTime = std::chrono::steady_clock::now();
        
        SystemMetrics metrics;
        updateCPUMetrics(metrics);
        updateMemoryMetrics(metrics);
        updateGPUMetrics(metrics);
        updateDiskIOMetrics(metrics);
        validateMetrics(metrics);
        
        metrics.timestamp = startTime;
        metrics.valid = true;
        
        // Update VRAM emergency flags
        if (metrics.totalVRAM > 0) {
            double vramUsagePercent = (static_cast<double>(metrics.usedVRAM) / static_cast<double>(metrics.totalVRAM)) * 100.0;
            vramCritical_ = (vramUsagePercent > 90.0);
            vramDangerous_ = (vramUsagePercent > 95.0);
        }
        
        // Update cached metrics
        {
            std::lock_guard<std::mutex> lock(metricsMutex_);
            previousMetrics_ = currentMetrics_;
            currentMetrics_ = metrics;
            lastUpdate_ = startTime;
        }
        
        // Trigger callback if set
        {
            std::lock_guard<std::mutex> callbackLock(callbackMutex_);
            if (metricsCallback_) {
                metricsCallback_(metrics);
            }
        }
        
        // Sleep for the remaining interval
        auto endTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        auto sleepTime = updateInterval_ - elapsed;
        
        if (sleepTime > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(sleepTime);
        }
    }
    
    Log(DEBUG, "ResourceMonitor", "Metrics update loop ended");
}

void ResourceMonitor::updateCPUMetrics(SystemMetrics& metrics) {
#ifdef __linux__
    // Read CPU usage from /proc/stat
    std::ifstream file("/proc/stat");
    if (!file.is_open()) {
        Log(WARNING, "ResourceMonitor", "Failed to open /proc/stat for CPU metrics");
        return;
    }
    
    std::string line;
    if (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string cpu;
        long user, nice, system, idle, iowait, irq, softirq, steal;
        
        if (iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) {
            long totalIdle = idle + iowait;
            long totalNonIdle = user + nice + system + irq + softirq + steal;
            long total = totalIdle + totalNonIdle;
            
            static long prevTotal = 0;
            static long prevIdle = 0;
            
            if (prevTotal != 0) {
                long totalDiff = total - prevTotal;
                long idleDiff = totalIdle - prevIdle;
                
                if (totalDiff > 0) {
                    metrics.cpuUsagePercent = ((double)(totalDiff - idleDiff) / totalDiff) * 100.0;
                }
            }
            
            prevTotal = total;
            prevIdle = totalIdle;
        }
    }
    
    // Get CPU core count
    metrics.cpuCoreCount = std::thread::hardware_concurrency();
    if (metrics.cpuCoreCount == 0) {
        metrics.cpuCoreCount = 4; // Fallback
    }
    
    // Calculate available cores based on CPU usage
    double usageRatio = metrics.cpuUsagePercent / 100.0;
    metrics.availableCores = static_cast<int>(metrics.cpuCoreCount * (1.0 - usageRatio));
    metrics.availableCores = std::max(1, metrics.availableCores); // Always at least 1 core available
#endif
}

void ResourceMonitor::updateMemoryMetrics(SystemMetrics& metrics) {
#ifdef __linux__
    std::ifstream file("/proc/meminfo");
    if (!file.is_open()) {
        Log(WARNING, "ResourceMonitor", "Failed to open /proc/meminfo for memory metrics");
        return;
    }
    
    std::string line;
    uint64_t memTotal = 0, memFree = 0, memAvailable = 0, buffers = 0, cached = 0;
    
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        uint64_t value;
        std::string unit;
        
        if (iss >> key >> value >> unit) {
            value *= 1024; // Convert from KB to bytes
            
            if (key == "MemTotal:") {
                memTotal = value;
            } else if (key == "MemFree:") {
                memFree = value;
            } else if (key == "MemAvailable:") {
                memAvailable = value;
            } else if (key == "Buffers:") {
                buffers = value;
            } else if (key == "Cached:") {
                cached = value;
            }
        }
    }
    
    metrics.totalRAM = memTotal;
    metrics.availableRAM = memAvailable > 0 ? memAvailable : (memFree + buffers + cached);
    metrics.usedRAM = memTotal - metrics.availableRAM;
    
    if (memTotal > 0) {
        metrics.ramUsagePercent = (static_cast<double>(metrics.usedRAM) / static_cast<double>(memTotal)) * 100.0;
    }
#endif
}

void ResourceMonitor::updateGPUMetrics(SystemMetrics& metrics) {
    // Try runtime NVML if available; gracefully degrade otherwise
    NVMLApi& nvml = getNVML();
    if (nvml.available && nvml.device) {
        nvmlUtilization_t_dyn utilization{};
        if (nvml.nvmlDeviceGetUtilizationRates(nvml.device, &utilization) == NVML_SUCCESS_CODE) {
            metrics.gpuUsagePercent = static_cast<double>(utilization.gpu);
        }

        nvmlMemory_t_dyn memoryInfo{};
        if (nvml.nvmlDeviceGetMemoryInfo(nvml.device, &memoryInfo) == NVML_SUCCESS_CODE) {
            metrics.totalVRAM = memoryInfo.total;
            metrics.usedVRAM = memoryInfo.used;
            metrics.availableVRAM = memoryInfo.free;
            if (memoryInfo.total > 0) {
                metrics.vramUsagePercent = (static_cast<double>(memoryInfo.used) / static_cast<double>(memoryInfo.total)) * 100.0;
            }
        }

        if (verbose) {
            Log(DEBUG, "ResourceMonitor", "GPU: {}% usage, VRAM: {:.1f}% ({:.1f}GB / {:.1f}GB)",
                static_cast<int>(metrics.gpuUsagePercent),
                metrics.vramUsagePercent,
                static_cast<double>(metrics.usedVRAM) / (1024.0 * 1024.0 * 1024.0),
                static_cast<double>(metrics.totalVRAM) / (1024.0 * 1024.0 * 1024.0));
        }
        return;
    }

    // Fallback when NVML is unavailable
    metrics.gpuUsagePercent = 0.0;
    metrics.totalVRAM = 0;
    metrics.usedVRAM = 0;
    metrics.availableVRAM = 0;
    metrics.vramUsagePercent = 0.0;
}

void ResourceMonitor::updateDiskIOMetrics(SystemMetrics& metrics) {
#ifdef __linux__
    // Read disk I/O statistics from /proc/diskstats
    std::ifstream file("/proc/diskstats");
    if (!file.is_open()) {
        Log(WARNING, "ResourceMonitor", "Failed to open /proc/diskstats for disk I/O metrics");
        return;
    }
    
    std::string outputDevice = getOutputDiskDevice();
    std::string gameDevice = getGameDiskDevice();
    
    std::string line;
    uint64_t totalReadBytes = 0, totalWriteBytes = 0, totalReadOps = 0, totalWriteOps = 0;
    uint64_t gameReadBytes = 0, gameReadOps = 0;
    
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        int major, minor;
        std::string deviceName;
        uint64_t readOps, readMerges, readSectors, readTicks;
        uint64_t writeOps, writeMerges, writeSectors, writeTicks;
        uint64_t ioInFlight, ioTicks, ioTimeWeighted;
        
        if (iss >> major >> minor >> deviceName >> readOps >> readMerges >> readSectors >> readTicks
               >> writeOps >> writeMerges >> writeSectors >> writeTicks >> ioInFlight >> ioTicks >> ioTimeWeighted) {
            
            // Convert sectors to bytes (assuming 512 bytes per sector)
            uint64_t readBytes = readSectors * 512;
            uint64_t writeBytes = writeSectors * 512;
            
            // Accumulate for all devices
            totalReadBytes += readBytes;
            totalWriteBytes += writeBytes;
            totalReadOps += readOps;
            totalWriteOps += writeOps;
            
            // Track game disk separately
            if (deviceName == gameDevice) {
                gameReadBytes += readBytes;
                gameReadOps += readOps;
            }
        }
    }
    
    // Calculate per-second rates (simple difference from last measurement)
    static uint64_t prevTotalReadBytes = 0, prevTotalWriteBytes = 0;
    static uint64_t prevTotalReadOps = 0, prevTotalWriteOps = 0;
    static uint64_t prevGameReadBytes = 0, prevGameReadOps = 0;
    static auto prevTime = std::chrono::steady_clock::now();
    
    auto currentTime = std::chrono::steady_clock::now();
    auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - prevTime);
    
    if (timeDiff.count() > 0 && prevTotalReadBytes > 0) {
        double seconds = timeDiff.count() / 1000.0;
        
        metrics.diskReadBytesPerSec = static_cast<uint64_t>((totalReadBytes - prevTotalReadBytes) / seconds);
        metrics.diskWriteBytesPerSec = static_cast<uint64_t>((totalWriteBytes - prevTotalWriteBytes) / seconds);
        metrics.diskReadOpsPerSec = static_cast<uint64_t>((totalReadOps - prevTotalReadOps) / seconds);
        metrics.diskWriteOpsPerSec = static_cast<uint64_t>((totalWriteOps - prevTotalWriteOps) / seconds);
        
        metrics.gameDiskReadBytesPerSec = static_cast<uint64_t>((gameReadBytes - prevGameReadBytes) / seconds);
        metrics.gameDiskReadOpsPerSec = static_cast<uint64_t>((gameReadOps - prevGameReadOps) / seconds);
    }
    
    prevTotalReadBytes = totalReadBytes;
    prevTotalWriteBytes = totalWriteBytes;
    prevTotalReadOps = totalReadOps;
    prevTotalWriteOps = totalWriteOps;
    prevGameReadBytes = gameReadBytes;
    prevGameReadOps = gameReadOps;
    prevTime = currentTime;
#endif
}

void ResourceMonitor::validateMetrics(SystemMetrics& metrics) {
    // Validate VRAM metrics to prevent invalid data
    if (metrics.totalVRAM > 0) {
        // Ensure VRAM values are consistent
        if (metrics.usedVRAM > metrics.totalVRAM) {
            Log(WARNING, "ResourceMonitor", "Invalid VRAM metrics detected: used ({}) > total ({}). Correcting.", 
                metrics.usedVRAM, metrics.totalVRAM);
            metrics.usedVRAM = metrics.totalVRAM;
            metrics.availableVRAM = 0;
        } else {
            // Calculate truly available VRAM for operations
            // Account for baseline system usage (typically 15-20% of total VRAM)
            // and add safety margin to prevent overcommitment
            
            uint64_t baselineVRAM = static_cast<uint64_t>(metrics.totalVRAM * 0.20); // Reserve 20% for system baseline
            uint64_t safetyMargin = static_cast<uint64_t>(metrics.totalVRAM * 0.10);  // Additional 10% safety margin
            uint64_t reservedVRAM = baselineVRAM + safetyMargin;
            
            if (metrics.usedVRAM + reservedVRAM < metrics.totalVRAM) {
                metrics.availableVRAM = metrics.totalVRAM - metrics.usedVRAM - reservedVRAM;
            } else {
                metrics.availableVRAM = 0; // Not enough VRAM available for operations
            }
            
            if(verbose) {
                Log(DEBUG, "ResourceMonitor", "VRAM calculation: total={}GB, used={}GB, baseline={}GB, safety={}GB, available={}GB",
                    metrics.totalVRAM / (1024.0*1024*1024),
                    metrics.usedVRAM / (1024.0*1024*1024),
                    baselineVRAM / (1024.0*1024*1024),
                    safetyMargin / (1024.0*1024*1024),
                    metrics.availableVRAM / (1024.0*1024*1024));
            }
        }
        metrics.vramUsagePercent = (static_cast<double>(metrics.usedVRAM) / static_cast<double>(metrics.totalVRAM)) * 100.0;
    }
    
    // Validate RAM metrics
    if (metrics.totalRAM > 0) {
        if (metrics.usedRAM > metrics.totalRAM) {
            Log(WARNING, "ResourceMonitor", "Invalid RAM metrics detected: used ({}) > total ({}). Correcting.", 
                metrics.usedRAM, metrics.totalRAM);
            metrics.usedRAM = metrics.totalRAM;
            metrics.availableRAM = 0;
        }
        // Recalculate percentage to ensure consistency
        metrics.ramUsagePercent = (static_cast<double>(metrics.usedRAM) / static_cast<double>(metrics.totalRAM)) * 100.0;
    }
    
    // Validate CPU metrics
    metrics.cpuUsagePercent = std::clamp(metrics.cpuUsagePercent, 0.0, 100.0);
    // Ensure cpuCoreCount is at least 1 to avoid invalid clamp range
    int cpuCoresSafe = std::max(1, metrics.cpuCoreCount);
    int availableCoresSafe = std::clamp(metrics.availableCores, 1, cpuCoresSafe);
    metrics.availableCores = availableCoresSafe;
}

std::string ResourceMonitor::getDiskDeviceForPath(const std::string& path) const {
//     #ifdef __linux__
//     try {
//         std::filesystem::path fsPath(path);
//         if (!std::filesystem::exists(fsPath)) {
//             Log(WARNING, "OperationsMonitor", "Path does not exist: {}", path);
//             return "";
//         }
        
//         // Get the canonical path to resolve symlinks
//         std::filesystem::path canonicalPath = std::filesystem::canonical(fsPath);
        
//         // Find the mount point for this path
//         std::string mountPoint = "";
//         std::string devicePath = "";
        
//         // Read /proc/mounts to find the mount point
//         std::ifstream mountsFile("/proc/mounts");
//         if (!mountsFile.is_open()) {
//             Log(WARNING, "OperationsMonitor", "Failed to open /proc/mounts");
//             return "";
//         }
        
//         std::string line;
//         while (std::getline(mountsFile, line)) {
//             std::istringstream iss(line);
//             std::string device, mount, fsType, options;
            
//             iss >> device >> mount >> fsType >> options;
            
//             // Skip non-physical devices
//             if (device.find("/dev/") != 0) {
//                 continue;
//             }
            
//             // Check if this mount point is a prefix of our path
//             if (canonicalPath.string().find(mount) == 0) {
//                 // Found a match, but we want the longest matching mount point
//                 if (mount.length() > mountPoint.length()) {
//                     mountPoint = mount;
//                     devicePath = device;
//                 }
//             }
//         }
        
//         if (mountPoint.empty()) {
//             Log(WARNING, "OperationsMonitor", "Could not find mount point for path: {}", path);
//             return "";
//         }
        
//         Log(DEBUG, "OperationsMonitor", "Path '{}' is mounted at '{}' on device '{}'", 
//             path, mountPoint, devicePath);
        
//         // Extract device name from device path (e.g., "/dev/nvme1n1p2" -> "nvme1n1p2")
//         std::string deviceName = devicePath.substr(devicePath.find_last_of('/') + 1);
        
//         // Read /proc/partitions to get major/minor numbers for the device
//         std::ifstream partitionsFile("/proc/partitions");
//         if (!partitionsFile.is_open()) {
//             Log(WARNING, "OperationsMonitor", "Failed to open /proc/partitions");
//             return "";
//         }
        
//         // Skip header lines
//         std::getline(partitionsFile, line); // "major minor  #blocks  name"
//         std::getline(partitionsFile, line); // empty line
        
//         while (std::getline(partitionsFile, line)) {
//             std::istringstream iss(line);
//             uint64_t major, minor, blocks;
//             std::string name;
            
//             iss >> major >> minor >> blocks >> name;
            
//             if (name == deviceName) {
//                 Log(DEBUG, "OperationsMonitor", "Found device '{}' in /proc/partitions: major={}, minor={}", 
//                     deviceName, major, minor);
                
//                 // Now find the corresponding device in /proc/diskstats
//                 std::ifstream diskStatsFile("/proc/diskstats");
//                 if (!diskStatsFile.is_open()) {
//                     Log(WARNING, "OperationsMonitor", "Failed to open /proc/diskstats");
//                     return "";
//                 }
                
//                 while (std::getline(diskStatsFile, line)) {
//                     std::istringstream diskIss(line);
//                     uint64_t devMajor, devMinor;
//                     std::string diskDevice;
                    
//                     diskIss >> devMajor >> devMinor >> diskDevice;
                    
//                     // Skip loopback devices
//                     if (devMajor == 7) {
//                         continue;
//                     }
                    
//                     // For I/O monitoring, we want the parent device, not the partition
//                     // Check if this is the parent device (same major, no 'p' in name)
//                     if (devMajor == major && diskDevice.find('p') == std::string::npos) {
//                         // For NVMe devices, the parent should match the partition name prefix
//                         // e.g., nvme1n1p2 -> nvme1n1
//                         std::string expectedParent = deviceName.substr(0, deviceName.find('p'));
//                         if (diskDevice == expectedParent) {
//                             Log(DEBUG, "OperationsMonitor", "Found parent disk device '{}' for path on partition: {}", diskDevice, path);
//                             return diskDevice;
//                         }
//                     }
                    
//                     // Only use exact partition match as fallback if no parent found
//                     if (devMajor == major && devMinor == minor) {
//                         Log(DEBUG, "OperationsMonitor", "Found exact disk device '{}' for path (fallback): {}", diskDevice, path);
//                         return diskDevice;
//                     }
//                 }
                
//                 Log(WARNING, "OperationsMonitor", "Could not find device '{}' in /proc/diskstats", deviceName);
//                 return "";
//             }
//         }
        
//         Log(WARNING, "OperationsMonitor", "Could not find device '{}' in /proc/partitions", deviceName);
//         return "";
//     } catch (const std::exception& e) {
//         Log(WARNING, "OperationsMonitor", "Exception getting disk device for path {}: {}", path, e.what());
//         return "";
//     }
// #else
//     return "";
// #endif

return "";
}

std::string ResourceMonitor::getOutputDiskDevice() const {
    // Get the current working directory (where the binary is located)
    std::string currentDir = std::filesystem::current_path().string();
    return getDiskDeviceForPath(currentDir);
}

std::string ResourceMonitor::getGameDiskDevice() const {
    return getDiskDeviceForPath(PIE4K_CFG.GamePath);
}

} // namespace ProjectIE4k
