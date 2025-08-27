#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ProjectIE4k {

// Forward declarations
struct SystemMetrics;
class ResourceMonitor;

/**
 * @brief Resource profile for an operation type
 */
struct ResourceProfile {
    std::string operationType;
    uint64_t estimatedRAM = 0;
    uint64_t estimatedVRAM = 0;
    double estimatedCPU = 0.0;
    int optimalConcurrency = 1;
    bool learned = false;
    std::chrono::steady_clock::time_point lastUpdated;
    
    // Learning statistics
    uint32_t sampleCount = 0;
    uint64_t totalRAMUsed = 0;
    uint64_t totalVRAMUsed = 0;
    double totalCPUUsed = 0.0;
    std::chrono::milliseconds totalExecutionTime{0};
};

/**
 * @brief Performance statistics for operations monitoring
 */
struct OperationsPerformanceStats {
    uint64_t totalTasksCompleted = 0;
    uint64_t totalTasksFailed = 0;
    std::chrono::milliseconds averageTaskTime{0};
    std::chrono::milliseconds totalProcessingTime{0};
    double averageCPUUsage = 0.0;
    double averageRAMUsage = 0.0;
    double averageVRAMUsage = 0.0;
    
    // Resource efficiency metrics
    double ramEfficiency = 0.0; // actual vs estimated
    double vramEfficiency = 0.0;
    double cpuEfficiency = 0.0;
};

/**
 * @brief Intelligent performance profiling and learning system
 * 
 * Learns from actual resource usage to improve future estimates.
 * Provides adaptive concurrency and resource optimization.
 */
class PerformanceProfiler {
public:
    PerformanceProfiler();
    ~PerformanceProfiler();
    
    // Lifecycle management
    void initialize(std::shared_ptr<ResourceMonitor> resourceMonitor = nullptr);
    void shutdown();
    bool isInitialized() const { return initialized_; }
    
    // Resource profile management
    void registerResourceProfile(const std::string& operationType, uint64_t ramUsage, uint64_t vramUsage, double cpuUsage);
    ResourceProfile getResourceProfile(const std::string& operationType) const;
    bool hasProfile(const std::string& operationType) const;
    
    // Learning from actual usage
    void recordTaskExecution(
        const std::string& operationType,
        const std::chrono::milliseconds& executionTime,
        uint64_t actualRAMUsed,
        uint64_t actualVRAMUsed,
        double actualCPUUsed,
        bool success,
        bool saveProfile = true
    );
    
    // Intelligent resource estimation
    uint64_t estimateMemoryForOperation(const std::string& operationType, uint64_t inputSize = 0) const;
    uint64_t estimateVRAMForOperation(const std::string& operationType, uint64_t inputSize = 0) const;
    int estimateCPUCoresForOperation(const std::string& operationType, uint64_t inputSize = 0) const;

    // Performance recording only - no concurrency control
    OperationsPerformanceStats getPerformanceStats() const;
    
    /**
     * @brief Seed resource profile with known estimates
     * @param operationType Type of operation
     * @param estimatedRAM Estimated RAM usage in bytes
     * @param estimatedVRAM Estimated VRAM usage in bytes
     * @param estimatedCPU Estimated CPU usage percentage
     */
    void seedResourceProfile(const std::string& operationType, uint64_t estimatedRAM, uint64_t estimatedVRAM, double estimatedCPU);
    OperationsPerformanceStats getOperationStats(const std::string& operationType) const;
    
    // Dynamic optimization
    void updateMetricsInterval(const std::chrono::milliseconds& taskTime);
    std::chrono::milliseconds getRecommendedMetricsInterval() const { return recommendedMetricsInterval_; }
    
    // Profile persistence (for learning across restarts)
    void saveProfilesToFile(const std::string& filename) const;
    void loadProfilesFromFile(const std::string& filename);

    // Performance recording only - removed concurrency callback

    // Dynamic metrics interval
    std::atomic<std::chrono::milliseconds> recommendedMetricsInterval_{
        std::chrono::milliseconds(250)};

    // Performance recording only - removed concurrency callback

    // Resource monitoring
    std::shared_ptr<ResourceMonitor> resourceMonitor_;

    // Configuration
    static constexpr size_t MAX_RECENT_SAMPLES = 100;
    static constexpr size_t MIN_SAMPLES_FOR_LEARNING = 1;
    static constexpr double LEARNING_RATE =
        0.1; // How quickly to adapt to new data
    static constexpr std::chrono::hours PROFILE_EXPIRY{
        24}; // Remove unused profiles after 24h

  private:
    // Learning algorithms (performance recording only)
    void updateResourceProfile(ResourceProfile &profile, uint64_t ramUsed,
                               uint64_t vramUsed, double cpuUsed,
                               const std::chrono::milliseconds &executionTime);

    // Statistical analysis
    double calculateMovingAverage(const std::vector<double>& values, size_t windowSize = 10) const;
    double calculateEfficiency(uint64_t actual, uint64_t estimated) const;
    
    // Profile management
    void cleanupOldProfiles();
    void validateProfile(ResourceProfile& profile) const;
    
    // State management
    std::atomic<bool> initialized_{false};
    std::atomic<bool> shutdown_{false};
    
    // Profile storage
    mutable std::mutex profilesMutex_;
    std::map<std::string, ResourceProfile> resourceProfiles_;
    
    // Performance tracking
    mutable std::mutex statsMutex_;
    OperationsPerformanceStats globalStats_;
    std::map<std::string, OperationsPerformanceStats> operationStats_;
    
    // Recent performance data for adaptive algorithms
    std::vector<std::chrono::milliseconds> recentTaskTimes_;
    std::vector<double> recentCPUUsage_;
    std::vector<double> recentRAMUsage_;
    std::vector<double> recentVRAMUsage_;
};

} // namespace ProjectIE4k
