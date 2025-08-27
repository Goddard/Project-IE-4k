#include "ResourceReservation.h"
#include "core/CFG.h"
#include "core/Logging/Logging.h"
#include <algorithm>
#include <numeric>

namespace ProjectIE4k {

ResourceReservationManager::ResourceReservationManager() {
    Log(DEBUG, "ResourceReservationManager", "ResourceReservationManager created");
}

ResourceReservationManager::~ResourceReservationManager() {
    shutdown();
}

void ResourceReservationManager::initialize(std::shared_ptr<ResourceMonitor> resourceMonitor) {
    if (initialized_) return;
    
    resourceMonitor_ = resourceMonitor;
    if (!resourceMonitor_) {
        Log(ERROR, "ResourceReservationManager", "Cannot initialize without ResourceMonitor");
        return;
    }
    
    Log(MESSAGE, "ResourceReservationManager", "Initializing ResourceReservationManager");
    
    lastCleanup_ = std::chrono::steady_clock::now();

    // Capture baseline available resources at startup
    SystemMetrics metrics = resourceMonitor_->getCurrentMetrics();
    baselineAvailableVRAM_ = metrics.availableVRAM;
    baselineAvailableRAM_ = metrics.availableRAM;
    Log(DEBUG, "ResourceReservationManager",
        "Captured baseline availability: VRAM={}MB, RAM={}MB",
        baselineAvailableVRAM_.load() / (1024 * 1024),
        baselineAvailableRAM_.load() / (1024 * 1024));

    initialized_ = true;
    Log(MESSAGE, "ResourceReservationManager", "ResourceReservationManager initialized successfully");
}

void ResourceReservationManager::shutdown() {
    if (!initialized_ || shutdown_) return;
    
    Log(MESSAGE, "ResourceReservationManager", "Shutting down ResourceReservationManager");
    
    shutdown_ = true;
    
    // Release all active reservations
    forceReleaseAll();
    
    Log(MESSAGE, "ResourceReservationManager", "ResourceReservationManager shutdown complete");
}

bool ResourceReservationManager::tryReserveResources(const OperationRequirements& requirements, const std::string& taskId) {
    std::lock_guard<std::mutex> lock(reservationsMutex_);
    
    // Clean up expired reservations periodically
    auto now = std::chrono::steady_clock::now();
    if (now - lastCleanup_ > std::chrono::minutes(1)) {
        cleanupExpiredReservations();
        lastCleanup_ = now;
    }
    
    // Get current system metrics
    SystemMetrics metrics = resourceMonitor_->getFreshMetrics();

    // Lazy baseline init if not captured (e.g., service started after monitor
    // warmup)
    if (baselineAvailableVRAM_.load() == 0)
      baselineAvailableVRAM_ = metrics.availableVRAM;
    if (baselineAvailableRAM_.load() == 0)
      baselineAvailableRAM_ = metrics.availableRAM;

    // Check if we can allocate the requested resources using internal
    // accounting
    if (!checkResourceAvailability(requirements, metrics)) {
        return false;
    }

    // Live VRAM hard-stop guard: if live VRAM usage is already at or above
    // the configured MaxVRAM fraction, defer new reservations until it drops.
    // if (metrics.totalVRAM > 0) {
    //     // Use raw NVML-based VRAM usage percent; do not derive from
    //     availableVRAM
    //     // because availableVRAM is adjusted by baseline/safety in
    //     ResourceMonitor if (metrics.vramUsagePercent >= PIE4K_CFG.MaxVRAM) {
    //         Log(DEBUG, "ResourceReservationManager",
    //             "Deferring reservation due to live VRAM {:.1f}% >= MaxVRAM
    //             {:.1f}%", metrics.vramUsagePercent, PIE4K_CFG.MaxVRAM);
    //         return false;
    //     }
    // }

    // Handle exclusive access
    if (requirements.resourceAccess == ResourceAccess::EXCLUSIVE) {
        if (!canGrantExclusiveAccess()) {
            Log(DEBUG, "ResourceReservationManager", "Cannot grant exclusive access - other reservations exist");
            return false;
        }
        setExclusiveReservation(true);
    }
    
    // Create the reservation
    std::string reservationId = generateReservationId();
    ResourceReservation reservation(reservationId, requirements, taskId);
    
    activeReservations_[reservationId] = reservation;
    
    // Track reservations by task ID for easy cleanup
    if (!taskId.empty()) {
        taskToReservations_[taskId].insert(reservationId);
    }
    
    // Update fast access counters
    updateUsageCounters();
    
    Log(DEBUG, "ResourceReservationManager", "Reserved resources [{}]: {}MB RAM, {}MB VRAM, {} cores for task {}", 
        reservationId,
        requirements.estimatedMemoryUsage / (1024 * 1024),
        requirements.estimatedVRAMUsage / (1024 * 1024),
        requirements.estimatedCPUCores,
        taskId);
    
    return true;
}

void ResourceReservationManager::releaseResources(const std::string& reservationId) {
    std::lock_guard<std::mutex> lock(reservationsMutex_);
    
    auto it = activeReservations_.find(reservationId);
    if (it == activeReservations_.end()) {
        Log(WARNING, "ResourceReservationManager", "Attempted to release non-existent reservation: {}", reservationId);
        return;
    }
    
    const ResourceReservation& reservation = it->second;
    
    // Track reservation duration for statistics
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - reservation.reservedAt);
    
