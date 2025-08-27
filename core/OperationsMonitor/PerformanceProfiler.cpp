#include "PerformanceProfiler.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <numeric>
#include <fstream>

#include "ResourceMonitor.h"
#include "core/Logging/Logging.h"
#include "core/CFG.h"

namespace ProjectIE4k {

PerformanceProfiler::PerformanceProfiler() {
    Log(DEBUG, "PerformanceProfiler", "PerformanceProfiler created");
}

PerformanceProfiler::~PerformanceProfiler() {
    shutdown();
}

void PerformanceProfiler::initialize(std::shared_ptr<ResourceMonitor> resourceMonitor) {
    if (initialized_) return;
    
    Log(MESSAGE, "PerformanceProfiler", "Initializing PerformanceProfiler");
    
    // Store ResourceMonitor reference
    resourceMonitor_ = resourceMonitor;
    
    // Initialize global statistics
    globalStats_ = OperationsPerformanceStats{};
    
    initialized_ = true;
    Log(MESSAGE, "PerformanceProfiler", "PerformanceProfiler initialized successfully");
}

void PerformanceProfiler::shutdown() {
    if (!initialized_ || shutdown_) return;
    
    Log(MESSAGE, "PerformanceProfiler", "Shutting down PerformanceProfiler");
    
    shutdown_ = true;
    
    Log(MESSAGE, "PerformanceProfiler", "PerformanceProfiler shutdown complete");
}

void PerformanceProfiler::registerResourceProfile(const std::string& operationType, uint64_t ramUsage, uint64_t vramUsage, double cpuUsage) {
    std::lock_guard<std::mutex> lock(profilesMutex_);
    
    ResourceProfile& profile = resourceProfiles_[operationType];
    profile.operationType = operationType;
    profile.estimatedRAM = ramUsage;
    profile.estimatedVRAM = vramUsage;
    profile.estimatedCPU = cpuUsage;
    profile.learned = false; // This is a manual registration, not learned
    profile.lastUpdated = std::chrono::steady_clock::now();
    profile.optimalConcurrency = 1; // Default
    
    Log(DEBUG, "PerformanceProfiler", "Registered resource profile for {}: {}MB RAM, {}MB VRAM, {:.1f}% CPU", 
        operationType, ramUsage / (1024 * 1024), vramUsage / (1024 * 1024), cpuUsage);
}

ResourceProfile PerformanceProfiler::getResourceProfile(const std::string& operationType) const {
    std::lock_guard<std::mutex> lock(profilesMutex_);
    
    auto it = resourceProfiles_.find(operationType);
    if (it != resourceProfiles_.end()) {
        return it->second;
    }
    
    // Return default profile if not found
    ResourceProfile defaultProfile;
    defaultProfile.operationType = operationType;
    defaultProfile.estimatedRAM = 1024 * 1024 * 1024; // 1GB default
    defaultProfile.estimatedVRAM = 512 * 1024 * 1024; // 512MB default
    defaultProfile.estimatedCPU = 25.0; // 25% CPU default
    defaultProfile.optimalConcurrency = 1;
    defaultProfile.learned = false;
    
    return defaultProfile;
}

bool PerformanceProfiler::hasProfile(const std::string& operationType) const {
    std::lock_guard<std::mutex> lock(profilesMutex_);
    return resourceProfiles_.find(operationType) != resourceProfiles_.end();
}

void PerformanceProfiler::recordTaskExecution(
    const std::string& operationType,
    const std::chrono::milliseconds& executionTime,
    uint64_t actualRAMUsed,
    uint64_t actualVRAMUsed,
    double actualCPUUsed,
    bool success,
    bool saveProfile) {
    
    // Update global statistics
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        
        if (success) {
            globalStats_.totalTasksCompleted++;
        } else {
            globalStats_.totalTasksFailed++;
        }
        
        globalStats_.totalProcessingTime += executionTime;
        
        // Update recent performance data
        recentTaskTimes_.push_back(executionTime);
        if (recentTaskTimes_.size() > MAX_RECENT_SAMPLES) {
            recentTaskTimes_.erase(recentTaskTimes_.begin());
        }
        
        recentCPUUsage_.push_back(actualCPUUsed);
        if (recentCPUUsage_.size() > MAX_RECENT_SAMPLES) {
            recentCPUUsage_.erase(recentCPUUsage_.begin());
        }
        
        // Calculate averages
        if (!recentTaskTimes_.empty()) {
            auto totalTime = std::accumulate(recentTaskTimes_.begin(), recentTaskTimes_.end(), std::chrono::milliseconds(0));
            globalStats_.averageTaskTime = totalTime / recentTaskTimes_.size();
        }
        
        if (!recentCPUUsage_.empty()) {
            globalStats_.averageCPUUsage = std::accumulate(recentCPUUsage_.begin(), recentCPUUsage_.end(), 0.0) / recentCPUUsage_.size();
        }
        
        // Update operation-specific statistics
        OperationsPerformanceStats& opStats = operationStats_[operationType];
        if (success) {
            opStats.totalTasksCompleted++;
        } else {
            opStats.totalTasksFailed++;
        }
        opStats.totalProcessingTime += executionTime;
    }
    
