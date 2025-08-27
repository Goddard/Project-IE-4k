#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "ResourceMonitor.h"
#include "PerformanceProfiler.h"
#include "core/Logging/Logging.h"

namespace ProjectIE4k {

/**
 * @brief Execution domain for tasks, GPU tasks can still consume cpu, or ram so
 * still need to be careful
 */
enum class ExecutionDomain { CPU, GPU };

/**
 * @brief Task priority levels
 */
enum class TaskPriority {
  LOW = 0,
  NORMAL = 50,
  HIGH = 100,
  CRITICAL = 200,
  // Gets exclusive access to all resources
  EXCLUSIVE = 1000
};

/**
 * @brief Resource access mode
 */
enum class ResourceAccess {
  // Share resources with other tasks
  SHARED,
  // Reserve specific amount of resources
  RESERVED,
  // Get exclusive access to all available resources
  EXCLUSIVE
};

/**
 * @brief Operation requirements and constraints
 */
struct OperationRequirements {
  // Estimated RAM usage in bytes
  uint64_t estimatedMemoryUsage = 0;
  // Estimated VRAM usage in bytes
  uint64_t estimatedVRAMUsage = 0;
  // Estimated disk I/O in bytes
  uint64_t estimatedDiskIO = 0;
  // Estimated CPU cores needed
  int estimatedCPUCores = 1;
  // Starting thread count
  int startingThreadCount = 1;
  TaskPriority priority = TaskPriority::NORMAL;
  ResourceAccess resourceAccess = ResourceAccess::SHARED;
  // Routing hint for multi-pool scheduling
  ExecutionDomain domain = ExecutionDomain::CPU;
  // Type of operation for logging
  std::string operationType;
  // Resource name being processed
  std::string resourceName;
  // Whether this task blocks other tasks
  bool blocking = false;
  // Whether to save a performance profile for this task
  bool saveProfile = true;
  // If this is a parent thread with nested children recommended only when doing
  // nested cpu/gpu tasks
  bool hasChildren = false;
};

/**
 * @brief Internal task representation
 */
struct ScheduledTask {
    std::function<void()> task;
    OperationRequirements requirements;
    std::chrono::steady_clock::time_point submittedTime;
    std::string taskId;
    
    ScheduledTask(std::function<void()> t, const OperationRequirements& req, const std::string& id)
        : task(std::move(t)), requirements(req), submittedTime(std::chrono::steady_clock::now()), taskId(id) {}
};

/**
 * @brief High-performance task scheduler with resource awareness
 *
 * Focuses on efficient task queuing, priority management, and scheduling.
 */
class TaskScheduler {
public:
    TaskScheduler();
    ~TaskScheduler();
    
    // Lifecycle management
    void initialize(std::shared_ptr<ResourceMonitor> resourceMonitor, std::shared_ptr<PerformanceProfiler> performanceProfiler = nullptr);
    void shutdown();
    bool isInitialized() const { return initialized_; }
    
    // Task submission - returns future for result
    template<typename F, typename... Args>
    auto submitTask(F&& f, Args&&... args) -> std::future<decltype(f(args...))>;

    template <typename F>
    auto submitTaskWithRequirements(F &&f,
                                    const OperationRequirements &requirements,
                                    const std::string &taskId = "")
        -> std::future<decltype(f())>;

    // Priority-based task submission
    template<typename F>
    auto submitCriticalTask(F&& f, const std::string& taskId = "") -> std::future<decltype(f())>;
    
    template<typename F>
    auto submitExclusiveTask(F&& f, const std::string& taskId = "") -> std::future<decltype(f())>;
    
    // Thread pool management
    void setMaxThreads(size_t maxThreads);
    size_t getMaxThreads() const { return maxThreads_; }
    size_t getActiveThreads() const { return activeThreads_; }
    size_t getQueuedTasks() const;

    // Task state queries
    bool hasCriticalTasks() const { return criticalTasks_ > 0; }
    bool hasExclusiveTasks() const { return exclusiveTasks_ > 0; }
    
    // Wait for specific task types
    void waitForCriticalTasks();
    void waitForExclusiveTasks();
    
private:
    // Core scheduling logic
    void workerThread();
    void scheduleNextTask();
    bool canExecuteTask(const ScheduledTask& task, const SystemMetrics& metrics);
    void executeTask(ScheduledTask task);
    
    // Task management
    std::string generateTaskId();
    void updateTaskCounters(const OperationRequirements& requirements, bool increment);
    