    {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        recentReservationDurations_.push_back(duration);
        if (recentReservationDurations_.size() > 100) {
            recentReservationDurations_.erase(recentReservationDurations_.begin());
        }
    }
    
    // Remove from task tracking
    if (!reservation.taskId.empty()) {
        auto taskIt = taskToReservations_.find(reservation.taskId);
        if (taskIt != taskToReservations_.end()) {
            taskIt->second.erase(reservationId);
            if (taskIt->second.empty()) {
                taskToReservations_.erase(taskIt);
            }
        }
    }
    
    // Handle exclusive access release
    if (reservation.requirements.resourceAccess == ResourceAccess::EXCLUSIVE) {
        setExclusiveReservation(false);
    }
    
    Log(DEBUG, "ResourceReservationManager", "Released resources [{}] after {}ms for task {}", 
        reservationId, duration.count(), reservation.taskId);
    
    activeReservations_.erase(it);
    
    // Update fast access counters
    updateUsageCounters();
}

void ResourceReservationManager::releaseResourcesByTaskId(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(reservationsMutex_);
    
    auto taskIt = taskToReservations_.find(taskId);
    if (taskIt == taskToReservations_.end()) {
        return; // No reservations for this task
    }
    
    // Release all reservations for this task
    auto reservationIds = taskIt->second; // Copy the set
    for (const std::string& reservationId : reservationIds) {
        auto it = activeReservations_.find(reservationId);
        if (it != activeReservations_.end()) {
            // Handle exclusive access release
            if (it->second.requirements.resourceAccess == ResourceAccess::EXCLUSIVE) {
                setExclusiveReservation(false);
            }
            
            activeReservations_.erase(it);
        }
    }
    
    taskToReservations_.erase(taskIt);
    
    // Update fast access counters
    updateUsageCounters();
    
    Log(DEBUG, "ResourceReservationManager", "Released {} reservations for task {}", reservationIds.size(), taskId);
}

bool ResourceReservationManager::canAllocateResources(const OperationRequirements& requirements) const {
    std::lock_guard<std::mutex> lock(reservationsMutex_);
    
    SystemMetrics metrics = resourceMonitor_->getCurrentMetrics();
    return checkResourceAvailability(requirements, metrics);
}

ResourceReservationManager::ResourceUsage ResourceReservationManager::getCurrentUsage() const {
    ResourceUsage usage;
    usage.reservedRAM = totalReservedRAM_.load();
    usage.reservedVRAM = totalReservedVRAM_.load();
    usage.reservedCores = totalReservedCores_.load();
    usage.hasExclusive = hasExclusiveReservation_.load();
    
    {
        std::lock_guard<std::mutex> lock(reservationsMutex_);
        usage.activeReservations = activeReservations_.size();
    }
    
    return usage;
}

void ResourceReservationManager::emergencyCleanup() {
    std::lock_guard<std::mutex> lock(reservationsMutex_);
    
    Log(WARNING, "ResourceReservationManager", "Performing emergency cleanup of stale reservations");
    
    cleanupExpiredReservations();
    
    // If we still have too many reservations, force cleanup of oldest ones
    if (activeReservations_.size() > 100) {
        std::vector<std::pair<std::chrono::steady_clock::time_point, std::string>> reservationsByAge;
        
        for (const auto& [id, reservation] : activeReservations_) {
            reservationsByAge.emplace_back(reservation.reservedAt, id);
        }
        
        std::sort(reservationsByAge.begin(), reservationsByAge.end());
        
        // Remove oldest 50% of reservations
        size_t toRemove = activeReservations_.size() / 2;
        for (size_t i = 0; i < toRemove && i < reservationsByAge.size(); ++i) {
            const std::string& reservationId = reservationsByAge[i].second;
            auto it = activeReservations_.find(reservationId);
            if (it != activeReservations_.end()) {
                if (it->second.requirements.resourceAccess == ResourceAccess::EXCLUSIVE) {
                    setExclusiveReservation(false);
                }
                activeReservations_.erase(it);
            }
        }
        
        updateUsageCounters();
        
        Log(WARNING, "ResourceReservationManager", "Emergency cleanup removed {} stale reservations", toRemove);
    }
}

