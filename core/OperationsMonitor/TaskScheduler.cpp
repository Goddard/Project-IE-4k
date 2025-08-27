#include "TaskScheduler.h"
#include "core/CFG.h"
#include "core/Logging/Logging.h"
#include <algorithm>
#include <optional>
#include <thread>

namespace ProjectIE4k {

TaskScheduler::TaskScheduler() {
    // Initialize with hardware concurrency
    size_t hwThreads = std::thread::hardware_concurrency();
    if (hwThreads == 0) hwThreads = 4; // Fallback

    // Allow up to 4x hardware threads
    maxThreads_ = hwThreads * 4;
    
    Log(DEBUG, "TaskScheduler", "TaskScheduler created with max threads: {}", maxThreads_.load());
}

TaskScheduler::~TaskScheduler() {
    shutdown();
}

void TaskScheduler::initialize(std::shared_ptr<ResourceMonitor> resourceMonitor, std::shared_ptr<PerformanceProfiler> performanceProfiler) {
    if (initialized_) return;
    
    resourceMonitor_ = resourceMonitor;
    performanceProfiler_ = performanceProfiler;
    
    if (!resourceMonitor_) {
        Log(ERROR, "TaskScheduler", "Cannot initialize without ResourceMonitor");
        return;
    }

    // Start with hardware concurrency for better initial performance
    size_t startingThreads = std::thread::hardware_concurrency() * 4;
    if (startingThreads == 0)
      startingThreads = 4; // Fallback

    Log(MESSAGE, "TaskScheduler",
        "Initializing TaskScheduler with {} starting threads (max: {})",
        startingThreads, maxThreads_.load());

    // Start worker threads
    for (size_t i = 0; i < startingThreads; ++i) {
        workerThreads_.emplace_back(&TaskScheduler::workerThread, this);
    }
    
    initialized_ = true;
    Log(MESSAGE, "TaskScheduler", "TaskScheduler initialized successfully");
}

void TaskScheduler::shutdown() {
    if (!initialized_ || shutdown_) return;
    
    Log(MESSAGE, "TaskScheduler", "Shutting down TaskScheduler");
    
    shutdown_ = true;
    taskCondition_.notify_all();
    
    // Wait for all worker threads to finish
    for (auto& thread : workerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    workerThreads_.clear();
    
    Log(MESSAGE, "TaskScheduler", "TaskScheduler shutdown complete");
}

void TaskScheduler::setMaxThreads(size_t maxThreads) {
    size_t oldMax = maxThreads_.exchange(maxThreads);
    Log(DEBUG, "TaskScheduler", "Max threads changed from {} to {}", oldMax, maxThreads);
    
    // Implement dynamic thread scaling
    std::lock_guard<std::mutex> lock(taskQueueMutex_);
    
    size_t currentThreads = workerThreads_.size();
    
    if (maxThreads > currentThreads) {
        // Scale up - add more worker threads
        size_t threadsToAdd = maxThreads - currentThreads;
        Log(MESSAGE, "TaskScheduler", "Scaling up: adding {} worker threads (current: {}, target: {})", 
            threadsToAdd, currentThreads, maxThreads);
        
        for (size_t i = 0; i < threadsToAdd; ++i) {
            workerThreads_.emplace_back(&TaskScheduler::workerThread, this);
        }
    } else if (maxThreads < currentThreads && !shutdown_) {
      // TODO: add scale down logic
      Log(MESSAGE, "TaskScheduler",
          "Scale down requested: target {} threads (current: {})", maxThreads,
          currentThreads);
    }
}

size_t TaskScheduler::getQueuedTasks() const {
    std::lock_guard<std::mutex> lock(taskQueueMutex_);
    return taskQueue_.size();
}

void TaskScheduler::waitForCriticalTasks() {
    while (criticalTasks_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void TaskScheduler::waitForExclusiveTasks() {
    while (exclusiveTasks_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void TaskScheduler::workerThread() {
    Log(DEBUG, "TaskScheduler", "Worker thread started");

    while (!shutdown_) {
        std::optional<ScheduledTask> taskOpt;
        
        {
            std::unique_lock<std::mutex> lock(taskQueueMutex_);
            taskCondition_.wait(lock, [this] { return shutdown_ || !taskQueue_.empty(); });
            
            if (shutdown_) break;
            
            if (!taskQueue_.empty()) {
                // Copy the top task (can't move from const reference) - but avoid default initialization
                ScheduledTask candidateTask = taskQueue_.top();

                // Check if we can execute this task using the sampled (cached)
                SystemMetrics metrics = resourceMonitor_->getCurrentMetrics();
                if (canExecuteTask(candidateTask, metrics)) {
                    taskQueue_.pop();
                    taskOpt = std::move(candidateTask);
                    Log(DEBUG, "TaskScheduler", "Task {} ready for execution", taskOpt->taskId);
                } else {
                    Log(DEBUG, "TaskScheduler", "Task {} cannot execute due to insufficient resources. Waiting.", candidateTask.taskId);
                    taskCondition_.wait_for(lock, std::chrono::milliseconds(100));
                    continue;
                }
            }
        }
        
        if (taskOpt.has_value()) {
            executeTask(std::move(taskOpt.value()));
        }
    }
    
    Log(DEBUG, "TaskScheduler", "Worker thread ended");
}

bool TaskScheduler::canExecuteTask(const ScheduledTask& task, const SystemMetrics& metrics) {
  Log(DEBUG, "TaskScheduler",
      "Checking if task {} can execute: activeThreads={}, maxThreads={}, "
      "access={}, priority={}",
      task.taskId, activeThreads_.load(), maxThreads_.load(),
      (task.requirements.resourceAccess == ResourceAccess::EXCLUSIVE
           ? "EXCLUSIVE"
       : task.requirements.resourceAccess == ResourceAccess::RESERVED
           ? "RESERVED"
           : "SHARED"),
      static_cast<int>(task.requirements.priority));

  // Check if we have reached maximum concurrent tasks
  if (activeThreads_ >= maxThreads_) {
    Log(DEBUG, "TaskScheduler",
        "Task {} rejected: activeThreads ({}) >= maxThreads ({})", task.taskId,
        activeThreads_.load(), maxThreads_.load());
    return false;
  }

    // Handle exclusive tasks
    if (task.requirements.resourceAccess == ResourceAccess::EXCLUSIVE) {
        // For exclusive tasks, no other tasks should be running
        if (activeThreads_ > 0) {
            Log(DEBUG, "TaskScheduler", "Task {} rejected: exclusive task but {} other tasks active", 
                task.taskId, activeThreads_.load());
            return false;
        }
    }

    // If resources were RESERVED, OperationsMonitor has already performed a
    // reservation against the ResourceReservationManager using internal
    // availability. Avoid double-blocking based on ResourceMonitor
    // instantaneous metrics here.
    if (task.requirements.resourceAccess == ResourceAccess::RESERVED) {
      Log(DEBUG, "TaskScheduler",
          "Task {} approved (RESERVED) - skipping ResourceMonitor-based checks",
          task.taskId);
      return true;
    }

    // For SHARED tasks, check basic resource availability using live metrics
    // Check CPU usage - don't start new tasks if CPU is overloaded
    if (metrics.cpuUsagePercent > PIE4K_CFG.MaxCPU) {
      Log(DEBUG, "TaskScheduler",
          "Task {} rejected: CPU usage too high ({:.1f}%)", task.taskId,
          metrics.cpuUsagePercent);
      return false;
    }

    // Check if we have basic memory available
    if (task.requirements.estimatedMemoryUsage > 0) {
      if (task.requirements.estimatedMemoryUsage >
          metrics.availableRAM * PIE4K_CFG.MaxRAM) {
        Log(DEBUG, "TaskScheduler",
            "Task {} rejected: insufficient RAM (need {}MB, available {}MB)",
            task.taskId, task.requirements.estimatedMemoryUsage / (1024 * 1024),
            (metrics.availableRAM * PIE4K_CFG.MaxRAM) / (1024 * 1024));
        return false;
      }
    }
    
    // Check if we have basic VRAM available (if GPU task)
    if (task.requirements.estimatedVRAMUsage > 0 && metrics.totalVRAM > 0) {
      if (task.requirements.estimatedVRAMUsage >
          metrics.availableVRAM * PIE4K_CFG.MaxVRAM) {
        Log(DEBUG, "TaskScheduler",
            "Task {} rejected: insufficient VRAM (need {}MB, available {}MB)",
            task.taskId, task.requirements.estimatedVRAMUsage / (1024 * 1024),
            (metrics.availableVRAM * PIE4K_CFG.MaxVRAM) / (1024 * 1024));
        return false;
      }
    }
    
    Log(DEBUG, "TaskScheduler", "Task {} can execute: resources available", task.taskId);
    return true;
}

void TaskScheduler::executeTask(ScheduledTask task) {
    activeThreads_++;
    
    auto startTime = std::chrono::steady_clock::now();
    SystemMetrics startMetrics;
    if (performanceProfiler_ && resourceMonitor_) {
        startMetrics = resourceMonitor_->getFreshMetrics(); // Force fresh metrics before task
    }
    
    Log(DEBUG, "TaskScheduler", "Executing task {} (type: {}, priority: {})", 
        task.taskId, task.requirements.operationType, static_cast<int>(task.requirements.priority));
    
    // For accurate VRAM measurement, sample during execution
    uint64_t peakVRAMUsage = 0;
    uint64_t baselineVRAM = startMetrics.usedVRAM;
    
    try {
        
        // Execute the task with periodic VRAM sampling
        std::atomic<bool> taskCompleted{false};
        std::thread vramSampler;

        if (performanceProfiler_ && resourceMonitor_ &&
            task.requirements.domain == ExecutionDomain::GPU) {
          // Start VRAM sampling thread for upscale operations
          vramSampler = std::thread([&]() {
            while (!taskCompleted.load()) {
              auto currentMetrics = resourceMonitor_->getFreshMetrics();
              uint64_t currentVRAMUsage = currentMetrics.usedVRAM;
              if (currentVRAMUsage > baselineVRAM) {
                peakVRAMUsage =
                    std::max(peakVRAMUsage, currentVRAMUsage - baselineVRAM);
              }
              std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
          });
        }

        // Execute the task
        task.task();
        
        // Stop VRAM sampling
        taskCompleted.store(true);
        if (vramSampler.joinable()) {
            vramSampler.join();
        }
        
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        // Record successful execution with profiling
        if (performanceProfiler_ && resourceMonitor_) {
            auto endMetrics = resourceMonitor_->getFreshMetrics(); // Force fresh metrics after task

            // Use peak VRAM measurement for gpu domain operations, fallback to
            // difference for others
            uint64_t actualVRAMUsed = peakVRAMUsage;
            if (actualVRAMUsed == 0 ||
                task.requirements.domain == ExecutionDomain::GPU) {
              actualVRAMUsed =
                  (endMetrics.usedVRAM > startMetrics.usedVRAM)
                      ? (endMetrics.usedVRAM - startMetrics.usedVRAM)
                      : 0;
            }

            Log(DEBUG, "TaskScheduler", "VRAM measurement for {}: baseline={}MB, peak={}MB, end={}MB, recorded={}MB",
                task.taskId, baselineVRAM / (1024*1024), 
                (baselineVRAM + peakVRAMUsage) / (1024*1024),
                endMetrics.usedVRAM / (1024*1024), 
                actualVRAMUsed / (1024*1024));
            
            Log(DEBUG, "TaskScheduler", "About to record task execution for {}: saveProfile={}", 
                task.requirements.operationType, task.requirements.saveProfile);
            
            performanceProfiler_->recordTaskExecution(
                task.requirements.operationType,
                duration,
                endMetrics.usedRAM - startMetrics.usedRAM,
                actualVRAMUsed,
                endMetrics.cpuUsagePercent,
                true,
                task.requirements.saveProfile
            );
        }
        
        Log(DEBUG, "TaskScheduler", "Task {} completed successfully in {}ms", task.taskId, duration.count());
        
    } catch (const std::exception& e) {
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        // Record failed execution with profiling
        if (performanceProfiler_ && resourceMonitor_) {
            auto endMetrics = resourceMonitor_->getFreshMetrics(); // Force fresh metrics after failed task
            
            // Use peak VRAM measurement for upscale operations, fallback to difference for others
            uint64_t actualVRAMUsed = peakVRAMUsage;
            if (actualVRAMUsed == 0 || task.requirements.operationType.find("upscale") == std::string::npos) {
                actualVRAMUsed = (endMetrics.usedVRAM > startMetrics.usedVRAM) ? 
                    (endMetrics.usedVRAM - startMetrics.usedVRAM) : 0;
            }
            
            performanceProfiler_->recordTaskExecution(
                task.requirements.operationType,
                duration,
                endMetrics.usedRAM - startMetrics.usedRAM,
                actualVRAMUsed,
                endMetrics.cpuUsagePercent,
                false,
                task.requirements.saveProfile
            );
        }
        
        Log(ERROR, "TaskScheduler", "Task {} failed after {}ms: {}", task.taskId, duration.count(), e.what());
        
    } catch (...) {
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        // Record failed execution with profiling
        if (performanceProfiler_ && resourceMonitor_) {
            auto endMetrics = resourceMonitor_->getFreshMetrics(); // Force fresh metrics after failed task
            
            // Use peak VRAM measurement for upscale operations, fallback to difference for others
            uint64_t actualVRAMUsed = peakVRAMUsage;
            if (actualVRAMUsed == 0 ||
                task.requirements.domain == ExecutionDomain::GPU) {
              actualVRAMUsed =
                  (endMetrics.usedVRAM > startMetrics.usedVRAM)
                      ? (endMetrics.usedVRAM - startMetrics.usedVRAM)
                      : 0;
            }

            performanceProfiler_->recordTaskExecution(
                task.requirements.operationType,
                duration,
                endMetrics.usedRAM - startMetrics.usedRAM,
                actualVRAMUsed,
                endMetrics.cpuUsagePercent,
                false,
                task.requirements.saveProfile
            );
        }
        
        Log(ERROR, "TaskScheduler", "Task {} failed after {}ms with unknown exception", task.taskId, duration.count());
    }

    // Release reserved resources if this was a RESERVED task
    if (task.requirements.resourceAccess == ResourceAccess::RESERVED) {
      // We need to access OperationsMonitor to release resources
      // Since TaskScheduler doesn't have direct access, we'll need to handle
      // this differently
      Log(DEBUG, "TaskScheduler",
          "Task {} completed with RESERVED resources - resources should be "
          "released",
          task.taskId);
    }

    // Update task counters
    updateTaskCounters(task.requirements, false);
    
    activeThreads_--;
    
    // Notify other worker threads that resources may be available after task completion
    // This ensures they re-check metrics with fresh resource availability
    taskCondition_.notify_all();
    
    Log(DEBUG, "TaskScheduler", "Task {} completed, notifying waiting threads to re-check resources", task.taskId);
}

std::string TaskScheduler::generateTaskId() {
    return "task_" + std::to_string(++taskIdCounter_);
}

void TaskScheduler::updateTaskCounters(const OperationRequirements& requirements, bool increment) {
    if (requirements.priority == TaskPriority::CRITICAL) {
        if (increment) {
            criticalTasks_++;
        } else {
            criticalTasks_--;
        }
    }
    
    if (requirements.resourceAccess == ResourceAccess::EXCLUSIVE) {
        if (increment) {
            exclusiveTasks_++;
        } else {
            exclusiveTasks_--;
        }
    }
}

} // namespace ProjectIE4k
