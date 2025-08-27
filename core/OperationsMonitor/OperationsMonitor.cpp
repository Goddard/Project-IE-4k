#include "OperationsMonitor.h"

#include <algorithm>

#include "core/Logging/Logging.h"

namespace ProjectIE4k {

// Singleton instance
static std::unique_ptr<OperationsMonitor> g_instance = nullptr;
static std::mutex g_instanceMutex;

OperationsMonitor::OperationsMonitor() {
  Log(DEBUG, "OperationsMonitor", "OperationsMonitor created");
}

OperationsMonitor::~OperationsMonitor() { shutdown(); }

OperationsMonitor &OperationsMonitor::getInstance() {
  std::lock_guard<std::mutex> lock(g_instanceMutex);
  if (!g_instance) {
    g_instance = std::make_unique<OperationsMonitor>();
  }
  return *g_instance;
}

void OperationsMonitor::initialize() {
  if (initialized_)
    return;

  Log(MESSAGE, "OperationsMonitor", "Initializing OperationsMonitor");

  try {
    initializeComponents();
    initialized_ = true;
    Log(MESSAGE, "OperationsMonitor",
        "OperationsMonitor initialized successfully");
  } catch (const std::exception &e) {
    Log(ERROR, "OperationsMonitor", "Failed to initialize: {}", e.what());
    shutdownComponents();
    throw;
  }
}

void OperationsMonitor::shutdown() {
  if (!initialized_ || shutdown_)
    return;

  Log(MESSAGE, "OperationsMonitor", "Shutting down OperationsMonitor");

  shutdown_ = true;
  shutdownComponents();

  Log(MESSAGE, "OperationsMonitor", "OperationsMonitor shutdown complete");
}

bool OperationsMonitor::isInitialized() const {
  return initialized_ && !shutdown_;
}

SystemMetrics OperationsMonitor::getCurrentMetrics() const {
  if (!resourceMonitor_) {
    Log(WARNING, "OperationsMonitor", "ResourceMonitor not available");
    return SystemMetrics{};
  }
  return resourceMonitor_->getCurrentMetrics();
}

void OperationsMonitor::updateMetrics() {
  if (!resourceMonitor_) {
    Log(WARNING, "OperationsMonitor", "ResourceMonitor not available");
    return;
  }
  resourceMonitor_->getFreshMetrics();
}

void OperationsMonitor::setMaxThreads(size_t maxThreads,
                                      ExecutionDomain domain) {
  auto sched = (domain == ExecutionDomain::GPU) ? gpuScheduler_ : cpuScheduler_;
  if (sched)
    sched->setMaxThreads(maxThreads);
}

size_t OperationsMonitor::getMaxThreads(ExecutionDomain domain) const {
  auto sched = (domain == ExecutionDomain::GPU) ? gpuScheduler_ : cpuScheduler_;
  return sched ? sched->getMaxThreads() : 0;
}

size_t OperationsMonitor::getActiveThreads(ExecutionDomain domain) const {
  auto sched = (domain == ExecutionDomain::GPU) ? gpuScheduler_ : cpuScheduler_;
  return sched ? sched->getActiveThreads() : 0;
}

size_t OperationsMonitor::getQueuedTasks(ExecutionDomain domain) const {
  auto sched = (domain == ExecutionDomain::GPU) ? gpuScheduler_ : cpuScheduler_;
  return sched ? sched->getQueuedTasks() : 0;
}

bool OperationsMonitor::reserveResources(
    const OperationRequirements &requirements) {
  if (!resourceReservation_) {
    Log(WARNING, "OperationsMonitor",
        "ResourceReservationManager not available");
    return false;
  }

  // Generate a unique task ID for this reservation
  std::string taskId =
      "taskid_" +
      std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count());

  bool success =
      resourceReservation_->tryReserveResources(requirements, taskId);

  if (success) {
    // Track for legacy interface compatibility
    std::lock_guard<std::mutex> lock(legacyReservationsMutex_);
    legacyReservations_[requirements.resourceName] = taskId;
  }

  return success;
}

void OperationsMonitor::releaseResources(
    const OperationRequirements &requirements) {
  if (!resourceReservation_) {
    Log(WARNING, "OperationsMonitor",
        "ResourceReservationManager not available");
    return;
  }

  // Find the reservation ID for this resource name
  std::string taskId;
  {
    std::lock_guard<std::mutex> lock(legacyReservationsMutex_);
    auto it = legacyReservations_.find(requirements.resourceName);
    if (it != legacyReservations_.end()) {
      taskId = it->second;
      legacyReservations_.erase(it);
    }
  }

  if (!taskId.empty()) {
    resourceReservation_->releaseResourcesByTaskId(taskId);
  }
}

