#pragma once

#include "ServiceBase.h"

#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <mutex>

#include "core/SClassID.h"

namespace ProjectIE4k {

class StatisticsService : public ServiceBase {
public:
    struct ProcessStats {
        std::string processName;
        std::string resourceType;
        int totalFiles;
        int processedFiles;
        int successfulFiles;
        int failedFiles;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point endTime;
        std::map<std::string, int> errorCounts;
        std::vector<std::string> errors;
        
        ProcessStats() : totalFiles(0), processedFiles(0), successfulFiles(0), failedFiles(0) {}
    };

    StatisticsService();
    ~StatisticsService() override = default;
    
    // ServiceBase interface
    void initializeForResourceType(SClass_ID resourceType) override;
    void cleanup() override;
    bool isInitialized() const override { return initialized_; }
    SClass_ID getCurrentResourceType() const override { return currentResourceType_; }
    
    // Lifecycle management
    ServiceLifecycle getLifecycle() const override { return ServiceLifecycle::APPLICATION_START; }
    ServiceScope getScope() const override { return ServiceScope::SINGLETON; }
    bool shouldAutoInitialize() const override { return true; }
    void onLifecycleEvent(ServiceLifecycle event, const std::string& context = "") override;
    
    // Process lifecycle management
    void startProcess(const std::string& processName, const std::string& resourceType, int totalFiles = 0);
    void incrementProcessed(const std::string& processName, bool success = true);
    void recordError(const std::string& processName, const std::string& error);
    void endProcess(const std::string& processName);
    
    // Statistics retrieval
    ProcessStats getProcessStats(const std::string& processName) const;
    std::vector<std::string> getAllProcessNames() const;
    
    // Summary generation
    void generateSummary() const;
    void saveSummaryToFile(const std::string& filename) const;
    
    // Utility methods
    void clear();
    bool hasProcess(const std::string& processName) const;

private:
    bool initialized_ = false;
    SClass_ID currentResourceType_ = 0;
    mutable std::mutex statsMutex_;
    std::map<std::string, ProcessStats> processes_;
    
    std::string formatDuration(const std::chrono::steady_clock::duration& duration) const;
    std::string formatFileSize(size_t bytes) const;
};

} // namespace ProjectIE4k 