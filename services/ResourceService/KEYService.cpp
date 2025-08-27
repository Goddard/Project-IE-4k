#include "KEYService.h"

#include <cstring>
#include <algorithm>
#include <strings.h>
#include <filesystem>

#include "core/CFG.h"
#include "core/Logging/Logging.h"
#include "core/SClassID.h"

namespace ProjectIE4k {

KEYService::KEYService() : initialized_(false) {
    memset(&keyHeader, 0, sizeof(keyHeader));
}

KEYService::~KEYService() {
    cleanup();
}

void KEYService::initializeForResourceType(SClass_ID resourceType) {
    // KEY service doesn't need resource type-specific initialization
    // It maintains a global index for all resource types
    currentResourceType_ = resourceType;
    Log(DEBUG, "KEYService", "Initialized for resource type: {}", resourceType);
}

void KEYService::cleanup() {
    std::lock_guard<std::mutex> lock(resourceIndexMutex);
    resourceIndex.clear();
    bifFiles.clear();
    initialized_ = false;
    currentResourceType_ = 0;
    Log(DEBUG, "KEYService", "Cleaned up KEY service");
}

void KEYService::onLifecycleEvent(ServiceLifecycle event, const std::string& context) {
    switch (event) {
        case ServiceLifecycle::APPLICATION_START:
            Log(DEBUG, "KEYService", "Application start event received");
            break;
            
        case ServiceLifecycle::APPLICATION_SHUTDOWN:
            Log(DEBUG, "KEYService", "Application shutdown event received");
            cleanup();
            break;
            
        default:
            // Ignore other events
            break;
    }
}

bool KEYService::initialize(const std::string& keyPath) {
    if (initialized_) {
        Log(WARNING, "KEYService", "Already initialized");
        return true;
    }
    
    keyFilePath = keyPath;
    
    if (!parseKEYFile()) {
        Log(ERROR, "KEYService", "Failed to parse KEY file: {}", keyPath);
        return false;
    }
    
    if (!resolveBIFPaths()) {
        Log(ERROR, "KEYService", "Failed to resolve BIF paths");
        return false;
    }
    
    // currently does nothing info is added on initial file parse
    buildResourceIndex();
    
    initialized_ = true;
    Log(DEBUG, "KEYService", "Successfully initialized KEY service with {} resources from {} BIF files", 
        getResourceCount(), getBIFCount());
    return true;
}

bool KEYService::hasResource(const std::string& resourceName, SClass_ID resourceType) {
    if (!initialized_) {
        return false;
    }
    
    // Check if this resource is in the known bad list
    if (isResourceKnownBad(resourceName)) {
        Log(DEBUG, "KEYService", "hasResource: skipping known bad resource: {}", resourceName);
        return false;
    }
    
    std::string normalizedName = normalizeResourceName(resourceName);
    ResourceKey key(normalizedName, resourceType);
    
    Log(DEBUG, "KEYService", "hasResource: looking for '{}' (normalized: '{}') type {}", 
        resourceName, normalizedName, resourceType);
    
    std::lock_guard<std::mutex> lock(resourceIndexMutex);
    bool found = resourceIndex.find(key) != resourceIndex.end();
    Log(DEBUG, "KEYService", "hasResource: found = {}", found);
    return found;
}

ResourceInfo KEYService::getResourceInfo(const std::string& resourceName, SClass_ID resourceType) {
    if (!initialized_) {
        return ResourceInfo();
    }
    
    // Check if this resource is in the known bad list
    if (isResourceKnownBad(resourceName)) {
        Log(DEBUG, "KEYService", "getResourceInfo: skipping known bad resource: {}", resourceName);
        return ResourceInfo();
    }
    
    std::string normalizedName = normalizeResourceName(resourceName);
    ResourceKey key(normalizedName, resourceType);
    
    Log(DEBUG, "KEYService", "getResourceInfo: looking for '{}' (normalized: '{}') type {}", 
        resourceName, normalizedName, resourceType);
    
    std::lock_guard<std::mutex> lock(resourceIndexMutex);
    auto it = resourceIndex.find(key);
    if (it != resourceIndex.end()) {
        Log(DEBUG, "KEYService", "getResourceInfo: found resource in BIF {} with locator {:08x}", 
            it->second.bifIndex, it->second.locator);
        return it->second;
    }
    
    // Debug: Let's see what resources we have for this type
    Log(WARNING, "KEYService", "getResourceInfo: resource '{}' (type {}) not found", resourceName, resourceType);
    Log(DEBUG, "KEYService", "getResourceInfo: checking what resources we have for type {}", resourceType);
    
    int count = 0;
    for (const auto& [resourceKey, resourceInfo] : resourceIndex) {
        if (resourceKey.type == resourceType) {
            if (count < 10) { // Only show first 10 to avoid spam
                Log(DEBUG, "KEYService", "getResourceInfo: available resource: '{}' in BIF {}", 
                    resourceKey.name, resourceInfo.bifIndex);
            }
            count++;
        }
    }
    Log(DEBUG, "KEYService", "getResourceInfo: total {} resources of type {}", count, resourceType);
    
    return ResourceInfo();
}

std::vector<std::string> KEYService::listResourcesByType(SClass_ID resourceType) {
    std::vector<std::string> resources;
    
    if (!initialized_) {
        return resources;
    }
    
    std::lock_guard<std::mutex> lock(resourceIndexMutex);
    for (const auto& pair : resourceIndex) {
        if (pair.first.type == resourceType) {
            resources.push_back(pair.first.name);
        }
    }
    
    return resources;
}

std::vector<BIFEntry> KEYService::getBIFEntries() const {
    return bifFiles;
}

void KEYService::rebuildIndex() {
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(resourceIndexMutex);
    resourceIndex.clear();
    buildResourceIndex();
    Log(DEBUG, "KEYService", "Rebuilt resource index with {} resources", getResourceCount());
}

size_t KEYService::getResourceCount() const {
    std::lock_guard<std::mutex> lock(resourceIndexMutex);
    return resourceIndex.size();
}

size_t KEYService::getBIFCount() const {
    return bifFiles.size();
}

bool KEYService::parseKEYFile() {
    FILE* file = fopen(keyFilePath.c_str(), "rb");
    if (!file) {
        Log(ERROR, "KEYService", "Failed to open KEY file: {}", keyFilePath);
        return false;
    }
    
    // Read KEY header as raw bytes to avoid alignment issues
    uint8_t headerData[24]; // 6 fields * 4 bytes each
    if (fread(headerData, 1, 24, file) != 24) {
        Log(ERROR, "KEYService", "Failed to read KEY header");
        fclose(file);
        return false;
    }
    
    // Extract header fields manually
    memcpy(keyHeader.signature, headerData + 0, 4);
    memcpy(keyHeader.version, headerData + 4, 4);
    keyHeader.bifCount = (headerData[8] << 0) | (headerData[9] << 8) | (headerData[10] << 16) | (headerData[11] << 24);
    keyHeader.keyCount = (headerData[12] << 0) | (headerData[13] << 8) | (headerData[14] << 16) | (headerData[15] << 24);
    keyHeader.bifOffset = (headerData[16] << 0) | (headerData[17] << 8) | (headerData[18] << 16) | (headerData[19] << 24);
    keyHeader.keyOffset = (headerData[20] << 0) | (headerData[21] << 8) | (headerData[22] << 16) | (headerData[23] << 24);
    
    // Validate signature
    if (strncmp(keyHeader.signature, "KEY ", 4) != 0) {
        Log(ERROR, "KEYService", "Invalid KEY signature: {:.4s}", keyHeader.signature);
        fclose(file);
        return false;
    }
    
    // Validate version
    if (strncmp(keyHeader.version, "V1  ", 4) != 0) {
        Log(ERROR, "KEYService", "Unsupported KEY version: {:.4s}", keyHeader.version);
        fclose(file);
        return false;
    }
    
    Log(DEBUG, "KEYService", "KEY file: {} BIF files, {} resources", 
        keyHeader.bifCount, keyHeader.keyCount);
    Log(DEBUG, "KEYService", "KEY header: bifOffset={}, keyOffset={}", 
        keyHeader.bifOffset, keyHeader.keyOffset);
    
    // Read BIF entries
    if (!readBIFEntries(file)) {
        fclose(file);
        return false;
    }
    
    // Read resource entries
    if (!readResourceEntries(file)) {
        fclose(file);
        return false;
    }
    
    fclose(file);
    return true;
}

bool KEYService::readBIFEntries(FILE* file) {
    bifFiles.resize(keyHeader.bifCount);
    
    if (fseek(file, keyHeader.bifOffset, SEEK_SET) != 0) {
        Log(ERROR, "KEYService", "Failed to seek to BIF entries");
        return false;
    }
    
    for (uint32_t i = 0; i < keyHeader.bifCount; ++i) {
        BIFEntry& entry = bifFiles[i];
        
        // Read BIF entry structure - read as raw bytes to avoid alignment issues
        uint8_t entryData[12]; // 4+4+2+2 = 12 bytes
        if (fread(entryData, 1, 12, file) != 12) {
            Log(ERROR, "KEYService", "Failed to read BIF entry {} structure", i);
            return false;
        }
        
        // Extract fields manually to ensure correct byte order
        entry.fileSize = (entryData[0] << 0) | (entryData[1] << 8) | (entryData[2] << 16) | (entryData[3] << 24);
        entry.filenameOffset = (entryData[4] << 0) | (entryData[5] << 8) | (entryData[6] << 16) | (entryData[7] << 24);
        entry.filenameLength = (entryData[8] << 0) | (entryData[9] << 8);
        entry.flags = (entryData[10] << 0) | (entryData[11] << 8);
        
        Log(DEBUG, "KEYService", "BIF entry {}: size={}, filenameOffset={}, filenameLength={}, flags={:04x}", 
            i, entry.fileSize, entry.filenameOffset, entry.filenameLength, entry.flags);
        
        // Save current position
        long currentPos = ftell(file);
        if (currentPos == -1) {
            Log(ERROR, "KEYService", "Failed to get current file position for BIF entry {}", i);
            return false;
        }
        
        // Read filename
        Log(DEBUG, "KEYService", "Reading BIF filename {}: offset={}, length={}", i, entry.filenameOffset, entry.filenameLength);
        
        if (fseek(file, entry.filenameOffset, SEEK_SET) != 0) {
            Log(ERROR, "KEYService", "Failed to seek to BIF filename {} at offset {}", i, entry.filenameOffset);
            return false;
        }
        
        std::vector<char> filename(entry.filenameLength);
        size_t bytesRead = fread(filename.data(), 1, entry.filenameLength, file);
        if (bytesRead != entry.filenameLength) {
            Log(ERROR, "KEYService", "Failed to read BIF filename {}: expected {} bytes, got {} bytes", i, entry.filenameLength, bytesRead);
            return false;
        }
        
        // Create string from filename, stopping at NUL character
        // The filenameLength includes the NUL terminator, so we need to exclude it
        size_t actualLength = strnlen(filename.data(), entry.filenameLength);
        entry.filename = std::string(filename.data(), actualLength);
        
        // Convert Windows backslashes to forward slashes for cross-platform compatibility
        std::replace(entry.filename.begin(), entry.filename.end(), '\\', '/');
        
        Log(DEBUG, "KEYService", "BIF filename {}: '{}'", i, entry.filename);
        entry.found = false; // Will be resolved later
        
        // Return to original position to continue reading BIF entries
        if (fseek(file, currentPos, SEEK_SET) != 0) {
            Log(ERROR, "KEYService", "Failed to return to original position for BIF entry {}", i);
            return false;
        }
    }
    
    return true;
}

bool KEYService::readResourceEntries(FILE* file) {
    if (fseek(file, keyHeader.keyOffset, SEEK_SET) != 0) {
        Log(ERROR, "KEYService", "Failed to seek to resource entries");
        return false;
    }
    
    std::vector<ResourceEntry> resourceEntries(keyHeader.keyCount);
    
    for (uint32_t i = 0; i < keyHeader.keyCount; ++i) {
        ResourceEntry& entry = resourceEntries[i];
        
        if (fread(&entry.name, sizeof(entry.name), 1, file) != 1 ||
            fread(&entry.type, sizeof(entry.type), 1, file) != 1 ||
            fread(&entry.locator, sizeof(entry.locator), 1, file) != 1) {
            Log(ERROR, "KEYService", "Failed to read resource entry {}", i);
            return false;
        }
    }
    
    // Build resource index from entries
    std::lock_guard<std::mutex> lock(resourceIndexMutex);
    for (const auto& entry : resourceEntries) {
        std::string name(entry.name, 8);
        // Remove null padding
        size_t nullPos = name.find('\0');
        if (nullPos != std::string::npos) {
            name.erase(nullPos);
        }
        
        if (name.empty()) continue;
        
        // Normalize the resource name to uppercase for consistent lookups
        std::string normalizedName = normalizeResourceName(name);
        
        // Extract BIF index and offset from locator
        uint32_t bifIndex = (entry.locator & 0xFFF00000) >> 20;
        uint32_t offset = entry.locator & 0x000FFFFF;
        
        // For TIS files, also extract tileset index for BIF file matching
        uint32_t tilesetIndex = (entry.locator & 0x0003F000) >> 14;  // Bits 14-19 (6 bits)
        uint32_t fileIndex = entry.locator & 0x00003FFF;            // Bits 0-13 (14 bits)
        
        if (bifIndex < bifFiles.size()) {
            // Check if this resource is in the known bad list
            if (isResourceKnownBad(name)) {
                Log(DEBUG, "KEYService", "Skipping known bad resource: {}", name);
                continue;
            }
            
            ResourceKey key(normalizedName, entry.type);
            // Store original name in ResourceInfo for plugins, but use normalized name for lookup key
            resourceIndex[key] = ResourceInfo(name, entry.type, bifIndex, entry.locator, offset, 0);
        }
    }
    
    return true;
}

bool KEYService::resolveBIFPaths() {
    std::filesystem::path keyPath(keyFilePath);
    std::filesystem::path gameDir = keyPath.parent_path();
    std::filesystem::path dataDir = gameDir / "data";
    
    int foundCount = 0;
    int notFoundCount = 0;
    
    for (auto& bifEntry : bifFiles) {
        // The BIF filenames in the KEY file are relative to the data directory
        // We need to find the actual file using case-insensitive comparison
        std::filesystem::path relativePath(bifEntry.filename);
        std::filesystem::path targetPath = dataDir / relativePath;
        
        // Try to find the actual file with case-insensitive comparison
        std::filesystem::path actualPath = findCaseInsensitivePath(targetPath);
        
        if (!actualPath.empty() && std::filesystem::exists(actualPath)) {
            bifEntry.fullPath = actualPath.string();
            bifEntry.found = true;
            foundCount++;
        } else {
            // Fallback: try common variations
            std::filesystem::path fallbackPath = tryCommonVariations(dataDir, relativePath);
            if (!fallbackPath.empty() && std::filesystem::exists(fallbackPath)) {
                bifEntry.fullPath = fallbackPath.string();
                bifEntry.found = true;
                foundCount++;
            } else {
                bifEntry.fullPath = targetPath.string();
                bifEntry.found = false;
                notFoundCount++;
                Log(DEBUG, "KEYService", "BIF file not found: {}", targetPath.string());
            }
        }
    }
    
    Log(DEBUG, "KEYService", "BIF file statistics: {} found, {} not found (total: {})", 
        foundCount, notFoundCount, bifFiles.size());
    
    // Debug: Log some BIF files and their indices
    Log(DEBUG, "KEYService", "BIF file mapping (first 20):");
    for (size_t i = 0; i < std::min(bifFiles.size(), size_t(20)); i++) {
        Log(DEBUG, "KEYService", "  BIF {}: {} (found: {})", 
            i, bifFiles[i].filename, bifFiles[i].found);
    }
    
    return true;
}

void KEYService::buildResourceIndex() {
    // This is called from readResourceEntries, so the index is already built
    Log(DEBUG, "KEYService", "Resource index built with {} entries", resourceIndex.size());
}

std::string KEYService::normalizeResourceName(const std::string& name) {
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);
    return normalized;
}