    // Update resource profile learning (only if saveProfile is true)
    if (success && saveProfile) {
        std::lock_guard<std::mutex> lock(profilesMutex_);
        
        ResourceProfile& profile = resourceProfiles_[operationType];
        if (profile.operationType.empty()) {
            profile.operationType = operationType;
            profile.lastUpdated = std::chrono::steady_clock::now();
        }
        
        updateResourceProfile(profile, actualRAMUsed, actualVRAMUsed, actualCPUUsed, executionTime);
    }
    
    Log(DEBUG, "PerformanceProfiler", "Recorded execution for {}: {}ms, {}MB RAM, {}MB VRAM, {:.1f}% CPU, success: {}, saveProfile: {}", 
        operationType, executionTime.count(), actualRAMUsed / (1024 * 1024), actualVRAMUsed / (1024 * 1024), actualCPUUsed, success, saveProfile);
}

uint64_t PerformanceProfiler::estimateMemoryForOperation(const std::string& operationType, uint64_t inputSize) const {
    ResourceProfile profile = getResourceProfile(operationType);
    
    // If we have learned data, use it
    if (profile.learned && profile.sampleCount >= MIN_SAMPLES_FOR_LEARNING) {
        uint64_t estimate = profile.estimatedRAM;
        
        // Scale based on input size if provided
        if (inputSize > 0) {
            // Simple heuristic: assume memory scales with input size
            estimate = std::max(estimate, inputSize * PIE4K_CFG.UpScaleFactor);
        }
        
        return estimate;
    }
    
    // Fallback to static estimates
    if (operationType.find("upscale") != std::string::npos) {
        return std::max(static_cast<uint64_t>(2ULL * 1024 * 1024 * 1024), inputSize * 4); // 2GB or 4x input size
    } else if (operationType.find("compress") != std::string::npos) {
        return std::max(static_cast<uint64_t>(1ULL * 1024 * 1024 * 1024), inputSize * 2); // 1GB or 2x input size
    } else {
        return profile.estimatedRAM;
    }
}

uint64_t PerformanceProfiler::estimateVRAMForOperation(const std::string& operationType, uint64_t inputSize) const {
    ResourceProfile profile = getResourceProfile(operationType);
    
    // If we have learned data, use it
    if (profile.learned && profile.sampleCount >= MIN_SAMPLES_FOR_LEARNING) {
        uint64_t estimate = profile.estimatedVRAM;
        
        // Scale based on input size if provided
        if (inputSize > 0) {
            // GPU operations often need significant VRAM for textures
            estimate = std::max(estimate, inputSize * 3); // At least 3x input size for GPU ops
        }
        
        return estimate;
    }
    
    // Fallback to static estimates
    if (operationType.find("upscale") != std::string::npos) {
        return std::max(static_cast<uint64_t>(1ULL * 1024 * 1024 * 1024), inputSize * 6); // 1GB or 6x input size
    } else if (operationType.find("gpu") != std::string::npos || operationType.find("vulkan") != std::string::npos) {
        return std::max(static_cast<uint64_t>(512ULL * 1024 * 1024), inputSize * 4); // 512MB or 4x input size
    } else {
        return profile.estimatedVRAM;
    }
}