void ResourceReservationManager::forceReleaseAll() {
    std::lock_guard<std::mutex> lock(reservationsMutex_);
    
    size_t count = activeReservations_.size();
    activeReservations_.clear();
    taskToReservations_.clear();
    
    // Reset counters
    totalReservedRAM_ = 0;
    totalReservedVRAM_ = 0;
    totalReservedCores_ = 0;
    hasExclusiveReservation_ = false;
    
    Log(WARNING, "ResourceReservationManager", "Force released {} reservations", count);
}

size_t ResourceReservationManager::getActiveReservationCount() const {
    std::lock_guard<std::mutex> lock(reservationsMutex_);
    return activeReservations_.size();
}

std::chrono::milliseconds ResourceReservationManager::getAverageReservationDuration() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    if (recentReservationDurations_.empty()) {
        return std::chrono::milliseconds(0);
    }
    
    auto total = std::accumulate(recentReservationDurations_.begin(), recentReservationDurations_.end(), 
                                std::chrono::milliseconds(0));
    
    return total / recentReservationDurations_.size();
}

bool ResourceReservationManager::checkResourceAvailability(const OperationRequirements& requirements, const SystemMetrics& metrics) const {
    // Calculate currently reserved resources
    uint64_t reservedRAM = totalReservedRAM_.load();
    uint64_t reservedVRAM = totalReservedVRAM_.load();
    int reservedCores = totalReservedCores_.load();

    // Prefer internal baseline tracking for available resources to avoid sticky
    // VRAM readings
    uint64_t baseAvailRAM = baselineAvailableRAM_.load();
    uint64_t baseAvailVRAM = baselineAvailableVRAM_.load();

    // Fallback to live metrics if baseline not set
    if (baseAvailRAM == 0 || baseAvailVRAM == 0) {
      baseAvailRAM = metrics.availableRAM;
      baseAvailVRAM = metrics.availableVRAM;
    }

    // Compute actual available after our own reservations (ignoring driver-side
    // sticky allocations)
    uint64_t actualAvailableRAM =
        (baseAvailRAM > reservedRAM) ? (baseAvailRAM - reservedRAM) : 0;
    uint64_t actualAvailableVRAM =
        (baseAvailVRAM > reservedVRAM) ? (baseAvailVRAM - reservedVRAM) : 0;
    int actualAvailableCores = (metrics.availableCores > reservedCores) ? (metrics.availableCores - reservedCores) : 0;

    Log(DEBUG, "ResourceReservationManager",
        "Availability calc: baseRAM={}MB baseVRAM={}MB reservedRAM={}MB "
        "reservedVRAM={}MB -> usableRAM={}MB usableVRAM={}MB",
        baseAvailRAM / (1024 * 1024), baseAvailVRAM / (1024 * 1024),
        reservedRAM / (1024 * 1024), reservedVRAM / (1024 * 1024),
        actualAvailableRAM / (1024 * 1024),
        actualAvailableVRAM / (1024 * 1024));

    // Check RAM availability using global config
    if (requirements.estimatedMemoryUsage > 0) {
      uint64_t ramBudget =
          static_cast<uint64_t>(actualAvailableRAM * PIE4K_CFG.MaxRAM);
      if (requirements.estimatedMemoryUsage > ramBudget) {
        Log(DEBUG, "ResourceReservationManager",
            "Insufficient RAM: need {}MB, have {}MB available",
            requirements.estimatedMemoryUsage / (1024 * 1024),
            ramBudget / (1024 * 1024));
        return false;
      }
    }

    // Check VRAM availability using global config
    if (requirements.estimatedVRAMUsage > 0 && metrics.totalVRAM > 0) {
      uint64_t vramBudget =
          static_cast<uint64_t>(actualAvailableVRAM * PIE4K_CFG.MaxVRAM);
      if (requirements.estimatedVRAMUsage > vramBudget) {
        Log(DEBUG, "ResourceReservationManager",
            "Insufficient VRAM: need {}MB, have {}MB available",
            requirements.estimatedVRAMUsage / (1024 * 1024),
            vramBudget / (1024 * 1024));
        return false;
      }
    }

    // Check CPU cores availability (no extra safety margin; rely on metrics)
    // if (requirements.estimatedCPUCores > actualAvailableCores) {
    //   Log(DEBUG, "ResourceReservationManager",
    //       "Insufficient CPU cores: need {}, have {} available",
    //       requirements.estimatedCPUCores, actualAvailableCores);
    //   return false;
    // }

    return true;
}

std::string ResourceReservationManager::generateReservationId() {
    return "res_" + std::to_string(++reservationIdCounter_);
}

