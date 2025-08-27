#include "ResourceCoordinatorService.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <set>
#include <thread>
#include <iostream>
#include <chrono>
#include <sstream>

#include "core/Logging/Logging.h"
#include "core/SClassID.h"
#include "core/CFG.h"
#include "services/ServiceBase.h"

namespace ProjectIE4k {

ResourceCoordinatorService::ResourceCoordinatorService() {
    keyService = std::make_unique<KEYService>();
    bifService = std::make_unique<BIFService>();
}

void ResourceCoordinatorService::initializeForResourceType(SClass_ID resourceType) {
    std::lock_guard<std::mutex> lock(serviceMutex);
    
    currentResourceType_ = resourceType;
    
    if (keyService) {
        keyService->initializeForResourceType(resourceType);
    }
    if (bifService) {
        bifService->initializeForResourceType(resourceType);
    }
}

void ResourceCoordinatorService::cleanup() {
    std::lock_guard<std::mutex> lock(serviceMutex);
    
    if (keyService) {
        keyService->cleanup();
    }
    if (bifService) {
        bifService->cleanup();
    }
    
    initialized_ = false;
    currentResourceType_ = 0;
}

bool ResourceCoordinatorService::hasResource(const std::string& resourceName, SClass_ID resourceType) {
    if (!initialized_) {
        Log(ERROR, "ResourceCoordinatorService", "Service not initialized");
        return false;
    }

    std::lock_guard<std::mutex> lock(serviceMutex);
    
    // First check override directory (highest priority)
    if (hasResourceInOverride(resourceName, resourceType)) {
        Log(DEBUG, "ResourceCoordinatorService", "Found resource '{}' in override directory", resourceName);
        return true;
    }
    
    // Then check BIF files
    if (keyService && keyService->hasResource(resourceName, resourceType)) {
        return true;
    }
    
    return false;
}

ResourceData ResourceCoordinatorService::getResourceData(const std::string& resourceName, SClass_ID resourceType) {
    Log(DEBUG, "ResourceCoordinatorService", "getResourceData called: name={}, type={}", resourceName, resourceType);
    
    if (!initialized_) {
        Log(ERROR, "ResourceCoordinatorService", "Service not initialized");
        return ResourceData();
    }

    std::lock_guard<std::mutex> lock(serviceMutex);
    
    // First check override directory (highest priority)
    if (hasResourceInOverride(resourceName, resourceType)) {
        Log(DEBUG, "ResourceCoordinatorService", "Loading resource '{}' from override directory", resourceName);
        std::vector<uint8_t> data = getResourceDataFromOverride(resourceName, resourceType);
        
        // For override files, get the actual filename from the override file map
        std::string normalizedName = normalizeResourceName(resourceName);
        auto it = overrideFileMap.find(std::make_pair(normalizedName, resourceType));
        if (it != overrideFileMap.end()) {
            const OverrideFileInfo& fileInfo = it->second;
            std::string filename = fileInfo.originalFilename;
            return ResourceData(data, filename);
        } else {
            // Fallback: construct filename using SClass extension
            std::string extension = SClass::getExtension(resourceType);
            std::string filename = resourceName + "." + extension;
            return ResourceData(data, filename);
        }
    }
    
    // Then check BIF files
    Log(DEBUG, "ResourceCoordinatorService", "About to call keyService->getResourceInfo");
    ResourceInfo resourceInfo = keyService->getResourceInfo(resourceName, resourceType);
    Log(DEBUG, "ResourceCoordinatorService", "keyService->getResourceInfo returned: valid={}, bifIndex={}, offset={}, size={}", 
        resourceInfo.isValid, resourceInfo.bifIndex, resourceInfo.offset, resourceInfo.size);
    
    if (!resourceInfo.isValid) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to get resource info: {} (type: {})", resourceName, resourceType);
        return ResourceData();
    }
    