int PerformanceProfiler::estimateCPUCoresForOperation(const std::string& operationType, uint64_t inputSize) const {
    ResourceProfile profile = getResourceProfile(operationType);
    
    if (profile.learned && profile.sampleCount >= MIN_SAMPLES_FOR_LEARNING) {
        return std::max(1, static_cast<int>(profile.estimatedCPU / 25.0)); // Assume 25% per core
    }
    
    // Fallback to static estimates
    if (operationType.find("compress") != std::string::npos) {
        return std::min(8, std::max(2, static_cast<int>(std::thread::hardware_concurrency() / 2)));
    } else if (operationType.find("parallel") != std::string::npos) {
        return std::min(4, static_cast<int>(std::thread::hardware_concurrency()));
    } else {
        return 1;
    }
}

// Performance recording only - concurrency control removed

OperationsPerformanceStats PerformanceProfiler::getPerformanceStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return globalStats_;
}

void PerformanceProfiler::seedResourceProfile(const std::string& operationType, uint64_t estimatedRAM, uint64_t estimatedVRAM, double estimatedCPU) {
    std::lock_guard<std::mutex> lock(profilesMutex_);
    
    ResourceProfile& profile = resourceProfiles_[operationType];
    profile.operationType = operationType;
    profile.estimatedRAM = estimatedRAM;
    profile.estimatedVRAM = estimatedVRAM;
    profile.estimatedCPU = estimatedCPU;
    profile.learned = true; // Mark as learned so auto-scaling will use it
    profile.sampleCount = MIN_SAMPLES_FOR_LEARNING; // Set minimum samples to enable usage
    profile.optimalConcurrency = 1; // Start conservative
    profile.lastUpdated = std::chrono::steady_clock::now();
    
    Log(DEBUG, "PerformanceProfiler", "Seeded resource profile for '{}': {}MB RAM, {}MB VRAM, {:.1f}% CPU", 
        operationType, estimatedRAM / (1024 * 1024), estimatedVRAM / (1024 * 1024), estimatedCPU);
}

OperationsPerformanceStats PerformanceProfiler::getOperationStats(const std::string& operationType) const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    auto it = operationStats_.find(operationType);
    if (it != operationStats_.end()) {
        return it->second;
    }
    
    return OperationsPerformanceStats{}; // Return empty stats if not found
}

void PerformanceProfiler::updateMetricsInterval(const std::chrono::milliseconds& taskTime) {
    // Adaptive metrics interval based on task execution time
    if (taskTime < std::chrono::milliseconds(100)) {
        // Fast tasks - use faster metrics updates
        recommendedMetricsInterval_ = std::chrono::milliseconds(50);
    } else if (taskTime < std::chrono::milliseconds(1000)) {
        // Medium tasks - standard interval
        recommendedMetricsInterval_ = std::chrono::milliseconds(100);
    } else {
        // Slow tasks - slower updates to reduce overhead
        recommendedMetricsInterval_ = std::chrono::milliseconds(250);
    }
}

void PerformanceProfiler::saveProfilesToFile(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(profilesMutex_);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        Log(WARNING, "PerformanceProfiler", "Failed to open file for saving profiles: {}", filename);
        return;
    }
    
    for (const auto& [operationType, profile] : resourceProfiles_) {
        if (profile.learned && profile.sampleCount >= MIN_SAMPLES_FOR_LEARNING) {
            file << operationType << "," 
                 << profile.estimatedRAM << ","
                 << profile.estimatedVRAM << ","
                 << profile.estimatedCPU << ","
                 << profile.optimalConcurrency << ","
                 << profile.sampleCount << "\n";
        }
    }
    
    Log(DEBUG, "PerformanceProfiler", "Saved {} learned profiles to {}", resourceProfiles_.size(), filename);
}