    // Priority queue comparator
    struct TaskComparator {
        bool operator()(const ScheduledTask& a, const ScheduledTask& b) const {
            // Higher priority first, then FIFO for same priority
            if (static_cast<int>(a.requirements.priority) != static_cast<int>(b.requirements.priority)) {
                return static_cast<int>(a.requirements.priority) < static_cast<int>(b.requirements.priority);
            }
            return a.submittedTime > b.submittedTime;
        }
    };
    
    // State management
    std::atomic<bool> initialized_{false};
    std::atomic<bool> shutdown_{false};
    
    // Resource monitoring and profiling
    std::shared_ptr<ResourceMonitor> resourceMonitor_;
    std::shared_ptr<PerformanceProfiler> performanceProfiler_;
    
    // Thread pool
    std::vector<std::thread> workerThreads_;
    std::atomic<size_t> activeThreads_{0};
    std::atomic<size_t> maxThreads_{0};
    
    // Task queue
    std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, TaskComparator> taskQueue_;
    mutable std::mutex taskQueueMutex_;
    std::condition_variable taskCondition_;
    
    // Task counters
    std::atomic<size_t> criticalTasks_{0};
    std::atomic<size_t> exclusiveTasks_{0};
    
    // Task ID generation
    std::atomic<uint64_t> taskIdCounter_{0};
};

// Template implementations
template<typename F, typename... Args>
auto TaskScheduler::submitTask(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
    OperationRequirements requirements;
    requirements.operationType = "generic";
    return submitTaskWithRequirements([f = std::forward<F>(f), args...]() mutable {
        return f(args...);
    }, requirements);
}

template<typename F>
auto TaskScheduler::submitTaskWithRequirements(F&& f, const OperationRequirements& requirements, const std::string& taskId)
    -> std::future<decltype(f())> {
    
    using ReturnType = decltype(f());
    auto taskPromise = std::make_shared<std::promise<ReturnType>>();
    auto future = taskPromise->get_future();
    
    std::string actualTaskId = taskId.empty() ? generateTaskId() : taskId;

    Log(DEBUG, "TaskScheduler", "Creating task: id='{}', type='{}', priority={}", 
        actualTaskId, requirements.operationType, static_cast<int>(requirements.priority));
    
    // Create a proper copy/move of the function to avoid capture issues
    auto capturedFunction = std::forward<F>(f);
    auto wrappedTask = [taskPromise, capturedFunction = std::move(capturedFunction), requirements, actualTaskId]() mutable {
        try {
            if constexpr (std::is_void_v<ReturnType>) {
                capturedFunction();
                taskPromise->set_value();
            } else {
                auto result = capturedFunction();
                taskPromise->set_value(std::move(result));
            }
        } catch (...) {
            taskPromise->set_exception(std::current_exception());
        }
    };

    // Scale up thread pool if task requires more threads than currently
    // available
    if (requirements.startingThreadCount > maxThreads_.load()) {
      Log(MESSAGE, "TaskScheduler",
          "Scaling up thread pool from {} to {} threads for task {}",
          maxThreads_.load(), requirements.startingThreadCount, actualTaskId);
      setMaxThreads(requirements.startingThreadCount);
    }

    {
        std::lock_guard<std::mutex> lock(taskQueueMutex_);

        Log(DEBUG, "TaskScheduler",
            "Queueing task: id='{}', type='{}', priority={}, saveProfile={}, "
            "startingThreads={}",
            actualTaskId, requirements.operationType,
            static_cast<int>(requirements.priority), requirements.saveProfile,
            requirements.startingThreadCount);

        taskQueue_.emplace(std::move(wrappedTask), requirements, actualTaskId);
        updateTaskCounters(requirements, true);
    }
    
    taskCondition_.notify_one();
    return future;
}

template<typename F>
auto TaskScheduler::submitCriticalTask(F&& f, const std::string& taskId) -> std::future<decltype(f())> {
    OperationRequirements requirements;
    requirements.priority = TaskPriority::CRITICAL;
    requirements.operationType = "critical";

    return submitTaskWithRequirements(std::forward<F>(f), requirements, taskId);
}

template<typename F>
auto TaskScheduler::submitExclusiveTask(F&& f, const std::string& taskId) -> std::future<decltype(f())> {
    OperationRequirements requirements;
    requirements.priority = TaskPriority::EXCLUSIVE;
    requirements.resourceAccess = ResourceAccess::EXCLUSIVE;
    requirements.operationType = "exclusive";

    return submitTaskWithRequirements(std::forward<F>(f), requirements, taskId);
}

} // namespace ProjectIE4k
