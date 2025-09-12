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
#include "services/ServiceManager.h"
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

    // Then check unhardcoded directory (second priority)
    if (hasResourceInUnhardcoded(resourceName, resourceType)) {
        Log(DEBUG, "ResourceCoordinatorService", "Found resource '{}' in unhardcoded directory", resourceName);
        return true;
    }

    // Then check unhardcoded shared directory (third priority)
    if (hasResourceInUnhardcodedShared(resourceName, resourceType)) {
        Log(DEBUG, "ResourceCoordinatorService", "Found resource '{}' in unhardcoded shared directory", resourceName);
        return true;
    }

    // Finally check BIF files (lowest priority)
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

    // Then check unhardcoded shared directory (third priority)
    if (hasResourceInUnhardcodedShared(resourceName, resourceType)) {
        Log(DEBUG, "ResourceCoordinatorService", "Loading resource '{}' from unhardcoded shared directory", resourceName);
        std::vector<uint8_t> data = getResourceDataFromUnhardcodedShared(resourceName, resourceType);

        // For unhardcoded shared files, get the actual filename from the unhardcoded shared file map
        std::string normalizedName = normalizeResourceName(resourceName);
        auto it = unhardcodedSharedFileMap.find(std::make_pair(normalizedName, resourceType));
        if (it != unhardcodedSharedFileMap.end()) {
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

    // Then check unhardcoded directory (second priority)
    if (hasResourceInUnhardcoded(resourceName, resourceType)) {
        Log(DEBUG, "ResourceCoordinatorService", "Loading resource '{}' from unhardcoded directory", resourceName);
        std::vector<uint8_t> data = getResourceDataFromUnhardcoded(resourceName, resourceType);

        // For unhardcoded files, get the actual filename from the unhardcoded file map
        std::string normalizedName = normalizeResourceName(resourceName);
        auto it = unhardcodedFileMap.find(std::make_pair(normalizedName, resourceType));
        if (it != unhardcodedFileMap.end()) {
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

    // Finally check BIF files (lowest priority)
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

    // Scan unhardcoded shared directory for loose files of this type
    if (!unhardcodedSharedPath.empty()) {
        std::string extension = SClass::getExtension(resourceType);
        if (!extension.empty()) {
            std::vector<std::string> unhardcodedSharedResources = scanUnhardcodedSharedDirectoryForType(resourceType, extension);
            for (const auto& resource : unhardcodedSharedResources) {
                resourceSet.insert(resource);
            }
        }
    }

    // Scan unhardcoded directory for loose files of this type
    if (!unhardcodedPath.empty()) {
        std::string extension = SClass::getExtension(resourceType);
        if (!extension.empty()) {
            std::vector<std::string> unhardcodedResources = scanUnhardcodedDirectoryForType(resourceType, extension);
            for (const auto& resource : unhardcodedResources) {
                resourceSet.insert(resource);
            }
        }
    }

    // Convert set back to vector
    resources.assign(resourceSet.begin(), resourceSet.end());

    size_t bifCount = keyService ? keyService->listResourcesByType(resourceType).size() : 0;
    size_t overrideCount = overridePath.empty() ? 0 :
        (keyService ? (resources.size() - bifCount - (unhardcodedSharedPath.empty() ? 0 :
            scanUnhardcodedSharedDirectoryForType(resourceType, SClass::getExtension(resourceType)).size()) - (unhardcodedPath.empty() ? 0 :
            scanUnhardcodedDirectoryForType(resourceType, SClass::getExtension(resourceType)).size())) : 0);
    size_t unhardcodedSharedCount = unhardcodedSharedPath.empty() ? 0 :
        scanUnhardcodedSharedDirectoryForType(resourceType, SClass::getExtension(resourceType)).size();
    size_t unhardcodedCount = unhardcodedPath.empty() ? 0 :
        scanUnhardcodedDirectoryForType(resourceType, SClass::getExtension(resourceType)).size();

    Log(DEBUG, "ResourceCoordinatorService", "Found {} resources of type {} ({} from BIF, {} from override, {} from unhardcoded shared, {} from unhardcoded)",
        resources.size(), resourceType, bifCount, overrideCount, unhardcodedSharedCount, unhardcodedCount);
    
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

    // Set up unhardcoded path using GameType from config
    std::filesystem::path installPath = std::filesystem::path(PIE4K_CFG.GemRBPath);
    unhardcodedPath = (installPath / "unhardcoded" / PIE4K_CFG.GameType).string();
    unhardcodedSharedPath = (installPath / "unhardcoded" / "shared").string();

    Log(DEBUG, "ResourceCoordinatorService", "Initializing with game path: {}", gamePath);
    Log(DEBUG, "ResourceCoordinatorService", "Override path: {}", overridePath);
    Log(DEBUG, "ResourceCoordinatorService", "Unhardcoded path: {}", unhardcodedPath);
    Log(DEBUG, "ResourceCoordinatorService", "Unhardcoded shared path: {}", unhardcodedSharedPath);
    
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

    // Build the unhardcoded file map for fast lookups
    buildUnhardcodedFileMap();
    
    // Build the unhardcoded shared file map for fast lookups
    buildUnhardcodedSharedFileMap();
    
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

bool ResourceCoordinatorService::hasResourceInUnhardcoded(const std::string& resourceName, SClass_ID resourceType) const {
    if (unhardcodedFileMap.empty()) {
        return false;
    }

    // Normalize the resource name for case-insensitive lookup
    std::string normalizedName = resourceName;
    std::transform(normalizedName.begin(), normalizedName.end(), normalizedName.begin(), ::toupper);

    // Check if the resource exists in our unhardcoded map with the specific resource type
    auto it = unhardcodedFileMap.find(std::make_pair(normalizedName, resourceType));
    if (it != unhardcodedFileMap.end()) {
        Log(DEBUG, "ResourceCoordinatorService", "Found resource '{}' (type: {}) in unhardcoded map", resourceName, resourceType);
        return true;
    }

    return false;
}

std::vector<uint8_t> ResourceCoordinatorService::getResourceDataFromUnhardcoded(const std::string& resourceName, SClass_ID resourceType) const {
    if (unhardcodedFileMap.empty()) {
        return std::vector<uint8_t>();
    }

    // Normalize the resource name for case-insensitive lookup
    std::string normalizedName = resourceName;
    std::transform(normalizedName.begin(), normalizedName.end(), normalizedName.begin(), ::toupper);

    // Find the file info in our unhardcoded map with the specific resource type
    auto it = unhardcodedFileMap.find(std::make_pair(normalizedName, resourceType));
    if (it == unhardcodedFileMap.end()) {
        Log(ERROR, "ResourceCoordinatorService", "Resource '{}' (type: {}) not found in unhardcoded map", resourceName, resourceType);
        return std::vector<uint8_t>();
    }

    const OverrideFileInfo& fileInfo = it->second;
    std::filesystem::path filePath(fileInfo.fullPath);

    if (!std::filesystem::exists(filePath)) {
        Log(ERROR, "ResourceCoordinatorService", "Unhardcoded file not found: {}", filePath.string());
        return std::vector<uint8_t>();
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to open unhardcoded file: {}", filePath.string());
        return std::vector<uint8_t>();
    }

    // Use the pre-cached file size
    std::streamsize fileSize = static_cast<std::streamsize>(fileInfo.fileSize);

    // Read file data
    std::vector<uint8_t> data(fileSize);
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to read unhardcoded file: {}", filePath.string());
        return std::vector<uint8_t>();
    }

    Log(DEBUG, "ResourceCoordinatorService", "Successfully loaded {} bytes from unhardcoded file: {}", data.size(), filePath.string());
    return data;
}

bool ResourceCoordinatorService::hasResourceInUnhardcodedShared(const std::string& resourceName, SClass_ID resourceType) const {
    if (unhardcodedSharedFileMap.empty()) {
        return false;
    }

    // Normalize the resource name for case-insensitive lookup
    std::string normalizedName = resourceName;
    std::transform(normalizedName.begin(), normalizedName.end(), normalizedName.begin(), ::toupper);

    // Check if the resource exists in our unhardcoded shared map with the specific resource type
    auto it = unhardcodedSharedFileMap.find(std::make_pair(normalizedName, resourceType));
    if (it != unhardcodedSharedFileMap.end()) {
        Log(DEBUG, "ResourceCoordinatorService", "Found resource '{}' (type: {}) in unhardcoded shared map", resourceName, resourceType);
        return true;
    }

    return false;
}

std::vector<uint8_t> ResourceCoordinatorService::getResourceDataFromUnhardcodedShared(const std::string& resourceName, SClass_ID resourceType) const {
    if (unhardcodedSharedFileMap.empty()) {
        return std::vector<uint8_t>();
    }

    // Normalize the resource name for case-insensitive lookup
    std::string normalizedName = resourceName;
    std::transform(normalizedName.begin(), normalizedName.end(), normalizedName.begin(), ::toupper);

    // Find the file info in our unhardcoded shared map with the specific resource type
    auto it = unhardcodedSharedFileMap.find(std::make_pair(normalizedName, resourceType));
    if (it == unhardcodedSharedFileMap.end()) {
        Log(ERROR, "ResourceCoordinatorService", "Resource '{}' (type: {}) not found in unhardcoded shared map", resourceName, resourceType);
        return std::vector<uint8_t>();
    }

    const OverrideFileInfo& fileInfo = it->second;
    std::filesystem::path filePath(fileInfo.fullPath);

    if (!std::filesystem::exists(filePath)) {
        Log(ERROR, "ResourceCoordinatorService", "Unhardcoded shared file not found: {}", filePath.string());
        return std::vector<uint8_t>();
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to open unhardcoded shared file: {}", filePath.string());
        return std::vector<uint8_t>();
    }

    // Use the pre-cached file size
    std::streamsize fileSize = static_cast<std::streamsize>(fileInfo.fileSize);

    // Read file data
    std::vector<uint8_t> data(fileSize);
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) {
        Log(ERROR, "ResourceCoordinatorService", "Failed to read unhardcoded shared file: {}", filePath.string());
        return std::vector<uint8_t>();
    }

    Log(DEBUG, "ResourceCoordinatorService", "Successfully loaded {} bytes from unhardcoded shared file: {}", data.size(), filePath.string());
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

std::vector<std::string> ResourceCoordinatorService::scanUnhardcodedDirectoryForType(SClass_ID resourceType, const std::string& extension) const {
    std::vector<std::string> resources;

    if (unhardcodedPath.empty()) {
        return resources;
    }

    std::filesystem::path unhardcodedDir(unhardcodedPath);
    if (!std::filesystem::exists(unhardcodedDir) || !std::filesystem::is_directory(unhardcodedDir)) {
        Log(DEBUG, "ResourceCoordinatorService", "Unhardcoded directory does not exist or is not a directory: {}", unhardcodedPath);
        return resources;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(unhardcodedDir)) {
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
                              "Found unhardcoded resource: {} (type: {})",
                              normalizedName, resourceType);
                        } else {
                          Log(DEBUG, "ResourceCoordinatorService",
                              "Skipping known bad unhardcoded resource: {} (type: "
                              "{})",
                              normalizedName, resourceType);
                        }
                    } else {
                        Log(WARNING, "ResourceCoordinatorService", "Invalid resource name in unhardcoded: {} (type: {})", resourceName, resourceType);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        Log(ERROR, "ResourceCoordinatorService", "Error scanning unhardcoded directory: {}", e.what());
    }

    Log(DEBUG, "ResourceCoordinatorService", "Found {} unhardcoded resources of type {}", resources.size(), resourceType);
    return resources;
}

std::vector<std::string> ResourceCoordinatorService::scanUnhardcodedSharedDirectoryForType(SClass_ID resourceType, const std::string& extension) const {
    std::vector<std::string> resources;

    if (unhardcodedSharedPath.empty()) {
        return resources;
    }

    std::filesystem::path unhardcodedSharedDir(unhardcodedSharedPath);
    if (!std::filesystem::exists(unhardcodedSharedDir) || !std::filesystem::is_directory(unhardcodedSharedDir)) {
        Log(DEBUG, "ResourceCoordinatorService", "Unhardcoded shared directory does not exist or is not a directory: {}", unhardcodedSharedPath);
        return resources;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(unhardcodedSharedDir)) {
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
                              "Found unhardcoded shared resource: {} (type: {})",
                              normalizedName, resourceType);
                        } else {
                          Log(DEBUG, "ResourceCoordinatorService",
                              "Skipping known bad unhardcoded shared resource: {} (type: "
                              "{})",
                              normalizedName, resourceType);
                        }
                    } else {
                        Log(WARNING, "ResourceCoordinatorService", "Invalid resource name in unhardcoded shared: {} (type: {})", resourceName, resourceType);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        Log(ERROR, "ResourceCoordinatorService", "Error scanning unhardcoded shared directory: {}", e.what());
    }

    Log(DEBUG, "ResourceCoordinatorService", "Found {} unhardcoded shared resources of type {}", resources.size(), resourceType);
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

