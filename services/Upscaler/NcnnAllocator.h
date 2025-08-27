#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <ncnn/allocator.h>
#include <unordered_map>

namespace ProjectIE4k {

class NcnnAllocator : public ncnn::Allocator {
public:
  explicit NcnnAllocator();
  ~NcnnAllocator();

  void *fastMalloc(size_t size) override;
  void fastFree(void *ptr) override;

  using CleanupCallback = std::function<void(size_t freedBytes)>;
  void setCleanupCallback(const CleanupCallback &callback) {
    cleanupCallback_ = callback;
  }

  size_t getTotalAllocated() const { return totalAllocated_.load(); }
  size_t getAllocationCount() const { return allocationCount_.load(); }

  void forceCleanup();
  void waitForCleanup();

private:
  std::unique_ptr<ncnn::Allocator> defaultAllocator_;

  std::atomic<size_t> totalAllocated_{0};
  std::atomic<size_t> allocationCount_{0};

  mutable std::mutex allocationsMutex_;
  std::unordered_map<void *, size_t> activeAllocations_;

  CleanupCallback cleanupCallback_;

  void trackAllocation(void *ptr, size_t size);
  void trackDeallocation(void *ptr);
};

} // namespace ProjectIE4k
