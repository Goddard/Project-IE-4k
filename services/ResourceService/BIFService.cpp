#include "BIFService.h"

#include <chrono>
#include <numeric>
#include <future>

#include "BIFArchive.h"
#include "core/CFG.h"
#include "core/Logging/Logging.h"
#include "core/OperationsMonitor/OperationsMonitor.h"

namespace ProjectIE4k {

BIFService::BIFService() : initialized_(false) {
}

BIFService::~BIFService() {
    cleanup();
}

void BIFService::initializeForResourceType(SClass_ID resourceType) {
    // BIF service doesn't need resource type-specific initialization
    // It manages all BIF archives globally
    currentResourceType_ = resourceType;
    Log(DEBUG, "BIFService", "Initialized for resource type: {}", resourceType);
}

void BIFService::cleanup() {
    std::lock_guard<std::mutex> lock(bifCacheMutex);
    bifCache.clear();
    
    std::lock_guard<std::mutex> metadataLock(bifMetadataMutex);
    bifMetadata.clear();
    
    cleanupCacheDirectory();
    initialized_ = false;
    currentResourceType_ = 0;
    Log(DEBUG, "BIFService", "Cleaned up BIF service");
}

void BIFService::onLifecycleEvent(ServiceLifecycle event, const std::string& context) {
    switch (event) {
        case ServiceLifecycle::APPLICATION_START:
            Log(DEBUG, "BIFService", "Application start event received");
            break;
            
        case ServiceLifecycle::APPLICATION_SHUTDOWN:
            Log(DEBUG, "BIFService", "Application shutdown event received");
            cleanupCacheDirectory();
            break;
            
        default:
            // Ignore other events
            break;
    }
}

void BIFService::cleanupCacheDirectory() {
  // Check if we should retain cache based on configuration
  if (PIE4K_CFG.RetainCache) {
    Log(DEBUG, "BIFService", "Cache retention enabled, skipping cache cleanup");
    return;
  }

    std::filesystem::path exePath = std::filesystem::current_path();
    std::filesystem::path cacheDir = exePath / ".pie4kcache";
    
    if (std::filesystem::exists(cacheDir)) {
        try {
            std::filesystem::remove_all(cacheDir);
            Log(DEBUG, "BIFService", "Cleaned up cache directory: {}",
                cacheDir.string());
        } catch (const std::filesystem::filesystem_error& e) {
          Log(WARNING, "BIFService",
              "Failed to clean up cache directory {}: {}", cacheDir.string(),
              e.what());
        }
    }
}

bool BIFService::initialize(const std::vector<BIFEntry>& bifEntries) {
    if (initialized_) {
        Log(WARNING, "BIFService", "Already initialized");
        return true;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    Log(DEBUG, "BIFService", "Starting BIF service initialization with {} BIF files", bifEntries.size());
    
    // Store BIF metadata
    {
        std::lock_guard<std::mutex> lock(bifMetadataMutex);
        for (size_t i = 0; i < bifEntries.size(); ++i) {
            bifMetadata[i] = bifEntries[i];
        }
    }
    
    // Build complete size index for all BIF files
    Log(DEBUG, "BIFService", "Building complete size index for all BIF files...");
    if (!buildCompleteSizeIndex()) {
        Log(ERROR, "BIFService", "Failed to build complete size index");
        return false;
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    initialized_ = true;
    Log(MESSAGE, "BIFService", "Successfully initialized BIF service with {} BIF files in {} ms", 
        bifEntries.size(), duration.count());
    Log(DEBUG, "BIFService", "Complete size index contains {} resources with total size {} bytes", 
        getIndexedResourceCount(), getTotalIndexedSize());
    return true;
}

std::vector<uint8_t> BIFService::getResourceData(const ResourceInfo& resourceInfo) {
    Log(DEBUG, "BIFService", "getResourceData(ResourceInfo) called: bifIndex={}, locator={:08x}, type={}", 
        resourceInfo.bifIndex, resourceInfo.locator, resourceInfo.type);
    
    if (!resourceInfo.isValid) {
        Log(ERROR, "BIFService", "Invalid resource info");
        return std::vector<uint8_t>();
    }
    
    if (!initialized_) {
        Log(ERROR, "BIFService", "BIF service not initialized");
        return std::vector<uint8_t>();
    }
    
    if (!validateBIFIndex(resourceInfo.bifIndex)) {
        Log(ERROR, "BIFService", "Invalid BIF index: {}", resourceInfo.bifIndex);
        return std::vector<uint8_t>();
    }
    
    BIFArchive* archive = getBIFArchive(resourceInfo.bifIndex);
    if (!archive) {
        Log(ERROR, "BIFService", "Failed to get BIF archive for index: {}", resourceInfo.bifIndex);
        return std::vector<uint8_t>();
    }
    
    // Use optimized method that combines lookup and read in single operation
    std::vector<uint8_t> data = archive->getResourceDataOptimized(resourceInfo.locator, resourceInfo.type);
    
    if (data.empty()) {
        Log(ERROR, "BIFService", "Failed to extract resource data from BIF {} for locator {:08x}", resourceInfo.bifIndex, resourceInfo.locator);
        return std::vector<uint8_t>();
    }
    
    Log(DEBUG, "BIFService", "Successfully extracted {} bytes from BIF {} for locator {:08x}", data.size(), resourceInfo.bifIndex, resourceInfo.locator);
    
    // Special handling for TIS files - construct header and prepend to data
    if (resourceInfo.type == 0x3EB) { // IE_TIS_CLASS_ID
        Log(DEBUG, "BIFService", "Processing TIS file, constructing header");
        
        // Get tile entry information from the archive
        uint32_t tileCount = 0;
        if (archive->getTileEntryInfo(resourceInfo.locator, tileCount)) {
            Log(DEBUG, "BIFService", "TIS tile count from BIF: {}", tileCount);
            
            // Construct TIS header
            struct TISHeader {
                char signature[4];    // "TIS "
                char version[4];      // "V1 "
                uint32_t tileCount;   // Number of tiles
                uint32_t tileSize;    // Size of each tile entry (5120 for V1)
                uint32_t headerSize;  // Size of header (24 bytes)
                uint32_t tileDimension; // Tile dimension (64 pixels)
            } __attribute__((packed));
            
            TISHeader header;
            memcpy(header.signature, "TIS ", 4);
            memcpy(header.version, "V1  ", 4);
            header.tileCount = tileCount;
            header.tileSize = 5120;  // V1 palette-based tiles
            header.headerSize = 24;
            header.tileDimension = 64;
            
            // Prepend header to data
            std::vector<uint8_t> completeData;
            completeData.reserve(sizeof(TISHeader) + data.size());
            
            const uint8_t* headerPtr = reinterpret_cast<const uint8_t*>(&header);
            completeData.insert(completeData.end(), headerPtr, headerPtr + sizeof(TISHeader));
            completeData.insert(completeData.end(), data.begin(), data.end());
            
            Log(DEBUG, "BIFService", "Constructed complete TIS data: {} bytes (header: {} + data: {})", 
                completeData.size(), sizeof(TISHeader), data.size());
            
            return completeData;
        } else {
            Log(ERROR, "BIFService", "Failed to get tile entry info for TIS resource");
            return data; // Return original data without header
        }
    }
    
    return data;
}

std::vector<uint8_t> BIFService::getResourceData(unsigned int bifIndex, uint32_t offset, uint32_t size) {
    Log(DEBUG, "BIFService", "getResourceData(bifIndex, offset, size) called: bifIndex={}, offset={}, size={}", bifIndex, offset, size);
    
    if (!initialized_) {
        Log(ERROR, "BIFService", "BIF service not initialized");
        return std::vector<uint8_t>();
    }
    
    Log(DEBUG, "BIFService", "About to validate BIF index");
    if (!validateBIFIndex(bifIndex)) {
        Log(ERROR, "BIFService", "Invalid BIF index: {}", bifIndex);
        return std::vector<uint8_t>();
    }
    
    Log(DEBUG, "BIFService", "About to get BIF archive");
    BIFArchive* archive = getBIFArchive(bifIndex);
    if (!archive) {
        Log(ERROR, "BIFService", "Failed to get BIF archive for index: {}", bifIndex);
        return std::vector<uint8_t>();
    }
    
    // Use the offset and size directly to read the resource data
    std::vector<uint8_t> data = archive->getResourceDataByOffset(offset, size);
    
    if (data.empty()) {
        Log(ERROR, "BIFService", "Failed to extract resource data from BIF {} at offset {} with size {}", bifIndex, offset, size);
    } else {
        Log(DEBUG, "BIFService", "Successfully extracted {} bytes from BIF {} at offset {}", data.size(), bifIndex, offset);
    }
    
    return data;
}

bool BIFService::hasBIFArchive(unsigned int bifIndex) const {
    if (!initialized_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(bifMetadataMutex);
    return bifMetadata.find(bifIndex) != bifMetadata.end();
}

size_t BIFService::getCacheSize() const {
    std::lock_guard<std::mutex> lock(bifCacheMutex);
    return bifCache.size();
}

bool BIFService::preloadBIF(unsigned int bifIndex) {
    if (!initialized_) {
        Log(ERROR, "BIFService", "BIF service not initialized");
        return false;
    }
    
    if (!validateBIFIndex(bifIndex)) {
        Log(ERROR, "BIFService", "Invalid BIF index: {}", bifIndex);
        return false;
    }
    
    return loadBIFArchive(bifIndex);
}

size_t BIFService::getBIFCount() const {
    std::lock_guard<std::mutex> lock(bifMetadataMutex);
    return bifMetadata.size();
}

bool BIFService::isBIFLoaded(unsigned int bifIndex) const {
    std::lock_guard<std::mutex> lock(bifCacheMutex);
    return bifCache.find(bifIndex) != bifCache.end();
}

BIFArchive* BIFService::getBIFArchive(unsigned int bifIndex) {
    // First check if it's already in cache
    {
        std::lock_guard<std::mutex> lock(bifCacheMutex);
        auto it = bifCache.find(bifIndex);
        if (it != bifCache.end()) {
            Log(DEBUG, "BIFService", "Cache hit for BIF archive {}", bifIndex);
            return it->second.get();
        }
    }
    
    // Cache miss - load the archive (without holding the lock)
    Log(DEBUG, "BIFService", "Cache miss for BIF archive {}", bifIndex);
    if (loadBIFArchive(bifIndex)) {
        // Get the archive from cache after loading
        std::lock_guard<std::mutex> lock(bifCacheMutex);
        auto it = bifCache.find(bifIndex);
        if (it != bifCache.end()) {
            return it->second.get();
        }
    }
    
    return nullptr;
}

bool BIFService::loadBIFArchive(unsigned int bifIndex) {
    Log(DEBUG, "BIFService", "loadBIFArchive called: bifIndex={}", bifIndex);
    
    Log(DEBUG, "BIFService", "About to get BIF path");
    std::string bifPath = getBIFPath(bifIndex);
    if (bifPath.empty()) {
        Log(ERROR, "BIFService", "No path found for BIF index: {}", bifIndex);
        return false;
    }
    
    Log(DEBUG, "BIFService", "About to create BIFArchive: {}", bifPath);
    auto archive = std::make_unique<BIFArchive>(bifPath);
    
    Log(DEBUG, "BIFService", "About to open BIF archive");
    if (!archive->open()) {
        Log(ERROR, "BIFService", "Failed to open BIF archive: {}", bifPath);
        return false;
    }
    Log(DEBUG, "BIFService", "Successfully opened BIF archive");
    
    Log(DEBUG, "BIFService", "About to cache BIF archive");
    std::lock_guard<std::mutex> lock(bifCacheMutex);
    bifCache[bifIndex] = std::move(archive);
    
    Log(DEBUG, "BIFService", "Loaded BIF archive {}: {} ({} cached archives)", 
        bifIndex, bifPath, bifCache.size());
    return true;
}

void BIFService::unloadBIFArchive(unsigned int bifIndex) {
    std::lock_guard<std::mutex> lock(bifCacheMutex);
    auto it = bifCache.find(bifIndex);
    if (it != bifCache.end()) {
        bifCache.erase(it);
        Log(DEBUG, "BIFService", "Unloaded BIF archive {} ({} cached archives)", bifIndex, bifCache.size());
    }
}

std::string BIFService::getBIFPath(unsigned int bifIndex) const {
    std::lock_guard<std::mutex> lock(bifMetadataMutex);
    auto it = bifMetadata.find(bifIndex);
    if (it != bifMetadata.end()) {
        return it->second.fullPath;
    }
    return std::string();
}

bool BIFService::validateBIFIndex(unsigned int bifIndex) const {
    std::lock_guard<std::mutex> lock(bifMetadataMutex);
    return bifMetadata.find(bifIndex) != bifMetadata.end();
}

// Complete size indexing implementation with priority-based scheduling
bool BIFService::buildCompleteSizeIndex() {
    Log(DEBUG, "BIFService", "Building complete size index for all BIF files using priority-based scheduling");
    
    // Reset thread count to starting value for BIF indexing operations
    auto &monitor = OperationsMonitor::getInstance();
    // Thread scaling is now handled automatically by OperationsMonitor

    if (!monitor.isInitialized()) {
        monitor.initialize();
    }
    
    // Submit BIF indexing tasks with exclusive priority
    std::vector<std::future<std::pair<unsigned int, std::map<uint32_t, uint32_t>>>> futures;
    
    Log(DEBUG, "BIFService", "Using exclusive priority for BIF indexing");
    
    std::lock_guard<std::mutex> lock(sizeIndexMutex);
    completeSizeIndex.clear();
    
    size_t totalResources = 0;
    size_t totalSize = 0;
    
    // Collect BIF files to process
    std::vector<std::pair<unsigned int, BIFEntry>> bifFilesToProcess;
    for (const auto& [bifIndex, bifEntry] : bifMetadata) {
        if (bifEntry.found) {
            bifFilesToProcess.emplace_back(bifIndex, bifEntry);
        } else {
            Log(DEBUG, "BIFService", "Skipping BIF {} (not found): {}", bifIndex, bifEntry.filename);
        }
    }
    
    Log(DEBUG, "BIFService", "Processing {} BIF files in parallel", bifFilesToProcess.size());

    // Submit parallel tasks for BIF indexing using high priority
    for (const auto& [bifIndex, bifEntry] : bifFilesToProcess) {
      OperationRequirements requirements;
      requirements.operationType = "bif_index";
      requirements.resourceName = bifEntry.filename;
      requirements.startingThreadCount = (static_cast<int>(std::thread::hardware_concurrency() * 4));
      requirements.priority = TaskPriority::HIGH;
      requirements.resourceAccess = ResourceAccess::SHARED;
      requirements.blocking = true;
      requirements.saveProfile = false;

      // Submit task with high priority
      auto future =
          monitor.submitTaskWithRequirements(
              [this, bifIndex, bifEntry]()
                  -> std::pair<unsigned int, std::map<uint32_t, uint32_t>> {
                Log(DEBUG, "BIFService", "Processing BIF {}: {}", bifIndex, bifEntry.filename);
                
                // Load the BIF archive
                BIFArchive* archive = getBIFArchive(bifIndex);
                if (!archive) {
                  Log(ERROR, "BIFService", "Failed to load BIF archive {}: {}",
                      bifIndex, bifEntry.filename);
                  return {bifIndex, {}};
                }
                
                // Build size index for this BIF
                if (!archive->buildCompleteSizeIndex()) {
                  Log(ERROR, "BIFService",
                      "Failed to build size index for BIF {}: {}", bifIndex,
                      bifEntry.filename);
                  return {bifIndex, {}};
                }
                
                // Get the size index from this BIF
                const auto& bifSizeIndex = archive->getSizeIndex();
                Log(DEBUG, "BIFService", "Indexed {} resources from BIF {} ({} bytes)", 
                    bifSizeIndex.size(), bifIndex, bifSizeIndex.size() > 0 ? 
                    std::accumulate(bifSizeIndex.begin(), bifSizeIndex.end(), 0ULL, 
                        [](uint64_t sum, const auto& pair) { return sum + pair.second; }) : 0);
                
                return {bifIndex, bifSizeIndex};
              },
              requirements, "bif_index_" + std::to_string(bifIndex));

      futures.push_back(std::move(future));
    }

    Log(DEBUG, "BIFService", "Waiting for {} exclusive BIF indexing tasks to complete", futures.size());
    
    // Collect results from all parallel tasks
    for (auto& future : futures) {
        try {
            auto [bifIndex, bifSizeIndex] = future.get();
            
            // Add to complete index
            for (const auto& [locator, size] : bifSizeIndex) {
                // We need to map the locator back to a resource name and type
                // This requires coordination with KEYService to get the resource mapping
                // For now, we'll store by locator and provide a method to look up by locator
                completeSizeIndex[std::make_pair(std::to_string(locator), 0)] = size;
                totalSize += size;
            }
            
            totalResources += bifSizeIndex.size();
            
        } catch (const std::exception& e) {
          Log(ERROR, "BIFService", "Exception during parallel BIF indexing: {}",
              e.what());
        }
    }
    
    Log(DEBUG, "BIFService", "Complete size index built: {} resources, {} total bytes", 
        totalResources, totalSize);
    return true;
}

uint32_t BIFService::getResourceSize(const std::string& resourceName, SClass_ID resourceType) const {
    std::lock_guard<std::mutex> lock(sizeIndexMutex);
    
    auto it = completeSizeIndex.find(std::make_pair(resourceName, resourceType));
    if (it != completeSizeIndex.end()) {
        return it->second;
    }
    
    return 0; // Not found in index
}

size_t BIFService::getTotalIndexedSize() const {
    std::lock_guard<std::mutex> lock(sizeIndexMutex);
    
    size_t totalSize = 0;
    for (const auto& [key, size] : completeSizeIndex) {
        totalSize += size;
    }
    return totalSize;
}

size_t BIFService::getIndexedResourceCount() const {
    std::lock_guard<std::mutex> lock(sizeIndexMutex);
    return completeSizeIndex.size();
}

} // namespace ProjectIE4k 