void OperationsMonitor::waitForCriticalTasks() {
  if (cpuScheduler_)
    cpuScheduler_->waitForCriticalTasks();
  if (gpuScheduler_)
    gpuScheduler_->waitForCriticalTasks();
}

bool OperationsMonitor::hasCriticalTasks() const {
  bool cpu = cpuScheduler_ ? cpuScheduler_->hasCriticalTasks() : false;
  bool gpu = gpuScheduler_ ? gpuScheduler_->hasCriticalTasks() : false;
  return cpu || gpu;
}

void OperationsMonitor::registerResourceProfile(
    const std::string &operationType, uint64_t ramUsage, uint64_t vramUsage,
    double cpuUsage) {
  if (performanceProfiler_) {
    performanceProfiler_->registerResourceProfile(operationType, ramUsage,
                                                  vramUsage, cpuUsage);
  }
}

ResourceProfile
OperationsMonitor::getResourceProfile(const std::string &operationType) const {
  if (performanceProfiler_) {
    return performanceProfiler_->getResourceProfile(operationType);
  }
  return ResourceProfile{};
}

// Performance recording only - concurrency control removed

OperationsPerformanceStats OperationsMonitor::getPerformanceStats() const {
  if (performanceProfiler_) {
    return performanceProfiler_->getPerformanceStats();
  }
  return OperationsPerformanceStats{};
}

void OperationsMonitor::enableAutoScaling(bool enabled) {
  autoScalingEnabled_ = enabled;
  if (enabled) {
    lastScalingUpdate_ = std::chrono::steady_clock::now();
    Log(MESSAGE, "OperationsMonitor", "Auto-scaling enabled");
    // Trigger initial scaling update
    updateAutoScaling();
  } else {
    Log(MESSAGE, "OperationsMonitor", "Auto-scaling disabled");
  }
}

void OperationsMonitor::updateAutoScaling(const std::string &operationType,
                                          ExecutionDomain domain) {
  auto sched = (domain == ExecutionDomain::GPU) ? gpuScheduler_ : cpuScheduler_;
  if (!autoScalingEnabled_ || !sched || !performanceProfiler_ ||
      !resourceMonitor_) {
    return;
  }

  // Throttle scaling updates to avoid thrashing (minimum 5 second intervals)
  auto now = std::chrono::steady_clock::now();
  auto timeSinceLastUpdate =
      std::chrono::duration_cast<std::chrono::seconds>(now - lastScalingUpdate_)
          .count();
  // Per-domain last op name
  std::string &lastOp = (domain == ExecutionDomain::GPU)
                            ? lastOperationTypeGPU_
                            : lastOperationTypeCPU_;
  if (timeSinceLastUpdate < 5 && !lastOp.empty() && operationType == lastOp) {
    return;
  }

  lastScalingUpdate_ = now;
  lastOp = operationType;

  // Keep updateAutoScaling lightweight; reservation-based scaling happens at
  // submit If desired later, we can add heuristic-based scaling from
  // PerformanceProfiler here.
}

bool OperationsMonitor::isVRAMCritical() const {
  if (resourceMonitor_) {
    return resourceMonitor_->isVRAMCritical();
  }
  return false;
}

bool OperationsMonitor::isVRAMDangerous() const {
  if (resourceMonitor_) {
    return resourceMonitor_->isVRAMDangerous();
  }
  return false;
}

void OperationsMonitor::emergencyResourceCleanup() {
  Log(WARNING, "OperationsMonitor", "Performing emergency resource cleanup");

  emergencyMode_ = true;

  if (resourceReservation_) {
    resourceReservation_->emergencyCleanup();
  }

  // Force garbage collection if possible
  // TODO :: might need more complete clean up

  emergencyMode_ = false;

  Log(MESSAGE, "OperationsMonitor", "Emergency resource cleanup completed");
}

// Legacy compatibility helpers
uint64_t
OperationsMonitor::estimateMemoryForFileSize(uint64_t fileSize,
                                             const std::string &operationType) {
  // Simple heuristic based on operation type
  if (operationType.find("upscale") != std::string::npos) {
    return std::max(static_cast<uint64_t>(2ULL * 1024 * 1024 * 1024),
                    fileSize * 4); // 2GB or 4x file size
  } else if (operationType.find("compress") != std::string::npos) {
    return std::max(static_cast<uint64_t>(1ULL * 1024 * 1024 * 1024),
                    fileSize * 2); // 1GB or 2x file size
  } else {
    return std::max(static_cast<uint64_t>(512ULL * 1024 * 1024),
                    fileSize); // 512MB or file size
  }
}