    Log(DEBUG, "ResourceCoordinatorService", "About to call bifService->getResourceData");
    std::vector<uint8_t> data = bifService->getResourceData(resourceInfo);
    Log(DEBUG, "ResourceCoordinatorService", "bifService->getResourceData returned: {} bytes", data.size());
    
    if (data.empty()) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to extract resource data: {} (type: {})", resourceName, resourceType);
        return ResourceData();
    }
    
    // Create filename with extension using SClass functions
    std::string extension = SClass::getExtension(resourceType);
    std::string filename = resourceName + "." + extension;
    
    return ResourceData(data, filename);
}

std::vector<std::string> ResourceCoordinatorService::listResourcesByType(SClass_ID resourceType) {
    if (!initialized_) {
        Log(ERROR, "ResourceCoordinatorService", "Service not initialized");
        return {};
    }

    std::lock_guard<std::mutex> lock(serviceMutex);
    
    // Ensure BIFService is fully initialized before listing resources
    if (bifService && !bifService->isInitialized()) {
        Log(MESSAGE, "ResourceCoordinatorService", "Waiting for BIFService to complete initialization...");
        
        // Wait for BIFService to complete initialization
        while (bifService && !bifService->isInitialized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        if (!bifService || !bifService->isInitialized()) {
            Log(ERROR, "ResourceCoordinatorService", "BIFService failed to initialize - cannot list resources");
            return {};
        }
        
        Log(MESSAGE, "ResourceCoordinatorService", "BIFService initialization completed, proceeding with resource listing");
        FlushLogs(); // Ensure the completion message is written immediately
    }
    
    std::vector<std::string> resources;
    std::set<std::string> resourceSet; // Use set to avoid duplicates
    
    // Get resources from BIF files
    if (keyService) {
        std::vector<std::string> bifResources = keyService->listResourcesByType(resourceType);
        for (const auto& resource : bifResources) {
            resourceSet.insert(resource);
        }
    }
    
    // Scan override directory for loose files of this type
    if (!overridePath.empty()) {
        std::string extension = SClass::getExtension(resourceType);
        if (!extension.empty()) {
            std::vector<std::string> overrideResources = scanOverrideDirectoryForType(resourceType, extension);
            for (const auto& resource : overrideResources) {
                resourceSet.insert(resource);
            }
        }
    }
    
    // Convert set back to vector
    resources.assign(resourceSet.begin(), resourceSet.end());
    
    Log(DEBUG, "ResourceCoordinatorService", "Found {} resources of type {} ({} from BIF, {} from override)", 
        resources.size(), resourceType, 
        keyService ? keyService->listResourcesByType(resourceType).size() : 0,
        resources.size() - (keyService ? keyService->listResourcesByType(resourceType).size() : 0));
    
    return resources;
}

size_t ResourceCoordinatorService::getResourceCount() const {
    if (!initialized_) {
        return 0;
    }
    return keyService->getResourceCount();
}

size_t ResourceCoordinatorService::getBIFCount() const {
    if (!initialized_) {
        return 0;
    }
    return bifService->getBIFCount();
}

size_t ResourceCoordinatorService::getCacheSize() const {
    if (!initialized_) {
        return 0;
    }
    return bifService->getCacheSize();
}

bool ResourceCoordinatorService::initializeServices() {
    if (!keyService || !bifService) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to create sub-services");
        return false;
    }
    
    return true;
}

void ResourceCoordinatorService::optimizeForResourceType(SClass_ID resourceType) {
    // This could include preloading common BIF files for the resource type
    // For now, just log the optimization
    Log(DEBUG, "ResourceCoordinatorService", "Optimized for resource type: {}", resourceType);
}

bool ResourceCoordinatorService::validateResourceName(const std::string& resourceName) const {
    return !resourceName.empty() && resourceName.length() <= 8;
}

std::string ResourceCoordinatorService::normalizeResourceName(const std::string& resourceName) const {
    std::string normalized = resourceName;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);
    return normalized;
}

