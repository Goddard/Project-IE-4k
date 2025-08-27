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
    bool initialized_ = false;
    SClass_ID currentResourceType_ = 0;
    mutable std::mutex serviceMutex;
    
    // Enhanced in-memory cache for override directory files with size information
    mutable std::map<std::pair<std::string, SClass_ID>, OverrideFileInfo> overrideFileMap;

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
    
    // New methods for resource size information
    uint64_t getResourceSize(const std::string& resourceName, SClass_ID resourceType) const;
    std::vector<std::pair<std::string, uint64_t>> getResourcesWithSizes(SClass_ID resourceType) const;
    size_t getTotalSizeForResourceType(SClass_ID resourceType) const;

private:
    bool initializeServices();
    void optimizeForResourceType(SClass_ID resourceType);
    bool validateResourceName(const std::string& resourceName) const;
    std::string normalizeResourceName(const std::string& resourceName) const;
    std::string findKEYFile(const std::string& gamePath) const;
    std::filesystem::path findCaseInsensitivePath(const std::filesystem::path& targetPath) const;
    std::vector<std::string> scanOverrideDirectoryForType(SClass_ID resourceType, const std::string& extension) const;
    void buildOverrideFileMap();

    bool verbose = false;
};

} // namespace ProjectIE4k 