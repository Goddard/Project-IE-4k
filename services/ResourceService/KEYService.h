#pragma once

#include <unordered_map>
#include <mutex>
#include <vector>
#include <string>
#include <filesystem>

#include "ResourceTypes.h"
#include "services/ServiceBase.h"
#include "core/SClassID.h"

namespace ProjectIE4k {

/**
 * @brief Service for handling KEY file parsing and resource indexing
 * 
 * This service is responsible for:
 * - Parsing KEY files to build resource index
 * - Providing resource lookup functionality
 * - Managing BIF file entries
 */
class KEYService : public ServiceBase {
private:
    // KEY file data
    std::string keyFilePath;
    KEYHeader keyHeader;
    std::vector<BIFEntry> bifFiles;
    
    // Resource index
    std::unordered_map<ResourceKey, ResourceInfo, ResourceKeyHash> resourceIndex;
    mutable std::mutex resourceIndexMutex;
    
    // Service state
    bool initialized_;
    SClass_ID currentResourceType_ = 0;
    
public:
    KEYService();
    ~KEYService();
    
    // ServiceBase interface
    void initializeForResourceType(SClass_ID resourceType) override;
    void cleanup() override;
    bool isInitialized() const override { return initialized_; }
    SClass_ID getCurrentResourceType() const override { return currentResourceType_; }
    
    // Lifecycle management
    ServiceLifecycle getLifecycle() const override { return ServiceLifecycle::ON_DEMAND; }
    ServiceScope getScope() const override { return ServiceScope::SINGLETON; }
    bool shouldAutoInitialize() const override { return false; }
    void onLifecycleEvent(ServiceLifecycle event, const std::string& context = "") override;
    
    // Core KEY operations
    bool hasResource(const std::string& resourceName, SClass_ID resourceType);
    ResourceInfo getResourceInfo(const std::string& resourceName, SClass_ID resourceType);
    std::vector<std::string> listResourcesByType(SClass_ID resourceType);
    std::vector<BIFEntry> getBIFEntries() const;
    
    // Service lifecycle
    bool initialize(const std::string& keyPath);
    
    // Index management
    void rebuildIndex();
    size_t getResourceCount() const;
    size_t getBIFCount() const;
    
    // Utility methods
    std::string getKeyFilePath() const { return keyFilePath; }
    const KEYHeader& getHeader() const { return keyHeader; }
    
private:
    // Internal parsing methods
    bool parseKEYFile();
    bool readBIFEntries(FILE* file);
    bool readResourceEntries(FILE* file);
    bool resolveBIFPaths();
    void buildResourceIndex();
    
    // Helper methods
    std::string normalizeResourceName(const std::string& name);
    bool findBIFFile(BIFEntry& bifEntry);
    
    // Path resolution helpers
    std::filesystem::path findCaseInsensitivePath(const std::filesystem::path& targetPath);
    std::filesystem::path tryCommonVariations(const std::filesystem::path& dataDir, const std::filesystem::path& relativePath);
    
    // Resource filtering
    bool isResourceKnownBad(const std::string& resourceName) const;
};

} // namespace ProjectIE4k 