bool ResourceCoordinatorService::initializeFromGamePath(const std::string& gamePath) {
    auto startTime = std::chrono::steady_clock::now();
    std::cout << "Reading resource index..." << std::endl;
    
    this->gamePath = gamePath;
    
    // Set up override path
    overridePath = (std::filesystem::path(gamePath) / "override").string();
    
    Log(DEBUG, "ResourceCoordinatorService", "Initializing with game path: {}", gamePath);
    Log(DEBUG, "ResourceCoordinatorService", "Override path: {}", overridePath);
    
    // Find KEY file
    std::string keyPath = findKEYFile(gamePath);
    if (keyPath.empty()) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to find KEY file in game path: {}", gamePath);
        return false;
    }
    
    // Initialize sub-services
    if (!initializeServices()) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to initialize sub-services");
        return false;
    }
    
    // Initialize KEY service
    if (!keyService->initialize(keyPath)) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to initialize KEY service");
        return false;
    }
    
    // Initialize BIF service with BIF entries from KEY service
    // This will use exclusive priority and must complete before any batch operations
    Log(MESSAGE, "ResourceCoordinatorService", "Initializing BIF service with exclusive priority...");
    std::vector<BIFEntry> bifEntries = keyService->getBIFEntries();
    if (!bifService->initialize(bifEntries)) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to initialize BIF service");
        return false;
    }
    
    // Verify BIF service is fully initialized
    if (!bifService->isInitialized()) {
        Log(ERROR, "ResourceCoordinatorService", "BIF service initialization incomplete");
        return false;
    }
    
    // Force flush logs to ensure initialization messages are written immediately
    FlushLogs();
    
    // Build the override file map for fast lookups
    buildOverrideFileMap();
    
    initialized_ = true;
    Log(MESSAGE, "ResourceCoordinatorService", "Successfully initialized resource coordinator service");
    
    // Calculate and output timing
    auto endTime = std::chrono::steady_clock::now();
    auto duration = endTime - startTime;
    
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration - hours);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration - hours - minutes);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration - hours - minutes - seconds);
    
    std::ostringstream oss;
    if (hours.count() > 0) {
        oss << hours.count() << "h ";
    }
    if (minutes.count() > 0 || hours.count() > 0) {
        oss << minutes.count() << "m ";
    }
    if (seconds.count() > 0 || minutes.count() > 0 || hours.count() > 0) {
        oss << seconds.count() << "s";
        if (milliseconds.count() > 0) {
            oss << " " << milliseconds.count() << "ms";
        }
    } else {
        // Less than 1 second, show milliseconds
        oss << milliseconds.count() << "ms";
    }
    
    std::cout << "Resource index loaded in " << oss.str() << std::endl;
    
    return true;
}

bool ResourceCoordinatorService::hasResourceInOverride(const std::string& resourceName, SClass_ID resourceType) const {
    if (overrideFileMap.empty()) {
        return false;
    }
    
    // Normalize the resource name for case-insensitive lookup
    std::string normalizedName = resourceName;
    std::transform(normalizedName.begin(), normalizedName.end(), normalizedName.begin(), ::toupper);
    
    // Check if the resource exists in our override map with the specific resource type
    auto it = overrideFileMap.find(std::make_pair(normalizedName, resourceType));
    if (it != overrideFileMap.end()) {
        Log(DEBUG, "ResourceCoordinatorService", "Found resource '{}' (type: {}) in override map", resourceName, resourceType);
        return true;
    }
    
    return false;
}

