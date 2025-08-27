#include "NcnnAllocator.h"

#include <chrono>
#include <thread>
#include <functional>

#include "core/Logging/Logger.h"

namespace ProjectIE4k {

NcnnAllocator::NcnnAllocator()
    : defaultAllocator_(std::make_unique<ncnn::PoolAllocator>()) {
  Log(DEBUG, "NcnnAllocator", "Initialized with default PoolAllocator");
}

NcnnAllocator::~NcnnAllocator() {
  Log(DEBUG, "NcnnAllocator", "Destructor called - forcing cleanup");
  forceCleanup();
  Log(DEBUG, "NcnnAllocator", "Destructor complete");
}

void *NcnnAllocator::fastMalloc(size_t size) {
  void *ptr = defaultAllocator_->fastMalloc(size);

  if (ptr) {
    trackAllocation(ptr, size);
    const unsigned long long threadId = static_cast<unsigned long long>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    Log(DEBUG, "NcnnAllocator",
        "Allocated {} bytes at {} (thread: {}, total: {} bytes, count: {})",
        size, ptr, threadId, totalAllocated_.load(),
        allocationCount_.load());
  } else {
    Log(ERROR, "NcnnAllocator", "Failed to allocate {} bytes", size);
  }

  return ptr;
}

void NcnnAllocator::fastFree(void *ptr) {
  if (!ptr) {
    return;
  }

  size_t freedSize = 0;
  {
    std::lock_guard<std::mutex> lock(allocationsMutex_);
    auto it = activeAllocations_.find(ptr);
    if (it != activeAllocations_.end()) {
      freedSize = it->second;
      activeAllocations_.erase(it);

      totalAllocated_.fetch_sub(freedSize);
      allocationCount_.fetch_sub(1);
    }
  }

  defaultAllocator_->fastFree(ptr);

  const unsigned long long threadId = static_cast<unsigned long long>(
      std::hash<std::thread::id>{}(std::this_thread::get_id()));
  Log(DEBUG, "NcnnAllocator",
      "Freed {} bytes at {} (thread: {}, remaining: {} bytes, count: {})",
      freedSize, ptr, threadId, totalAllocated_.load(),
      allocationCount_.load());

  if (cleanupCallback_ && freedSize > 0) {
    try {
      cleanupCallback_(freedSize);
    } catch (const std::exception &e) {
      Log(ERROR, "NcnnAllocator", "Cleanup callback threw exception: {}",
          e.what());
    }
  }
}

void NcnnAllocator::trackAllocation(void *ptr, size_t size) {
  std::lock_guard<std::mutex> lock(allocationsMutex_);
  activeAllocations_[ptr] = size;

  totalAllocated_.fetch_add(size);
  allocationCount_.fetch_add(1);
}

void NcnnAllocator::trackDeallocation(void *ptr) {}

void NcnnAllocator::forceCleanup() {
  Log(DEBUG, "NcnnAllocator", "Force cleanup requested");

  std::vector<void *> ptrsToFree;
  {
    std::lock_guard<std::mutex> lock(allocationsMutex_);
    ptrsToFree.reserve(activeAllocations_.size());
    for (const auto &allocation : activeAllocations_) {
      ptrsToFree.push_back(allocation.first);
    }
  }

  Log(DEBUG, "NcnnAllocator", "Force cleanup freeing {} allocations",
      ptrsToFree.size());

  for (void *ptr : ptrsToFree) {
    fastFree(ptr);
  }

  Log(DEBUG, "NcnnAllocator", "Force cleanup complete");
}

void NcnnAllocator::waitForCleanup() {
  Log(DEBUG, "NcnnAllocator", "Waiting for cleanup completion");

  const auto timeout = std::chrono::milliseconds(5000); // 5 second timeout
  const auto start = std::chrono::steady_clock::now();

  while (allocationCount_.load() > 0) {
    if (std::chrono::steady_clock::now() - start > timeout) {
      Log(WARNING, "NcnnAllocator",
          "Cleanup timeout - {} allocations still active",
          allocationCount_.load());
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  const size_t remainingCount = allocationCount_.load();
  const size_t remainingBytes = totalAllocated_.load();

  if (remainingCount == 0) {
    Log(DEBUG, "NcnnAllocator", "All allocations cleaned up successfully");
  } else {
    Log(WARNING, "NcnnAllocator",
        "Cleanup incomplete - {} allocations ({} bytes) still active",
        remainingCount, remainingBytes);
  }
}

} // namespace ProjectIE4k
