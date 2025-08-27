#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <map>
#include <filesystem>

#include "ServiceBase.h"
#include "ResourceService/ResourceTypes.h"
#include "BIFArchive.h"

namespace ProjectIE4k {

class BIFService : public ServiceBase {
private:
    std::unordered_map<unsigned int, std::unique_ptr<BIFArchive>> bifCache;
    std::unordered_map<unsigned int, BIFEntry> bifMetadata;
    mutable std::mutex bifCacheMutex;
    mutable std::mutex bifMetadataMutex;
    bool initialized_ = false;
    SClass_ID currentResourceType_ = 0;
    
    // Complete size index for all resources across all BIF files
    std::map<std::pair<std::string, SClass_ID>, uint32_t> completeSizeIndex;
    mutable std::mutex sizeIndexMutex;

public:
    BIFService();
    ~BIFService();
    
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

    // BIF management
    bool initialize(const std::vector<BIFEntry>& bifEntries);
    std::vector<uint8_t> getResourceData(const ResourceInfo& resourceInfo);
    std::vector<uint8_t> getResourceData(unsigned int bifIndex, uint32_t offset, uint32_t size);
    
    // Archive management
    bool hasBIFArchive(unsigned int bifIndex) const;
    bool isBIFLoaded(unsigned int bifIndex) const;
    BIFArchive* getBIFArchive(unsigned int bifIndex);
    bool preloadBIF(unsigned int bifIndex);
    bool loadBIFArchive(unsigned int bifIndex);
    void unloadBIFArchive(unsigned int bifIndex);
    
    // Complete size indexing
    bool buildCompleteSizeIndex();
    uint32_t getResourceSize(const std::string& resourceName, SClass_ID resourceType) const;
    size_t getTotalIndexedSize() const;
    size_t getIndexedResourceCount() const;
    
    // Utility methods
    std::string getBIFPath(unsigned int bifIndex) const;
    bool validateBIFIndex(unsigned int bifIndex) const;
    size_t getBIFCount() const;
    size_t getCacheSize() const;

    // Clean up
    void cleanupCacheDirectory();

private:
    bool findBIFFile(BIFEntry& bifEntry);
    std::filesystem::path findCaseInsensitivePath(const std::filesystem::path& targetPath) const;
    std::filesystem::path tryCommonVariations(const std::filesystem::path& dataDir, const std::filesystem::path& relativePath) const;
};

} // namespace ProjectIE4k 