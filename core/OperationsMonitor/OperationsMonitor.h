#pragma once

#include <future>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "PerformanceProfiler.h"
#include "ResourceMonitor.h"
#include "ResourceReservation.h"
#include "TaskScheduler.h"

namespace ProjectIE4k {

/**
 * @brief Operations Monitor - help track system resources, performance profile,
 * and scheduling. Use this in your services if you need mutli-threaded tasks
 * accomplished.
 */
class OperationsMonitor {
public:
  OperationsMonitor();
  ~OperationsMonitor();

  // Singleton access (maintains compatibility with existing code)
  static OperationsMonitor &getInstance();

  // Lifecycle management
  void initialize();
  void shutdown();
  bool isInitialized() const;

  // Task submission - maintains exact same interface as original
  template <typename F, typename... Args>
  auto submitTask(F &&f, Args &&...args) -> std::future<decltype(f(args...))>;

  template <typename F>
  auto submitTaskWithRequirements(F &&f,
                                  const OperationRequirements &requirements,
                                  const std::string &taskId = "")
      -> std::future<decltype(f())>;

  // Priority-based task submission
  template <typename F>
  auto submitCriticalTask(F &&f, const std::string &taskId = "")
      -> std::future<decltype(f())>;

  template <typename F>
  auto submitExclusiveTask(F &&f, const std::string &taskId = "")
      -> std::future<decltype(f())>;

  // Resource monitoring - fast cached access
  SystemMetrics getCurrentMetrics() const;
  void updateMetrics(); // Force immediate update if needed

  // Thread pool management
  void setMaxThreads(size_t maxThreads,
                     ExecutionDomain domain = ExecutionDomain::CPU);
  size_t getMaxThreads(ExecutionDomain domain = ExecutionDomain::CPU) const;
  size_t getActiveThreads(ExecutionDomain domain = ExecutionDomain::CPU) const;
  size_t getQueuedTasks(ExecutionDomain domain = ExecutionDomain::CPU) const;

  // Resource reservation (now atomic and race-condition free)
  bool reserveResources(const OperationRequirements &requirements);
  void releaseResources(const OperationRequirements &requirements);

  // Task state queries
  void waitForCriticalTasks();
  bool hasCriticalTasks() const;

  // Resource learning and estimation
  void registerResourceProfile(const std::string &operationType,
                               uint64_t ramUsage, uint64_t vramUsage,
                               double cpuUsage);
  ResourceProfile getResourceProfile(const std::string &operationType) const;

  // Performance statistics
  OperationsPerformanceStats getPerformanceStats() const;

  // Auto-scaling (new feature)
  void enableAutoScaling(bool enabled = true);
  void updateAutoScaling(const std::string &operationType = "upscale",
                         ExecutionDomain domain = ExecutionDomain::CPU);

  // Emergency protection (new feature)
  bool isVRAMCritical() const;
  bool isVRAMDangerous() const;
  void emergencyResourceCleanup();

  // Legacy compatibility helpers
  static uint64_t estimateMemoryForFileSize(uint64_t fileSize,
                                            const std::string &operationType);
  static uint64_t estimateVRAMForOperation(const std::string &operationType,
                                           uint64_t inputSize);
  static int estimateCPUCoresForOperation(const std::string &operationType,
                                          uint64_t inputSize);

  // Component access (for advanced usage)
  std::shared_ptr<ResourceMonitor> getResourceMonitor() const {
    return resourceMonitor_;
  }
  std::shared_ptr<TaskScheduler> getTaskScheduler() const {
    return cpuScheduler_;
  }
  std::shared_ptr<TaskScheduler> getGpuTaskScheduler() const {
    return gpuScheduler_;
  }
  std::shared_ptr<PerformanceProfiler> getPerformanceProfiler() const {
    return performanceProfiler_;
  }

  std::shared_ptr<ResourceReservationManager> getResourceReservation() const {
    return resourceReservation_;
  }

private:
  // Component initialization
  void initializeComponents();
  void shutdownComponents();

  // Task execution wrapper (adds profiling and error handling)
  template <typename F>
  auto wrapTaskWithProfiling(F &&f, const OperationRequirements &requirements,
                             const std::string &taskId)
      -> std::function<decltype(f())()>;

  // State management
  std::atomic<bool> initialized_{false};
  std::atomic<bool> shutdown_{false};

  // Core components
  std::shared_ptr<ResourceMonitor> resourceMonitor_;
  std::shared_ptr<TaskScheduler> cpuScheduler_;
  std::shared_ptr<TaskScheduler> gpuScheduler_;
  std::shared_ptr<ResourceReservationManager> resourceReservation_;
  std::shared_ptr<PerformanceProfiler> performanceProfiler_;

  // Auto-scaling state
  std::atomic<bool> autoScalingEnabled_{false};
  std::chrono::steady_clock::time_point lastScalingUpdate_;
  std::string lastOperationType_;
  std::string lastOperationTypeCPU_;
  std::string lastOperationTypeGPU_;