void ResourceCoordinatorService::buildUnhardcodedFileMap() {
    unhardcodedFileMap.clear();

    if (unhardcodedPath.empty()) {
        Log(DEBUG, "ResourceCoordinatorService", "No unhardcoded path set, skipping unhardcoded file map build");
        return;
    }

    std::filesystem::path unhardcodedDir(unhardcodedPath);
    if (!std::filesystem::exists(unhardcodedDir) || !std::filesystem::is_directory(unhardcodedDir)) {
        Log(DEBUG, "ResourceCoordinatorService", "Unhardcoded directory does not exist or is not a directory: {}", unhardcodedPath);
        return;
    }

    Log(DEBUG, "ResourceCoordinatorService", "Building unhardcoded file map from: {}", unhardcodedPath);

    size_t totalSize = 0;
    size_t fileCount = 0;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(unhardcodedDir)) {
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
                    unhardcodedFileMap[std::make_pair(baseName, resourceType)] = fileInfo;

                    totalSize += fileSize;
                    fileCount++;

                    if (verbose) {
                      Log(DEBUG, "ResourceCoordinatorService",
                          "Added to unhardcoded map: {} (type: {}) -> {} ({} "
                          "bytes)",
                          baseName, resourceType, entry.path().string(),
                          fileSize);
                    }
                  } else {
                    Log(DEBUG, "ResourceCoordinatorService",
                        "Skipping known bad unhardcoded resource: {} (type: {})",
                        baseName, resourceType);
                  }
                } else {
                    Log(DEBUG, "ResourceCoordinatorService", "Skipping file with unknown extension: {}", filename);
                }
            }
        }
    } catch (const std::exception& e) {
        Log(ERROR, "ResourceCoordinatorService", "Error building unhardcoded file map: {}", e.what());
    }

    Log(DEBUG, "ResourceCoordinatorService", "Built unhardcoded file map with {} entries (total size: {} bytes)",
        unhardcodedFileMap.size(), totalSize);
}

