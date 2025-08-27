#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include "ResourceMonitor.h"
#include "TaskScheduler.h"

namespace ProjectIE4k {

/**
 * @brief Resource reservation entry
 */
struct ResourceReservation {
    std::string reservationId;
    OperationRequirements requirements;
    std::chrono::steady_clock::time_point reservedAt;
    std::string taskId;
    
    // Default constructor for std::map compatibility
    ResourceReservation() : reservedAt(std::chrono::steady_clock::now()) {}
    
    ResourceReservation(const std::string& id, const OperationRequirements& req, const std::string& task)
        : reservationId(id), requirements(req), reservedAt(std::chrono::steady_clock::now()), taskId(task) {}
};

/**
 * @brief Efficient resource reservation and tracking system
 * 
 * Manages resource allocation with real-time tracking and prevents overcommitment.
 * Provides fast reservation checks and automatic cleanup.
 */
class ResourceReservationManager {
public:
    ResourceReservationManager();
    ~ResourceReservationManager();
    
    // Lifecycle management
    void initialize(std::shared_ptr<ResourceMonitor> resourceMonitor);
    void shutdown();
    bool isInitialized() const { return initialized_; }
    
    // Resource reservation - fast, atomic operations
    bool tryReserveResources(const OperationRequirements& requirements, const std::string& taskId = "");
    void releaseResources(const std::string& reservationId);
    void releaseResourcesByTaskId(const std::string& taskId);
    
    // Resource availability queries - optimized for hot path
    bool canAllocateResources(const OperationRequirements& requirements) const;
    bool hasExclusiveReservation() const { return hasExclusiveReservation_; }
    
    // Resource usage tracking
    struct ResourceUsage {
        uint64_t reservedRAM = 0;
        uint64_t reservedVRAM = 0;
        int reservedCores = 0;
        size_t activeReservations = 0;
        bool hasExclusive = false;
    };
    
    ResourceUsage getCurrentUsage() const;

    // Estimate how many concurrent tasks with the given requirements can fit
    int estimateMaxConcurrent(const OperationRequirements &requirements,
                              const SystemMetrics &metrics) const;

    // Emergency resource management
    void emergencyCleanup(); // Clean up stale reservations
    void forceReleaseAll(); // Emergency release all reservations
    
    // Statistics and monitoring
    size_t getActiveReservationCount() const;
    std::chrono::milliseconds getAverageReservationDuration() const;
    
private:
    // Core reservation logic
    bool checkResourceAvailability(const OperationRequirements& requirements, const SystemMetrics& metrics) const;
    std::string generateReservationId();
    void updateUsageCounters();
    void cleanupExpiredReservations();
    
    // Exclusive access management
    bool canGrantExclusiveAccess() const;
    void setExclusiveReservation(bool exclusive);
    
    // State management
    std::atomic<bool> initialized_{false};
    std::atomic<bool> shutdown_{false};
    
    // Resource monitoring
    std::shared_ptr<ResourceMonitor> resourceMonitor_;
    
    // Reservation storage - optimized for fast lookup
    mutable std::mutex reservationsMutex_;
    std::map<std::string, ResourceReservation> activeReservations_;
    std::map<std::string, std::unordered_set<std::string>> taskToReservations_; // taskId -> reservationIds
    
    // Fast access counters (avoid recalculating on every check)
    std::atomic<uint64_t> totalReservedRAM_{0};
    std::atomic<uint64_t> totalReservedVRAM_{0};
    std::atomic<int> totalReservedCores_{0};
    std::atomic<bool> hasExclusiveReservation_{false};
    
    // Reservation ID generation
    std::atomic<uint64_t> reservationIdCounter_{0};
    
    // Performance tracking
    mutable std::mutex statsMutex_;
    std::vector<std::chrono::milliseconds> recentReservationDurations_;
    std::chrono::steady_clock::time_point lastCleanup_;

    // Config: reservation timeout for cleanup
    static constexpr std::chrono::minutes RESERVATION_TIMEOUT{5};

    // Internal baseline available resources captured at initialization or first
    // reservation
    std::atomic<uint64_t> baselineAvailableVRAM_{0};
    std::atomic<uint64_t> baselineAvailableRAM_{0};
};

} // namespace ProjectIE4k