void PerformanceProfiler::loadProfilesFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        Log(DEBUG, "PerformanceProfiler", "No existing profiles file found: {}", filename);
        return;
    }
    
    std::lock_guard<std::mutex> lock(profilesMutex_);
    
    std::string line;
    int loadedCount = 0;
    
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string operationType;
        uint64_t estimatedRAM, estimatedVRAM;
        double estimatedCPU;
        int optimalConcurrency;
        uint32_t sampleCount;
        
        if (std::getline(iss, operationType, ',') &&
            (iss >> estimatedRAM) && iss.ignore(1, ',') &&
            (iss >> estimatedVRAM) && iss.ignore(1, ',') &&
            (iss >> estimatedCPU) && iss.ignore(1, ',') &&
            (iss >> optimalConcurrency) && iss.ignore(1, ',') &&
            (iss >> sampleCount)) {
            
            ResourceProfile& profile = resourceProfiles_[operationType];
            profile.operationType = operationType;
            profile.estimatedRAM = estimatedRAM;
            profile.estimatedVRAM = estimatedVRAM;
            profile.estimatedCPU = estimatedCPU;
            profile.optimalConcurrency = optimalConcurrency;
            profile.sampleCount = sampleCount;
            profile.learned = true;
            profile.lastUpdated = std::chrono::steady_clock::now();
            
            loadedCount++;
        }
    }
    
    Log(MESSAGE, "PerformanceProfiler", "Loaded {} learned profiles from {}", loadedCount, filename);
}

void PerformanceProfiler::updateResourceProfile(ResourceProfile& profile, uint64_t ramUsed, uint64_t vramUsed, 
                                               double cpuUsed, const std::chrono::milliseconds& executionTime) {
    profile.sampleCount++;
    profile.totalRAMUsed += ramUsed;
    profile.totalVRAMUsed += vramUsed;
    profile.totalCPUUsed += cpuUsed;
    profile.totalExecutionTime += executionTime;
    profile.lastUpdated = std::chrono::steady_clock::now();
    
    if (profile.sampleCount >= MIN_SAMPLES_FOR_LEARNING) {
        // Use exponential moving average for learning
        double alpha = LEARNING_RATE;
        
        uint64_t avgRAM = profile.totalRAMUsed / profile.sampleCount;
        uint64_t avgVRAM = profile.totalVRAMUsed / profile.sampleCount;
        double avgCPU = profile.totalCPUUsed / profile.sampleCount;
        
        if (profile.learned) {
            // Update existing estimates with exponential moving average
            profile.estimatedRAM = static_cast<uint64_t>((1.0 - alpha) * profile.estimatedRAM + alpha * avgRAM);
            profile.estimatedVRAM = static_cast<uint64_t>((1.0 - alpha) * profile.estimatedVRAM + alpha * avgVRAM);
            profile.estimatedCPU = (1.0 - alpha) * profile.estimatedCPU + alpha * avgCPU;
            
            // Concurrency already learned - don't recalculate
            Log(DEBUG, "PerformanceProfiler", "Profile already learned for {}, skipping concurrency recalculation", profile.operationType);
        } else {
            // First time learning - calculate optimal concurrency ONCE
            profile.estimatedRAM = avgRAM;
            profile.estimatedVRAM = avgVRAM;
            profile.estimatedCPU = avgCPU;
            profile.learned = true;
            
            Log(DEBUG, "PerformanceProfiler", "First time learning for {}, calculating optimal concurrency", profile.operationType);
            
            // Calculate optimal concurrency based on learned resource usage (ONLY ONCE)
            SystemMetrics currentMetrics = resourceMonitor_->getFreshMetrics();
            
            // Start with CPU-based concurrency
            int cpuBasedConcurrency;
            if (profile.estimatedCPU < 25.0) {
                cpuBasedConcurrency = std::min(8, static_cast<int>(std::thread::hardware_concurrency()));
            } else if (profile.estimatedCPU < 50.0) {
                cpuBasedConcurrency = std::min(4, static_cast<int>(std::thread::hardware_concurrency() / 2));
            } else {
                cpuBasedConcurrency = 1;
            }
            
            // Apply VRAM constraints using learned VRAM usage
            int vramBasedConcurrency = cpuBasedConcurrency;
            if (profile.estimatedVRAM > 0 && currentMetrics.availableVRAM > 0) {
              double vramSafetyFactor = (PIE4K_CFG.MaxVRAM / 100);
              double availableForOps =
                  currentMetrics.availableVRAM * vramSafetyFactor;
              vramBasedConcurrency =
                  static_cast<int>(availableForOps / profile.estimatedVRAM);

              Log(DEBUG, "PerformanceProfiler",
                  "Learned concurrency calculation for {}:",
                  profile.operationType);
              Log(DEBUG, "PerformanceProfiler", "  - CPU-based: {}",
                  cpuBasedConcurrency);
              Log(DEBUG, "PerformanceProfiler",
                  "  - Learned VRAM per task: {:.2f}GB",
                  profile.estimatedVRAM / (1024.0 * 1024 * 1024));
              Log(DEBUG, "PerformanceProfiler", "  - Available VRAM: {:.2f}GB",
                  currentMetrics.availableVRAM / (1024.0 * 1024 * 1024));
              Log(DEBUG, "PerformanceProfiler",
                  "  - Available for ops (90%): {:.2f}GB",
                  availableForOps / (1024.0 * 1024 * 1024));
              Log(DEBUG, "PerformanceProfiler", "  - VRAM-based: {}",
                  vramBasedConcurrency);
            }
            
            // Use the most restrictive constraint
            profile.optimalConcurrency = std::max(1, std::min(cpuBasedConcurrency, vramBasedConcurrency));
            
            Log(DEBUG, "PerformanceProfiler", "Final learned concurrency for {}: {}", profile.operationType, profile.optimalConcurrency);

            // Performance recording only - concurrency control removed
        }
        
        validateProfile(profile);
        
        // Log the learned resource profile for debugging
        Log(DEBUG, "PerformanceProfiler", "Updated resource profile for {}: {}MB VRAM, {}MB RAM, {:.1f}% CPU",
            profile.operationType, 
            profile.estimatedVRAM / (1024 * 1024),
            profile.estimatedRAM / (1024 * 1024), 
            profile.estimatedCPU);
    }
}