std::vector<uint8_t> ResourceCoordinatorService::getResourceDataFromOverride(const std::string& resourceName, SClass_ID resourceType) const {
    if (overrideFileMap.empty()) {
        return std::vector<uint8_t>();
    }
    
    // Normalize the resource name for case-insensitive lookup
    std::string normalizedName = resourceName;
    std::transform(normalizedName.begin(), normalizedName.end(), normalizedName.begin(), ::toupper);
    
    // Find the file info in our override map with the specific resource type
    auto it = overrideFileMap.find(std::make_pair(normalizedName, resourceType));
    if (it == overrideFileMap.end()) {
        Log(ERROR, "ResourceCoordinatorService", "Resource '{}' (type: {}) not found in override map", resourceName, resourceType);
        return std::vector<uint8_t>();
    }
    
    const OverrideFileInfo& fileInfo = it->second;
    std::filesystem::path filePath(fileInfo.fullPath);
    
    if (!std::filesystem::exists(filePath)) {
        Log(ERROR, "ResourceCoordinatorService", "Override file not found: {}", filePath.string());
        return std::vector<uint8_t>();
    }
    
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to open override file: {}", filePath.string());
        return std::vector<uint8_t>();
    }
    
    // Use the pre-cached file size
    std::streamsize fileSize = static_cast<std::streamsize>(fileInfo.fileSize);
    
    // Read file data
    std::vector<uint8_t> data(fileSize);
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to read override file: {}", filePath.string());
        return std::vector<uint8_t>();
    }
    
    Log(DEBUG, "ResourceCoordinatorService", "Successfully loaded {} bytes from override file: {}", data.size(), filePath.string());
    return data;
}

std::string ResourceCoordinatorService::findKEYFile(const std::string& gamePath) const {
    // Look for CHITIN.KEY in the game root directory with case insensitive search
    std::filesystem::path gameDir(gamePath);
    std::filesystem::path keyPath = gameDir / "CHITIN.KEY";
    
    // Use case insensitive search
    std::filesystem::path foundPath = findCaseInsensitivePath(keyPath);
    if (!foundPath.empty()) {
        Log(DEBUG, "ResourceCoordinatorService", "Found KEY file: {}", foundPath.string());
        return foundPath.string();
    }
    
    // Debug: List all .KEY files in the game directory
    Log(DEBUG, "ResourceCoordinatorService", "CHITIN.KEY not found in game directory: {}", gamePath);
    Log(DEBUG, "ResourceCoordinatorService", "Searching for other KEY files in game directory:");
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(gameDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.find(".KEY") != std::string::npos || filename.find(".key") != std::string::npos) {
                    Log(DEBUG, "ResourceCoordinatorService", "  Found KEY file: {}", filename);
                }
            }
        }
    } catch (const std::exception& e) {
        Log(ERROR, "ResourceCoordinatorService", "Error searching for KEY files: {}", e.what());
    }
    
    return "";
}

