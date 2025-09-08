#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <map>

#include "ServiceBase.h"
#include "ResourceService/ResourceTypes.h"
#include "ResourceService/KEYService.h"
#include "ResourceService/BIFService.h"
#include "core/SClassID.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

// Structure to store file metadata including size information
struct OverrideFileInfo {
    std::string fullPath;
    uint64_t fileSize;
    std::string originalFilename;  // Original filename with extension
    
    OverrideFileInfo() : fileSize(0) {}
    OverrideFileInfo(const std::string& path, uint64_t size, const std::string& filename) 
        : fullPath(path), fileSize(size), originalFilename(filename) {}
};

class ResourceCoordinatorService : public ServiceBase {
private:
    std::unique_ptr<KEYService> keyService;
    std::unique_ptr<BIFService> bifService;
    std::string gamePath;
    std::string overridePath;
    std::string unhardcodedPath;
    std::string unhardcodedSharedPath;
    bool initialized_ = false;
    SClass_ID currentResourceType_ = 0;
    mutable std::mutex serviceMutex;

    // Enhanced in-memory cache for override directory files with size information
    mutable std::map<std::pair<std::string, SClass_ID>, OverrideFileInfo> overrideFileMap;

    // Enhanced in-memory cache for unhardcoded directory files with size information
    mutable std::map<std::pair<std::string, SClass_ID>, OverrideFileInfo> unhardcodedFileMap;
    
    // Enhanced in-memory cache for unhardcoded shared directory files with size information
    mutable std::map<std::pair<std::string, SClass_ID>, OverrideFileInfo> unhardcodedSharedFileMap;

public:
    ResourceCoordinatorService();
    ~ResourceCoordinatorService() override = default;

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

    // ResourceCoordinatorService specific methods
    bool hasResource(const std::string& resourceName, SClass_ID resourceType);
    ResourceData getResourceData(const std::string& resourceName, SClass_ID resourceType);
    std::vector<std::string> listResourcesByType(SClass_ID resourceType);
    size_t getResourceCount() const;
    size_t getBIFCount() const;
    size_t getCacheSize() const;
    bool initializeFromGamePath(const std::string& gamePath);
    
    // Override directory support
    std::vector<uint8_t> getResourceDataFromOverride(const std::string& resourceName, SClass_ID resourceType) const;
    bool hasResourceInOverride(const std::string& resourceName, SClass_ID resourceType) const;
    std::string getOverridePath() const { return overridePath; }

    // Unhardcoded directory support
    std::vector<uint8_t> getResourceDataFromUnhardcoded(const std::string& resourceName, SClass_ID resourceType) const;
    bool hasResourceInUnhardcoded(const std::string& resourceName, SClass_ID resourceType) const;
    std::string getUnhardcodedPath() const { return unhardcodedPath; }
    
    // Unhardcoded shared directory support
    std::vector<uint8_t> getResourceDataFromUnhardcodedShared(const std::string& resourceName, SClass_ID resourceType) const;
    bool hasResourceInUnhardcodedShared(const std::string& resourceName, SClass_ID resourceType) const;
    std::string getUnhardcodedSharedPath() const { return unhardcodedSharedPath; }
    
    // New methods for resource size information
    uint64_t getResourceSize(const std::string& resourceName, SClass_ID resourceType) const;
    std::vector<std::pair<std::string, uint64_t>> getResourcesWithSizes(SClass_ID resourceType) const;
    size_t getTotalSizeForResourceType(SClass_ID resourceType) const;
    
    /**
     * @brief List all resources for all types
     * @return true if successful
     */
    bool list();

private:
    bool initializeServices();
    void optimizeForResourceType(SClass_ID resourceType);
    bool validateResourceName(const std::string& resourceName) const;
    std::string normalizeResourceName(const std::string& resourceName) const;
    std::string findKEYFile(const std::string& gamePath) const;
    std::filesystem::path findCaseInsensitivePath(const std::filesystem::path& targetPath) const;
    std::vector<std::string> scanOverrideDirectoryForType(SClass_ID resourceType, const std::string& extension) const;
    void buildOverrideFileMap();
    std::vector<std::string> scanUnhardcodedDirectoryForType(SClass_ID resourceType, const std::string& extension) const;
    void buildUnhardcodedFileMap();
    std::vector<std::string> scanUnhardcodedSharedDirectoryForType(SClass_ID resourceType, const std::string& extension) const;
    void buildUnhardcodedSharedFileMap();

    bool verbose = false;

public:
    // Command registration (following plugin pattern)
    static void registerCommands(CommandTable& commandTable);
};

} // namespace ProjectIE4k 