uint64_t
OperationsMonitor::estimateVRAMForOperation(const std::string &operationType,
                                            uint64_t inputSize) {
  // VRAM estimation based on operation type and input size
  if (operationType.find("upscale") != std::string::npos) {
    return std::max(static_cast<uint64_t>(1ULL * 1024 * 1024 * 1024),
                    inputSize * 6); // 1GB or 6x input size
  } else if (operationType.find("compress") != std::string::npos) {
    return std::max(static_cast<uint64_t>(512ULL * 1024 * 1024),
                    inputSize * 4); // 512MB or 4x input size
  } else {
    return std::max(static_cast<uint64_t>(256ULL * 1024 * 1024),
                    inputSize * 2); // 256MB or 2x input size
  }
}

int OperationsMonitor::estimateCPUCoresForOperation(
    const std::string &operationType, uint64_t inputSize) {
  if (operationType.find("compress") != std::string::npos) {
    return std::min(
        8,
        std::max(2, static_cast<int>(std::thread::hardware_concurrency() / 2)));
  } else if (operationType.find("parallel") != std::string::npos) {
    return std::min(4, static_cast<int>(std::thread::hardware_concurrency()));
  } else {
    return 1;
  }
}

void OperationsMonitor::initializeComponents() {
  Log(DEBUG, "OperationsMonitor", "Initializing components");

  // Initialize ResourceMonitor first (others depend on it)
  resourceMonitor_ = std::make_shared<ResourceMonitor>();
  resourceMonitor_->initialize();

  // Set up VRAM emergency callback
  resourceMonitor_->setMetricsCallback([this](const SystemMetrics &metrics) {
    if (metrics.vramUsagePercent > 95.0) {
      Log(WARNING, "OperationsMonitor", "VRAM usage critical: {:.1f}%",
          metrics.vramUsagePercent);
      // COMMENTED OUT FOR TESTING - Emergency thread cleanup disabled
      // if (!emergencyMode_) {
      //   emergencyResourceCleanup();
      // }
    }
  });

  // Initialize PerformanceProfiler with ResourceMonitor
  performanceProfiler_ = std::make_shared<PerformanceProfiler>();
  performanceProfiler_->initialize(resourceMonitor_);

  // Try to load existing profiles
  try {
    performanceProfiler_->loadProfilesFromFile("performance_profiles.csv");
  } catch (const std::exception &e) {
    Log(DEBUG, "OperationsMonitor", "Could not load performance profiles: {}",
        e.what());
  }

  // Enable dynamic scaling - start with 1 thread for new operations, scale
  // based on observed usage
  enableAutoScaling(true);

  // Initialize ResourceReservationManager
  resourceReservation_ = std::make_shared<ResourceReservationManager>();
  resourceReservation_->initialize(resourceMonitor_);

  // Initialize separate CPU and GPU schedulers
  cpuScheduler_ = std::make_shared<TaskScheduler>();
  cpuScheduler_->initialize(resourceMonitor_, performanceProfiler_);
  gpuScheduler_ = std::make_shared<TaskScheduler>();
  gpuScheduler_->initialize(resourceMonitor_, performanceProfiler_);

  // Performance recording only - concurrency control removed

  Log(DEBUG, "OperationsMonitor",
      "All components initialized successfully with dynamic scaling enabled");
}

void OperationsMonitor::shutdownComponents() {
  Log(DEBUG, "OperationsMonitor", "Shutting down components");

  // Save performance profiles before shutdown
  if (performanceProfiler_) {
    try {
      performanceProfiler_->saveProfilesToFile("performance_profiles.csv");
    } catch (const std::exception &e) {
      Log(WARNING, "OperationsMonitor",
          "Could not save performance profiles: {}", e.what());
    }
  }

  // Shutdown in reverse order
  if (cpuScheduler_) {
    cpuScheduler_->shutdown();
    cpuScheduler_.reset();
  }
  if (gpuScheduler_) {
    gpuScheduler_->shutdown();
    gpuScheduler_.reset();
  }

  if (resourceReservation_) {
    resourceReservation_->shutdown();
    resourceReservation_.reset();
  }

  if (performanceProfiler_) {
    performanceProfiler_->shutdown();
    performanceProfiler_.reset();
  }

  if (resourceMonitor_) {
    resourceMonitor_->shutdown();
    resourceMonitor_.reset();
  }

  // Clear legacy reservations
  {
    std::lock_guard<std::mutex> lock(legacyReservationsMutex_);
    legacyReservations_.clear();
  }

  Log(DEBUG, "OperationsMonitor", "All components shutdown successfully");
}

} // namespace ProjectIE4k