std::filesystem::path ResourceCoordinatorService::findCaseInsensitivePath(const std::filesystem::path& targetPath) const {
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

std::vector<std::string> ResourceCoordinatorService::scanOverrideDirectoryForType(SClass_ID resourceType, const std::string& extension) const {
    std::vector<std::string> resources;
    
    if (overridePath.empty()) {
        return resources;
    }
    
    std::filesystem::path overrideDir(overridePath);
    if (!std::filesystem::exists(overrideDir) || !std::filesystem::is_directory(overrideDir)) {
        Log(DEBUG, "ResourceCoordinatorService", "Override directory does not exist or is not a directory: {}", overridePath);
        return resources;
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(overrideDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                
                // Check if the file has the correct extension (case insensitive)
                std::string fileExtension = entry.path().extension().string();
                std::transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), ::tolower);
                std::string expectedExtension = "." + extension;
                std::transform(expectedExtension.begin(), expectedExtension.end(), expectedExtension.begin(), ::tolower);
                
                if (fileExtension == expectedExtension) {
                    // Extract the resource name (filename without extension)
                    std::string resourceName = entry.path().stem().string();
                    
                    // Validate the resource name (should be 8 characters or less for IE games)
                    if (validateResourceName(resourceName)) {
                        // Normalize the resource name (convert to uppercase)
                        std::string normalizedName =
                            normalizeResourceName(resourceName);

                        // Check if this resource is in the known bad list
                        if (!PIE4K_CFG.isResourceKnownBad(normalizedName)) {
                          resources.push_back(resourceName);
                          Log(DEBUG, "ResourceCoordinatorService",
                              "Found override resource: {} (type: {})",
                              normalizedName, resourceType);
                        } else {
                          Log(DEBUG, "ResourceCoordinatorService",
                              "Skipping known bad override resource: {} (type: "
                              "{})",
                              normalizedName, resourceType);
                        }
                    } else {
                        Log(WARNING, "ResourceCoordinatorService", "Invalid resource name in override: {} (type: {})", resourceName, resourceType);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        Log(ERROR, "ResourceCoordinatorService", "Error scanning override directory: {}", e.what());
    }
    
    Log(DEBUG, "ResourceCoordinatorService", "Found {} override resources of type {}", resources.size(), resourceType);
    return resources;
}

void ResourceCoordinatorService::buildOverrideFileMap() {
    overrideFileMap.clear();
    
    if (overridePath.empty()) {
        Log(DEBUG, "ResourceCoordinatorService", "No override path set, skipping override file map build");
        return;
    }
    
    std::filesystem::path overrideDir(overridePath);
    if (!std::filesystem::exists(overrideDir) || !std::filesystem::is_directory(overrideDir)) {
        Log(DEBUG, "ResourceCoordinatorService", "Override directory does not exist or is not a directory: {}", overridePath);
        return;
    }
    
    Log(DEBUG, "ResourceCoordinatorService", "Building override file map from: {}", overridePath);
    
    size_t totalSize = 0;
    size_t fileCount = 0;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(overrideDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                std::string baseName = entry.path().stem().string();
                std::string extension = entry.path().extension().string();
                
                // Get file size during indexing
                uint64_t fileSize = entry.file_size();
                
                // Normalize the base name (convert to uppercase for case-insensitive lookup)
                std::transform(baseName.begin(), baseName.end(), baseName.begin(), ::toupper);
                
                // Convert extension to resource type
                std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);
                SClass_ID resourceType = SClass::getResourceTypeFromExtension(extension);
                
                if (resourceType != 0) {
                  // Check if this resource is in the known bad list
                  if (!PIE4K_CFG.isResourceKnownBad(baseName)) {
                    // Store the mapping from (base name, resource type) to file info with size
                    OverrideFileInfo fileInfo(entry.path().string(), fileSize, filename);
                    overrideFileMap[std::make_pair(baseName, resourceType)] = fileInfo;

                    totalSize += fileSize;
                    fileCount++;

                    if (verbose) {
                      Log(DEBUG, "ResourceCoordinatorService",
                          "Added to override map: {} (type: {}) -> {} ({} "
                          "bytes)",
                          baseName, resourceType, entry.path().string(),
                          fileSize);
                    }
                  } else {
                    Log(DEBUG, "ResourceCoordinatorService",
                        "Skipping known bad override resource: {} (type: {})",
                        baseName, resourceType);
                  }
                } else {
                    Log(DEBUG, "ResourceCoordinatorService", "Skipping file with unknown extension: {}", filename);
                }
            }
        }
    } catch (const std::exception& e) {
        Log(ERROR, "ResourceCoordinatorService", "Error building override file map: {}", e.what());
    }
    
    Log(DEBUG, "ResourceCoordinatorService", "Built override file map with {} entries (total size: {} bytes)", 
        overrideFileMap.size(), totalSize);
}

// New methods for resource size information
uint64_t ResourceCoordinatorService::getResourceSize(const std::string& resourceName, SClass_ID resourceType) const {
    std::lock_guard<std::mutex> lock(serviceMutex);
    
    // First check override files
    std::string normalizedName = normalizeResourceName(resourceName);
    auto it = overrideFileMap.find(std::make_pair(normalizedName, resourceType));
    if (it != overrideFileMap.end()) {
        return it->second.fileSize;
    }
    
    // Then check BIF files using the complete size index
    if (bifService) {
        uint32_t size = bifService->getResourceSize(resourceName, resourceType);
        if (size > 0) {
            return size;
        }
    }
    
    // Fallback to KEY service (lazy loading)
    if (keyService) {
        ResourceInfo resourceInfo = keyService->getResourceInfo(resourceName, resourceType);
        if (resourceInfo.isValid) {
            return resourceInfo.size;
        }
    }
    
    return 0; // Resource not found
}