  // Emergency state tracking
  std::atomic<bool> emergencyMode_{false};
  // Reservation tracking for legacy interface compatibility
  mutable std::mutex legacyReservationsMutex_;
  std::map<std::string, std::string>
      legacyReservations_; // resourceName -> reservationId
};

// Template implementations
template <typename F, typename... Args>
auto OperationsMonitor::submitTask(F &&f, Args &&...args)
    -> std::future<decltype(f(args...))> {
  return getTaskScheduler()->submitTask(std::forward<F>(f), args...);
}

template <typename F>
auto OperationsMonitor::submitTaskWithRequirements(
    F &&f, const OperationRequirements &requirements, const std::string &taskId)
    -> std::future<decltype(f())> {

  // Trigger auto-scaling for new operation types to prevent deadlock
  if (autoScalingEnabled_ && !requirements.operationType.empty()) {
    updateAutoScaling(requirements.operationType, requirements.domain);
  }

  // If this task requires resource reservation, reserve before scheduling
  if (requirements.resourceAccess == ResourceAccess::RESERVED) {
    using ReturnT = std::invoke_result_t<std::decay_t<F>>;

    // Work on a local copy for reservation metadata
    OperationRequirements reqCopy = requirements;

    // Block until we can reserve the resources for this task
    while (!reserveResources(reqCopy)) {
      // Avoid busy spin; refresh metrics and wait briefly
      updateMetrics();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Wrap task to guarantee release on completion or failure
    auto wrapped = [this, reqCopy,
                    func = std::forward<F>(f)]() mutable -> ReturnT {
      try {
        if constexpr (std::is_void_v<ReturnT>) {
          func();
          releaseResources(reqCopy);
          return;
        } else {
          auto r = func();
          releaseResources(reqCopy);
          return r;
        }
      } catch (...) {
        releaseResources(reqCopy);
        throw;
      }
    };

    auto sched = (requirements.domain == ExecutionDomain::GPU)
                     ? getGpuTaskScheduler()
                     : getTaskScheduler();
    return sched->submitTaskWithRequirements(std::move(wrapped), reqCopy,
                                             taskId);
  }

  // Route to CPU or GPU scheduler based on requirements.domain (no reservation)
  auto sched = (requirements.domain == ExecutionDomain::GPU)
                   ? getGpuTaskScheduler()
                   : getTaskScheduler();
  return sched->submitTaskWithRequirements(std::forward<F>(f), requirements,
                                           taskId);
}

template <typename F>
auto OperationsMonitor::submitCriticalTask(F &&f, const std::string &taskId)
    -> std::future<decltype(f())> {
  OperationRequirements requirements;
  requirements.priority = TaskPriority::CRITICAL;
  requirements.operationType = "critical";
  return submitTaskWithRequirements(std::forward<F>(f), requirements, taskId);
}

template <typename F>
auto OperationsMonitor::submitExclusiveTask(F &&f, const std::string &taskId)
    -> std::future<decltype(f())> {
  OperationRequirements requirements;
  requirements.priority = TaskPriority::EXCLUSIVE;
  requirements.resourceAccess = ResourceAccess::EXCLUSIVE;
  requirements.operationType = "exclusive";
  return submitTaskWithRequirements(std::forward<F>(f), requirements, taskId);
}

template <typename F>
auto OperationsMonitor::wrapTaskWithProfiling(
    F &&f, const OperationRequirements &requirements, const std::string &taskId)
    -> std::function<decltype(f())()> {

  // Create a proper copy/move of the function to avoid capture issues
  auto capturedFunction = std::forward<F>(f);
  return [this, capturedFunction = std::move(capturedFunction), requirements,
          taskId]() mutable -> decltype(capturedFunction()) {
    auto startTime = std::chrono::steady_clock::now();
    auto startMetrics = resourceMonitor_->getCurrentMetrics();

    try {
      // Execute the actual task
      if constexpr (std::is_void_v<decltype(capturedFunction())>) {
        capturedFunction();

        // Record successful execution
        auto endTime = std::chrono::steady_clock::now();
        auto endMetrics = resourceMonitor_->getCurrentMetrics();
        auto executionTime =
            std::chrono::duration_cast<std::chrono::milliseconds>(endTime -
                                                                  startTime);

        performanceProfiler_->recordTaskExecution(
            requirements.operationType, executionTime,
            endMetrics.usedRAM - startMetrics.usedRAM,
            endMetrics.usedVRAM - startMetrics.usedVRAM,
            endMetrics.cpuUsagePercent, true, requirements.saveProfile);
      } else {
        auto result = capturedFunction();

        // Record successful execution
        auto endTime = std::chrono::steady_clock::now();
        auto endMetrics = resourceMonitor_->getCurrentMetrics();
        auto executionTime =
            std::chrono::duration_cast<std::chrono::milliseconds>(endTime -
                                                                  startTime);

        performanceProfiler_->recordTaskExecution(
            requirements.operationType, executionTime,
            endMetrics.usedRAM - startMetrics.usedRAM,
            endMetrics.usedVRAM - startMetrics.usedVRAM,
            endMetrics.cpuUsagePercent, true, requirements.saveProfile);

        return result;
      }
    } catch (...) {
      // Record failed execution
      auto endTime = std::chrono::steady_clock::now();
      auto executionTime =
          std::chrono::duration_cast<std::chrono::milliseconds>(endTime -
                                                                startTime);

      performanceProfiler_->recordTaskExecution(requirements.operationType,
                                                executionTime, 0, 0, 0.0, false,
                                                requirements.saveProfile);

      throw; // Re-throw the exception
    }
  };
}

} // namespace ProjectIE4k