void ResourceCoordinatorService::buildUnhardcodedSharedFileMap() {
    unhardcodedSharedFileMap.clear();
    
    if (unhardcodedSharedPath.empty()) {
        Log(DEBUG, "ResourceCoordinatorService", "No unhardcoded shared path set, skipping unhardcoded shared file map build");
        return;
    }
    
    std::filesystem::path unhardcodedSharedDir(unhardcodedSharedPath);
    if (!std::filesystem::exists(unhardcodedSharedDir) || !std::filesystem::is_directory(unhardcodedSharedDir)) {
        Log(DEBUG, "ResourceCoordinatorService", "Unhardcoded shared directory does not exist or is not a directory: {}", unhardcodedSharedPath);
        return;
    }
    
    Log(DEBUG, "ResourceCoordinatorService", "Building unhardcoded shared file map from: {}", unhardcodedSharedPath);
    
    size_t totalSize = 0;
    size_t fileCount = 0;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(unhardcodedSharedDir)) {
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
                    unhardcodedSharedFileMap[std::make_pair(baseName, resourceType)] = fileInfo;
                    totalSize += fileSize;
                    fileCount++;
                  } else {
                    Log(DEBUG, "ResourceCoordinatorService",
                        "Skipping known bad unhardcoded shared resource: {} (type: {})",
                        baseName, resourceType);
                  }
                } else {
                    Log(DEBUG, "ResourceCoordinatorService", "Skipping file with unknown extension: {}", filename);
                }
            }
        }
    } catch (const std::exception& e) {
        Log(ERROR, "ResourceCoordinatorService", "Error building unhardcoded shared file map: {}", e.what());
    }

    Log(DEBUG, "ResourceCoordinatorService", "Built unhardcoded shared file map with {} entries (total size: {} bytes)",
        unhardcodedSharedFileMap.size(), totalSize);
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

    // Then check unhardcoded shared files
    auto usit = unhardcodedSharedFileMap.find(std::make_pair(normalizedName, resourceType));
    if (usit != unhardcodedSharedFileMap.end()) {
        return usit->second.fileSize;
    }

    // Then check unhardcoded files
    auto uit = unhardcodedFileMap.find(std::make_pair(normalizedName, resourceType));
    if (uit != unhardcodedFileMap.end()) {
        return uit->second.fileSize;
    }

    // Finally check BIF files using the complete size index
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

    // Get resources from unhardcoded shared files
    for (const auto& [key, fileInfo] : unhardcodedSharedFileMap) {
        if (key.second == resourceType) {
            resourcesWithSizes.emplace_back(key.first, fileInfo.fileSize);
        }
    }

    // Get resources from unhardcoded files
    for (const auto& [key, fileInfo] : unhardcodedFileMap) {
        if (key.second == resourceType) {
            resourcesWithSizes.emplace_back(key.first, fileInfo.fileSize);
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

    // Sum up unhardcoded shared file sizes
    for (const auto& [key, fileInfo] : unhardcodedSharedFileMap) {
        if (key.second == resourceType) {
            totalSize += fileInfo.fileSize;
        }
    }

    // Sum up unhardcoded file sizes
    for (const auto& [key, fileInfo] : unhardcodedFileMap) {
        if (key.second == resourceType) {
            totalSize += fileInfo.fileSize;
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

bool ResourceCoordinatorService::list() {
    std::cout << "=== Resource Index Listing ===" << std::endl;
    std::cout << std::endl;
    
    // Get all resource types from SClass
    auto allTypes = SClass::getAllResourceTypes();
    
    // Track totals
    size_t totalResourceCount = 0;
    size_t totalTypesWithResources = 0;
    
    for (SClass_ID resourceType : allTypes) {
        auto resources = listResourcesByType(resourceType);
        
        if (!resources.empty()) {
            totalTypesWithResources++;
            totalResourceCount += resources.size();
            
            // Print type header
            std::cout << SClass::getExtension(resourceType) 
                      << " (" << SClass::getDescription(resourceType) << "): " 
                      << resources.size() << " resources" << std::endl;
            
            // Print all resources
            for (const auto& resource : resources) {
                std::cout << "  " << resource << std::endl;
            }
            
            std::cout << std::endl;
        }
    }
    
    // Print summary
    std::cout << "=== Summary ===" << std::endl;
    std::cout << "Total resource types with resources: " << totalTypesWithResources << std::endl;
    std::cout << "Total resources: " << totalResourceCount << std::endl;
    
    return true;
}

void ResourceCoordinatorService::registerCommands(CommandTable& commandTable) {
    commandTable["resources"] = {
        "Resource index operations",
        {
            {"list", {"List all resources in the index",
                [](const std::vector<std::string>& args) -> int {
                    return ServiceManager::listAllResources() ? 0 : 1;
                }
            }}
        }
    };
}

// Register the service dynamically
REGISTER_SERVICE(ResourceCoordinatorService)

} // namespace ProjectIE4k