std::vector<std::pair<std::string, uint64_t>> ResourceCoordinatorService::getResourcesWithSizes(SClass_ID resourceType) const {
    std::lock_guard<std::mutex> lock(serviceMutex);
    std::vector<std::pair<std::string, uint64_t>> resourcesWithSizes;
    
    // Get resources from BIF files
    if (keyService) {
        std::vector<std::string> bifResources = keyService->listResourcesByType(resourceType);
        for (const auto& resource : bifResources) {
            ResourceInfo resourceInfo = keyService->getResourceInfo(resource, resourceType);
            if (resourceInfo.isValid) {
                resourcesWithSizes.emplace_back(resource, resourceInfo.size);
            }
        }
    }
    
    // Get resources from override files
    for (const auto& [key, fileInfo] : overrideFileMap) {
        if (key.second == resourceType) {
            resourcesWithSizes.emplace_back(key.first, fileInfo.fileSize);
        }
    }
    
    return resourcesWithSizes;
}

size_t ResourceCoordinatorService::getTotalSizeForResourceType(SClass_ID resourceType) const {
    std::lock_guard<std::mutex> lock(serviceMutex);
    size_t totalSize = 0;
    
    // Sum up BIF file sizes
    if (keyService) {
        std::vector<std::string> bifResources = keyService->listResourcesByType(resourceType);
        for (const auto& resource : bifResources) {
            ResourceInfo resourceInfo = keyService->getResourceInfo(resource, resourceType);
            if (resourceInfo.isValid) {
                totalSize += resourceInfo.size;
            }
        }
    }
    
    // Sum up override file sizes
    for (const auto& [key, fileInfo] : overrideFileMap) {
        if (key.second == resourceType) {
            totalSize += fileInfo.fileSize;
        }
    }
    
    return totalSize;
}

void ResourceCoordinatorService::onLifecycleEvent(ServiceLifecycle event, const std::string& context) {
    switch (event) {
        case ServiceLifecycle::APPLICATION_START:
            Log(DEBUG, "ResourceCoordinatorService", "Application start event received");
            // Initialize from config game path
            if (!initialized_) {
                Log(DEBUG, "ResourceCoordinatorService", "Initializing ResourceCoordinatorService from config");
                if (!initializeFromGamePath(PIE4K_CFG.GamePath)) {
                    Log(ERROR, "ResourceCoordinatorService", "Failed to initialize from config game path: {}", PIE4K_CFG.GamePath);
                }
            }
            break;
            
        case ServiceLifecycle::APPLICATION_SHUTDOWN:
            Log(DEBUG, "ResourceCoordinatorService", "Application shutdown event received");
            cleanup();
            break;
            
        case ServiceLifecycle::RESOURCE_TYPE_START:
            if (!context.empty()) {
                try {
                    SClass_ID resourceType = static_cast<SClass_ID>(std::stoi(context));
                    Log(DEBUG, "ResourceCoordinatorService", "Resource type start event received for type: {}", resourceType);
                    initializeForResourceType(resourceType);
                } catch (const std::exception& e) {
                    Log(ERROR, "ResourceCoordinatorService", "Failed to parse resource type from context: {}", context);
                }
            }
            break;
            
        case ServiceLifecycle::RESOURCE_TYPE_END:
            Log(DEBUG, "ResourceCoordinatorService", "Resource type end event received");
            // Could implement cleanup per resource type if needed
            break;
            
        default:
            // Ignore other events
            break;
    }
}

// Register the service dynamically
REGISTER_SERVICE(ResourceCoordinatorService)

} // namespace ProjectIE4k
