#pragma once

#include <map>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>

#include "services/ServiceBase.h"
#include "core/SClassID.h"
#include "services/ResourceService/ResourceCoordinatorService.h"
#include "services/OperationsTrackerService/OperationsTrackerService.h"
#include "CommandRegistry.h"

namespace ProjectIE4k {
    class PluginBase;
    
/**
 * @brief Manages all plugins and provides batch operations
 * 
 * This class handles plugin registration, discovery, and provides
 * generalized batch operations across all resource types.
 */
class PluginManager {
public:
    static PluginManager& getInstance() {
        static PluginManager instance; // Static instance, initialized once
        return instance;
    }

    PluginManager();
    ~PluginManager();
    
    // Plugin registration - will be implemented in cpp file
    void registerPlugin(SClass_ID resourceType, std::function<std::unique_ptr<PluginBase>(const std::string&)> factory);
    void ensurePluginRegistered(SClass_ID resourceType);
    
    // Batch operations - generalized across all plugins
    bool extractAllResources();
    bool upscaleAllResources();
    bool assembleAllResources();
    bool completeAllResources();
    bool transferAssembledAssetsToOverride();
    bool transferByResourceType(SClass_ID resourceType);
    
    // Type-specific batch operations
    bool extractAllResourcesOfType(SClass_ID resourceType);
    bool upscaleAllResourcesOfType(SClass_ID resourceType);
    bool assembleAllResourcesOfType(SClass_ID resourceType);
    bool completeAllResourcesOfType(SClass_ID resourceType);
    
    // Individual resource operations
    bool extractResource(const std::string& resourceName, SClass_ID resourceType, bool enableStats = true);
    bool upscaleResource(const std::string& resourceName, SClass_ID resourceType, bool enableStats = true);
    bool assembleResource(const std::string& resourceName, SClass_ID resourceType, bool enableStats = true);
    
    // Command registration
    void registerAllCommands(CommandTable& commandTable);
    
    // Plugin discovery
    std::vector<SClass_ID> getSupportedResourceTypes() const;
    bool isResourceTypeSupported(SClass_ID resourceType) const;
    
    // Utility methods
    SClass_ID getResourceTypeFromString(const std::string& typeString) const;

    // PVRZ name generation methods
    std::pair<std::string, int> generatePVRZName(const std::string& resourceName, SClass_ID resourceType);
    std::string generatePVRZNameInternal(const std::string& resourceName, int pageNumber, SClass_ID resourceType);
    
    // Service management
    void registerService(const std::string& serviceName, std::unique_ptr<ServiceBase> service);
    ServiceBase* getService(const std::string& serviceName);
    
    // Dynamic service loading
    std::vector<std::string> getAvailableServices() const;
    
    // Batch lifecycle management
    void onBatchTypeStart(SClass_ID resourceType);
    void onBatchTypeEnd(SClass_ID resourceType);
    
    // Shared resource management
    void ensureSharedResourcesInitialized(SClass_ID resourceType);
    
    // Internal sync operations
    bool syncUnsupportedOnly();
    bool syncOverrideAssets();
    bool syncTTFFiles();
    
    // Resource service methods
    ResourceCoordinatorService* getResourceCoordinator();
    std::vector<std::string> listResourcesByType(SClass_ID resourceType);

    
    // Statistics
    struct BatchStats {
        int totalResources = 0;
        int successfulOperations = 0;
        int failedOperations = 0;
        std::chrono::milliseconds totalTime{0};
    };
    
    BatchStats getLastBatchStats() const { return lastBatchStats_; }

private:
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    using PluginFactory = std::function<std::unique_ptr<PluginBase>(const std::string&)>;
    
    std::map<SClass_ID, PluginFactory> pluginFactories_;
    std::map<std::string, std::unique_ptr<ServiceBase>> services_;
    SClass_ID currentBatchResourceType_;
    BatchStats lastBatchStats_;
    
    // PVRZ counter variables (moved from global scope)
    std::atomic<int> areaPVRZCounter_{0};
    std::atomic<int> mosPVRZCounter_{0};
    
    // Shared resource tracking
    std::map<SClass_ID, bool> sharedResourcesInitialized_;
    std::mutex sharedResourcesMutex_;
    
    // Helper methods
    std::unique_ptr<PluginBase> createPlugin(const std::string& resourceName, SClass_ID resourceType);
    void resetBatchStats();
    void logBatchResults(const std::string& operation, const BatchStats& stats);

    // Helper to build operation fingerprint used by OperationsTrackerService
    static OperationsTrackerService::InputFingerprint makeInputFingerprint(const std::string& resourceName,
                                                                           SClass_ID resourceType,
                                                                           const std::string& opVersion);
};

// Global instance declaration
PluginManager& getInstance();

} // namespace ProjectIE4k 