bool KEYService::findBIFFile(BIFEntry& bifEntry) {
    // This is handled in resolveBIFPaths
    return bifEntry.found;
}

std::filesystem::path KEYService::findCaseInsensitivePath(const std::filesystem::path& targetPath) {
    if (std::filesystem::exists(targetPath)) {
        return targetPath;
    }
    
    // Get the parent directory and filename
    std::filesystem::path parent = targetPath.parent_path();
    std::string filename = targetPath.filename().string();
    
    if (!std::filesystem::exists(parent)) {
        return std::filesystem::path();
    }
    
    // Try to find the file with case-insensitive comparison
    for (const auto& entry : std::filesystem::directory_iterator(parent)) {
        if (entry.is_regular_file()) {
            std::string entryName = entry.path().filename().string();
            if (strcasecmp(entryName.c_str(), filename.c_str()) == 0) {
                return entry.path();
            }
        }
    }
    
    return std::filesystem::path();
}

std::filesystem::path KEYService::tryCommonVariations(const std::filesystem::path& dataDir, const std::filesystem::path& relativePath) {
    std::string pathStr = relativePath.string();
    std::string filename = relativePath.filename().string();
    
    // Try common variations
    std::vector<std::filesystem::path> variations = {
        dataDir / "Data" / filename,
        dataDir / "DATA" / filename,
        dataDir / "data" / filename,
        dataDir / "Movies" / filename,
        dataDir / "MOVIES" / filename,
        dataDir / "movies" / filename,
        dataDir / filename
    };
    
    for (const auto& variation : variations) {
        if (std::filesystem::exists(variation)) {
            return variation;
        }
    }
    
    return std::filesystem::path();
}

bool KEYService::isResourceKnownBad(const std::string& resourceName) const {
    const auto& knownBadResources = PIE4K_CFG.ResourceKnownBad;
    for (const auto& badResource : knownBadResources) {
        if (resourceName == badResource) {
            return true;
        }
    }
    return false;
}

} // namespace ProjectIE4k