void PerformanceProfiler::cleanupOldProfiles() {
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> toRemove;
    
    for (const auto& [operationType, profile] : resourceProfiles_) {
        if (now - profile.lastUpdated > PROFILE_EXPIRY) {
            toRemove.push_back(operationType);
        }
    }
    
    for (const std::string& operationType : toRemove) {
        resourceProfiles_.erase(operationType);
    }
    
    if (!toRemove.empty()) {
        Log(DEBUG, "PerformanceProfiler", "Cleaned up {} expired profiles", toRemove.size());
    }
}

void PerformanceProfiler::validateProfile(ResourceProfile& profile) const {
    // Ensure reasonable bounds on estimates
    // Ensure clamp ranges are valid
    uint64_t minRAM = static_cast<uint64_t>(64ULL * 1024 * 1024);
    uint64_t maxRAM = static_cast<uint64_t>(32ULL * 1024 * 1024 * 1024);
    if (maxRAM < minRAM) std::swap(minRAM, maxRAM);
    profile.estimatedRAM = std::clamp(profile.estimatedRAM, minRAM, maxRAM); // 64MB - 32GB

    uint64_t minVRAM = static_cast<uint64_t>(0ULL);
    uint64_t maxVRAM = static_cast<uint64_t>(24ULL * 1024 * 1024 * 1024);
    if (maxVRAM < minVRAM) std::swap(minVRAM, maxVRAM);
    profile.estimatedVRAM = std::clamp(profile.estimatedVRAM, minVRAM, maxVRAM); // 0 - 24GB

    int hwThreads = static_cast<int>(std::thread::hardware_concurrency());
    if (hwThreads <= 0) hwThreads = 1;
    double maxCPU = 100.0 * hwThreads;
    profile.estimatedCPU = std::clamp(profile.estimatedCPU, 0.0, maxCPU); // 0 - 100% per core

    int maxConcurrency = std::max(1, hwThreads * 2);
    profile.optimalConcurrency = std::clamp(profile.optimalConcurrency, 1, maxConcurrency);
}

double PerformanceProfiler::calculateMovingAverage(const std::vector<double>& values, size_t windowSize) const {
    if (values.empty()) return 0.0;
    
    size_t actualWindowSize = std::min(windowSize, values.size());
    size_t startIndex = values.size() - actualWindowSize;
    
    double sum = 0.0;
    for (size_t i = startIndex; i < values.size(); ++i) {
        sum += values[i];
    }
    
    return sum / actualWindowSize;
}

double PerformanceProfiler::calculateEfficiency(uint64_t actual, uint64_t estimated) const {
    if (estimated == 0) return 1.0;
    return static_cast<double>(actual) / static_cast<double>(estimated);
}

} // namespace ProjectIE4k
