#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace ProjectIE4k {

/**
 * @brief System resource metrics
 */
struct SystemMetrics {
    // CPU metrics
    double cpuUsagePercent = 0.0;
    int cpuCoreCount = 0;
    int availableCores = 0;
    
    // Memory metrics
    uint64_t totalRAM = 0;
    uint64_t usedRAM = 0;
    uint64_t availableRAM = 0;
    double ramUsagePercent = 0.0;
    
    // GPU metrics (if available)
    double gpuUsagePercent = 0.0;
    uint64_t totalVRAM = 0;
    uint64_t usedVRAM = 0;
    uint64_t availableVRAM = 0;
    double vramUsagePercent = 0.0;
    
    // Disk I/O metrics
    uint64_t diskReadBytesPerSec = 0;
    uint64_t diskWriteBytesPerSec = 0;
    uint64_t diskReadOpsPerSec = 0;
    uint64_t diskWriteOpsPerSec = 0;
    
    // Game disk read metrics (for game file access)
    uint64_t gameDiskReadBytesPerSec = 0;
    uint64_t gameDiskReadOpsPerSec = 0;
    
    // Timestamp
    std::chrono::steady_clock::time_point timestamp;
    
    // Validation flag
    bool valid = false;
};

/**
 * @brief Lightweight, high-performance resource monitoring
 * 
 * Focuses solely on collecting system metrics with minimal overhead.
 * Uses async collection and smart caching to avoid performance bottlenecks.
 */
class ResourceMonitor {
public:
    ResourceMonitor();
    ~ResourceMonitor();
    
    // Lifecycle management
    void initialize();
    void shutdown();
    bool isInitialized() const { return initialized_; }
    
    // Metrics access - fast, cached access
    SystemMetrics getCurrentMetrics() const;
    SystemMetrics getFreshMetrics(); // Force immediate update
    
    // Configuration
    void setUpdateInterval(std::chrono::milliseconds interval);
    std::chrono::milliseconds getUpdateInterval() const { return updateInterval_; }
    
    // Callbacks for metric changes
    using MetricsCallback = std::function<void(const SystemMetrics&)>;
    void setMetricsCallback(MetricsCallback callback);
    
    // Emergency VRAM protection
    bool isVRAMCritical() const; // > 90% usage
    bool isVRAMDangerous() const; // > 95% usage
    
private:
  bool verbose = true;

  // Core metric collection
  void metricsUpdateLoop();
  void updateCPUMetrics(SystemMetrics &metrics);
  void updateMemoryMetrics(SystemMetrics &metrics);
  void updateGPUMetrics(SystemMetrics &metrics);
  void updateDiskIOMetrics(SystemMetrics &metrics);

  // Validation and consistency checks
  void validateMetrics(SystemMetrics &metrics);

  // Disk device helpers
  std::string getDiskDeviceForPath(const std::string &path) const;
  std::string getOutputDiskDevice() const;
  std::string getGameDiskDevice() const;

  // State management
  std::atomic<bool> initialized_{false};
  std::atomic<bool> shutdown_{false};

  // Threading
  std::thread metricsThread_;

  // Metrics storage with double buffering for thread safety
  mutable std::mutex metricsMutex_;
  SystemMetrics currentMetrics_;
  SystemMetrics previousMetrics_;
  std::chrono::steady_clock::time_point lastUpdate_;
  std::chrono::milliseconds updateInterval_{
      100}; // Fast updates for responsiveness

  // Callback system
  MetricsCallback metricsCallback_;
  mutable std::mutex callbackMutex_;

  // VRAM tracking for emergency protection
  std::atomic<bool> vramCritical_{false};
  std::atomic<bool> vramDangerous_{false};
};

} // namespace ProjectIE4k