void ResourceReservationManager::updateUsageCounters() {
    uint64_t totalRAM = 0;
    uint64_t totalVRAM = 0;
    int totalCores = 0;
    
    for (const auto& [id, reservation] : activeReservations_) {
        totalRAM += reservation.requirements.estimatedMemoryUsage;
        totalVRAM += reservation.requirements.estimatedVRAMUsage;
        totalCores += reservation.requirements.estimatedCPUCores;
    }
    
    totalReservedRAM_ = totalRAM;
    totalReservedVRAM_ = totalVRAM;
    totalReservedCores_ = totalCores;
}

void ResourceReservationManager::cleanupExpiredReservations() {
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> expiredReservations;
    
    for (const auto& [id, reservation] : activeReservations_) {
        if (now - reservation.reservedAt > RESERVATION_TIMEOUT) {
            expiredReservations.push_back(id);
        }
    }
    
    for (const std::string& id : expiredReservations) {
        auto it = activeReservations_.find(id);
        if (it != activeReservations_.end()) {
            if (it->second.requirements.resourceAccess == ResourceAccess::EXCLUSIVE) {
                setExclusiveReservation(false);
            }
            
            // Remove from task tracking
            if (!it->second.taskId.empty()) {
                auto taskIt = taskToReservations_.find(it->second.taskId);
                if (taskIt != taskToReservations_.end()) {
                    taskIt->second.erase(id);
                    if (taskIt->second.empty()) {
                        taskToReservations_.erase(taskIt);
                    }
                }
            }
            
            activeReservations_.erase(it);
        }
    }
    
    if (!expiredReservations.empty()) {
        updateUsageCounters();
        Log(DEBUG, "ResourceReservationManager", "Cleaned up {} expired reservations", expiredReservations.size());
    }
}

bool ResourceReservationManager::canGrantExclusiveAccess() const {
    // Exclusive access can only be granted if no other reservations exist
    return activeReservations_.empty();
}

void ResourceReservationManager::setExclusiveReservation(bool exclusive) {
    hasExclusiveReservation_ = exclusive;
    if (exclusive) {
        Log(DEBUG, "ResourceReservationManager", "Exclusive access granted");
    } else {
        Log(DEBUG, "ResourceReservationManager", "Exclusive access released");
    }
}

int ResourceReservationManager::estimateMaxConcurrent(
    const OperationRequirements &requirements,
    const SystemMetrics &metrics) const {
  // Use internal baseline availability minus our reservations
  uint64_t reservedRAM = totalReservedRAM_.load();
  uint64_t reservedVRAM = totalReservedVRAM_.load();
  int reservedCores = totalReservedCores_.load();

  uint64_t baseAvailRAM = baselineAvailableRAM_.load();
  uint64_t baseAvailVRAM = baselineAvailableVRAM_.load();
  if (baseAvailRAM == 0 || baseAvailVRAM == 0) {
    baseAvailRAM = metrics.availableRAM;
    baseAvailVRAM = metrics.availableVRAM;
  }

  uint64_t usableRAM =
      (baseAvailRAM > reservedRAM) ? (baseAvailRAM - reservedRAM) : 0;
  uint64_t usableVRAM =
      (baseAvailVRAM > reservedVRAM) ? (baseAvailVRAM - reservedVRAM) : 0;
  int usableCores = (metrics.availableCores > reservedCores)
                        ? (metrics.availableCores - reservedCores)
                        : 0;

  // Apply global budgets
  usableRAM = static_cast<uint64_t>(usableRAM * PIE4K_CFG.MaxRAM);
  usableVRAM = static_cast<uint64_t>(usableVRAM * PIE4K_CFG.MaxVRAM);
  int coreBudget = static_cast<int>(usableCores); // no extra margin

  int byRam =
      requirements.estimatedMemoryUsage > 0
          ? static_cast<int>(usableRAM / requirements.estimatedMemoryUsage)
          : coreBudget;
  int byVram =
      requirements.estimatedVRAMUsage > 0
          ? static_cast<int>(usableVRAM / requirements.estimatedVRAMUsage)
          : coreBudget;
  int byCores =
      requirements.estimatedCPUCores > 0
          ? static_cast<int>(coreBudget / requirements.estimatedCPUCores)
          : coreBudget;

  int maxConc = std::max(0, std::min({byRam, byVram, byCores}));
  Log(DEBUG, "ResourceReservationManager",
      "estimateMaxConcurrent: usableRAM={}MB usableVRAM={}MB cores={} -> "
      "max={}",
      usableRAM / (1024 * 1024), usableVRAM / (1024 * 1024), coreBudget,
      maxConc);
  return maxConc;
}

} // namespace ProjectIE4k
