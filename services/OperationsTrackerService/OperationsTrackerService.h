#pragma once

#include "services/ServiceBase.h"

#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <unordered_map>

#include "core/SClassID.h"

namespace ProjectIE4k {

class OperationsTrackerService : public ServiceBase {
public:
    struct InputFingerprint {
        // For BIF-backed resources
        int bifIndex = -1;
        uint32_t keyLocator = 0;
        uint32_t size = 0;
        // For override-backed resources
        std::string sourcePath;
        uint64_t mtime = 0;
        uint64_t overrideSize = 0;
        // Common
        std::string configHash;
        std::string opVersion;
    };

    // ServiceBase interface
    void initializeForResourceType(SClass_ID resourceType) override;
    void cleanup() override;
    bool isInitialized() const override { return initialized_; }
    SClass_ID getCurrentResourceType() const override { return currentResourceType_; }

    ServiceLifecycle getLifecycle() const override { return ServiceLifecycle::APPLICATION_START; }
    ServiceScope getScope() const override { return ServiceScope::SINGLETON; }
    bool shouldAutoInitialize() const override { return true; }
    void onLifecycleEvent(ServiceLifecycle event, const std::string& context = "") override;

    // Phase-level API
    void startPhase(const std::string& phase, const std::string& resourceType, size_t total);
    void endPhase(const std::string& phase, const std::string& resourceType, bool allSucceeded);

    // Per-resource API
    bool shouldProcess(const std::string& phase,
                       const std::string& resourceType,
                       const std::string& resourceName,
                       const InputFingerprint& fp,
                       bool force = false);

    // Phase-level API
    bool shouldProcessPhase(const std::string& phase,
                           const std::string& resourceType);

    void markStarted(const std::string& phase,
                     const std::string& resourceType,
                     const std::string& resourceName,
                     const InputFingerprint& fp);

    void markCompleted(const std::string& phase,
                       const std::string& resourceType,
                       const std::string& resourceName,
                       bool success,
                       const std::vector<std::string>& outputPaths,
                       const std::string& errorMessage = "");

private:
    // Internal helpers
    void ensureLedgerOpen();
    std::string getOpsDirUnsafe() const; // no fs creation
    void writeJsonlUnsafe(const std::string& line);
    std::string makeKey(const std::string& phase, const std::string& resourceType, const std::string& resourceName) const;
    void loadLedgerIntoCacheUnsafe();
    void buildCacheBlocking();
    void flushPendingUnsafe();

    struct LatestEntry {
        std::string configHash;
        std::string opVersion;
        int bifIndex = -1;
        uint32_t keyLocator = 0;
        uint32_t size = 0;
        std::string sourcePath;
        uint64_t mtime = 0;
        uint64_t overrideSize = 0;
        bool success = false;
    };

    // Cache of last-seen status for quick shouldProcess decisions
    std::unordered_map<std::string, LatestEntry> latestCache_;

    // State
    bool initialized_ = false;
    SClass_ID currentResourceType_ = 0;
    mutable std::mutex mtx_;
    std::ofstream ledger_;
    bool cacheLoaded_ = false;
    bool dirty_ = false; // whether we have pending appends to write
    std::vector<std::string> pendingLines_; // buffered JSONL lines to flush on shutdown
};

} // namespace ProjectIE4k
