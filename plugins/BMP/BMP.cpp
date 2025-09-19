#include "BMP.h"
#include "SR.hpp"
#include "LM.hpp"
#include "HM.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>

#include <opencv2/imgcodecs.hpp>  // For imread, imwrite, imdecode, imencode
#include <opencv2/imgproc.hpp>    // For resize, INTER_NEAREST
#include <algorithm>              // For std::transform

#include "core/Logging/Logging.h"
#include "core/SClassID.h"
#include "core/CFG.h"       // For PIE4K_CFG
#include "plugins/CommandRegistry.h"
#include "services/ServiceManager.h"
#include "services/ResourceService/ResourceCoordinatorService.h"

namespace ProjectIE4k {

BMP::BMP(const std::string& resourceName) 
    : PluginBase(resourceName, IE_BMP_CLASS_ID), valid_(false) {
    // Detect format
    if (!detectFormat()) {
        Log(ERROR, "BMP", "Failed to detect BMP format");
        return;
    }
    valid_ = true;
}

BMP::~BMP() {
    // Clean shutdown - no need to log destruction
}

bool BMP::extract() {
    Log(MESSAGE, "BMP", "Starting BMP extraction for resource: {}", resourceName_);
    
    // Convert to PNG
    if (!convertBmpToPng()) {
        Log(ERROR, "BMP", "Failed to convert BMP to PNG");
        return false;
    }
    
    Log(MESSAGE, "BMP", "Successfully extracted BMP: {}", resourceName_);
    return true;
}

bool BMP::upscale() {
    if (!valid_) {
        Log(ERROR, "BMP", "BMP file not loaded or invalid");
        return false;
    }
    
    // Check if this is a multi-resolution set (L/M/S pattern)
    if (isMultiResolutionSet()) {
        std::string suffix = getResolutionSuffix();
        
        if (suffix == "M" || suffix == "S") {
            // For M/S versions, upscale the L version instead
            std::string baseName = getBaseName();
            // since we skip resource coordinator service that normalizes paths we need to do it here
            std::transform(baseName.begin(), baseName.end(), baseName.begin(), ::toupper);
            std::string largeVersion = baseName + "L";
            
            Log(MESSAGE, "BMP", "Multi-resolution {} version detected, upscaling L version instead: {} -> {}", 
                suffix, resourceName_, largeVersion);
            
            // Create a BMP instance for the L version and upscale it
            BMP largeBmp(largeVersion);
            if (!largeBmp.isValid()) {
                Log(ERROR, "BMP", "Failed to load L version for multi-resolution set: {}", largeVersion);
                return false;
            }
            
            // Check if L version is already upscaled
            std::string largeUpscaledFile = largeBmp.getUpscaledDir(false) + "/" + largeVersion + ".png";
            bool needsUpscaling = !std::filesystem::exists(largeUpscaledFile);
            
            bool success = true;
            if (needsUpscaling) {
                // Upscale the L version
                success = largeBmp.PluginBase::upscale();
                if (success) {
                    Log(MESSAGE, "BMP", "Successfully upscaled L version for multi-resolution set: {}", largeVersion);
                }
            } else {
                Log(DEBUG, "BMP", "L version already upscaled, skipping upscaling: {}", largeVersion);
            }
            
            if (success) {
                // Copy the upscaled L version to the current M/S version's directory
                std::string currentUpscaledDir = getUpscaledDir(true);
                std::string currentUpscaledFile = currentUpscaledDir + "/" + resourceName_ + ".png";
                
                try {
                    std::filesystem::copy_file(largeUpscaledFile, currentUpscaledFile, 
                                             std::filesystem::copy_options::overwrite_existing);
                    
                    Log(MESSAGE, "BMP", "Copied upscaled L version to {} version: {} -> {}", 
                        suffix, largeUpscaledFile, currentUpscaledFile);
                } catch (const std::filesystem::filesystem_error& e) {
                    Log(ERROR, "BMP", "Failed to copy upscaled L version to {} version: {}", suffix, e.what());
                    success = false;
                }
            }
            
            return success;
        } else {
            // For L version, proceed with normal upscaling
            Log(MESSAGE, "BMP", "Multi-resolution L version detected, using standard AI upscaling: {}", 
                resourceName_);
        }
    }
    
    // Check if this is an area map that needs special handling
    if (isAreaMapFile()) {
        Log(MESSAGE, "BMP", "Area map detected, using data structure upscaling: {}", resourceName_);
        return upscaleAreaMapBmp();
    } else {
        // For regular BMPs, use the parent's AI upscaling
        Log(MESSAGE, "BMP", "Regular BMP, using standard AI upscaling: {}", resourceName_);
        return PluginBase::upscale();
    }
}

bool BMP::assemble() {
    Log(MESSAGE, "BMP", "Starting BMP assembly for resource: {}", resourceName_);
    
    // Check if this is an area map that needs special handling
    if (isAreaMapFile()) {
        return assembleAreaMapBmp();
    }
    
    // Convert PNG to BMP
    if (!convertPngToBmp()) {
        Log(ERROR, "BMP", "Failed to convert PNG to BMP");
        return false;
    }
    
    Log(MESSAGE, "BMP", "Successfully assembled BMP: {}", resourceName_);
    return true;
}

// Path management overrides
std::string BMP::getOutputDir(bool ensureDir) const {
    return constructPath("-bmp", ensureDir);
}

std::string BMP::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-bmp-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string BMP::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-bmp-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string BMP::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-bmp-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

// Clean directories before operations - operation-specific
bool BMP::cleanExtractDirectory() {
    Log(DEBUG, "BMP", "Cleaning extract directory for resource: {}", resourceName_);
    return cleanDirectory(getExtractDir(false));
}

bool BMP::cleanUpscaleDirectory() {
    Log(DEBUG, "BMP", "Cleaning upscale directory for resource: {}", resourceName_);
    return cleanDirectory(getUpscaledDir(false));
}

bool BMP::cleanAssembleDirectory() {
    Log(DEBUG, "BMP", "Cleaning assemble directory for resource: {}", resourceName_);
    return cleanDirectory(getAssembleDir(false));
}

bool BMP::cleanDirectory(const std::string& dir) {
    if (std::filesystem::exists(dir)) {
        try {
            std::filesystem::remove_all(dir);
            Log(DEBUG, "BMP", "Cleaned directory: {}", dir);
            return true;
        } catch (const std::filesystem::filesystem_error& e) {
            Log(ERROR, "BMP", "Failed to clean directory {}: {}", dir, e.what());
            return false;
        }
    }
    return true; // Directory doesn't exist, nothing to clean
}

bool BMP::detectFormat() {
    if (originalFileData.size() < 2 ||
        originalFileData[0] != 'B' ||
        originalFileData[1] != 'M') {
        Log(ERROR, "BMP", "Invalid BMP signature");
        return false;
    }

    Log(DEBUG, "BMP", "Detected valid BMP format");
    return true;
}

bool BMP::convertBmpToPng() {
    Log(DEBUG, "BMP", "Converting BMP to PNG for resource: {}", resourceName_);
    
    // Check if this is an area map that needs special handling
    if (isAreaMapFile()) {
        return extractAreaMapBmp();
    }
    
    // Use OpenCV to read BMP and convert to PNG
    cv::Mat image = cv::imdecode(originalFileData, cv::IMREAD_COLOR);
    
    if (image.empty()) {
        Log(ERROR, "BMP", "Failed to decode BMP image");
        return false;
    }

    // Create output directory
    std::string outputDir = getExtractDir(true);
    std::string outputPath = outputDir + "/" + resourceName_ + ".png";
    
    // Save as PNG
    if (!cv::imwrite(outputPath, image)) {
        Log(ERROR, "BMP", "Failed to save PNG file: {}", outputPath);
        return false;
    }
    
    Log(DEBUG, "BMP", "Successfully converted BMP to PNG: {}", outputPath);
    return true;
}

bool BMP::skipPngToBmp() {
    Log(DEBUG, "BMP", "Skipping PNG to BMP conversion for resource: {}", resourceName_);
    
    std::string upscalePath = getUpscaledDir(false);
    std::string assemblePath = getAssembleDir(true);
    
    // Find the PNG file in the upscale directory
    std::string pngFile = upscalePath + "/" + resourceName_ + ".png";
    
    if (!std::filesystem::exists(pngFile)) {
        Log(ERROR, "BMP", "PNG file not found in upscale directory: {}", pngFile);
        return false;
    }
    
    // Copy the file to the assemble directory
    std::string assembleFile = assemblePath + "/" + resourceName_ + ".png";
    
    try {
        std::filesystem::copy_file(pngFile, assembleFile, std::filesystem::copy_options::overwrite_existing);
        Log(DEBUG, "BMP", "Copied {} to assemble directory", resourceName_);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "BMP", "Failed to copy file: {}", e.what());
        return false;
    }
}

bool BMP::convertPngToBmp() {
    // Check if this is an area map that needs special handling
    if (isAreaMapFile()) {
        // return convertAreaMapPngToBmp();
        // don't need to convert from PNG to BMP since we are just copying original for now.
        skipPngToBmp();
    }
    
    // Get upscaled directory
    std::string upscaledPath = getUpscaledDir();
    // Look for PNG file in upscaled directory
    std::string inputFile = upscaledPath + "/" + resourceName_ + ".png";
    if (!std::filesystem::exists(inputFile)) {
        Log(ERROR, "BMP", "PNG file not found: {}", inputFile);
        return false;
    }
    // Get assemble directory
    std::string assemblePath = getAssembleDir();
    // Create output BMP filename
    std::string outputFile = assemblePath + "/" + originalFileName;
    Log(DEBUG, "BMP", "Converting PNG to BMP: {} -> {}", inputFile, outputFile);
    // Use OpenCV to read the PNG
    cv::Mat pngMat = cv::imread(inputFile, cv::IMREAD_UNCHANGED);
    if (pngMat.empty()) {
        Log(ERROR, "BMP", "Failed to read PNG file: {}", inputFile);
        return false;
    }
    Log(DEBUG, "BMP", "PNG dimensions: {}x{}, channels: {}", pngMat.cols, pngMat.rows, pngMat.channels());
    // Save as BMP using OpenCV (no special parameters needed)
    if (!cv::imwrite(outputFile, pngMat)) {
        Log(ERROR, "BMP", "Failed to save BMP: {}", outputFile);
        return false;
    }
    Log(DEBUG, "BMP", "Successfully converted PNG to BMP: {}x{} -> {}", pngMat.cols, pngMat.rows, outputFile);
    return true;
}

bool BMP::isAreaMapFile() const {
    std::string resourceUpper = resourceName_;
    std::transform(resourceUpper.begin(), resourceUpper.end(), resourceUpper.begin(), ::toupper);
    
    // Check for area map suffixes
    return resourceUpper.find("SR") != std::string::npos ||  // Search maps
           resourceUpper.find("LM") != std::string::npos ||  // Light maps  
           resourceUpper.find("HT") != std::string::npos;    // Height maps
}

bool BMP::skipUpscaling() {
    Log(DEBUG, "BMP", "Skipping upscaling for resource: {}", resourceName_);
    
    std::string extractPath = getExtractDir(false);
    std::string upscalePath = getUpscaledDir(true);
    
    // Find the PNG file in the extract directory
    std::string pngFile = extractPath + "/" + resourceName_ + ".png";
    
    if (!std::filesystem::exists(pngFile)) {
        Log(ERROR, "BMP", "PNG file not found in extract directory: {}", pngFile);
        return false;
    }
    
    // Copy the file to the upscale directory
    std::string upscaleFile = upscalePath + "/" + resourceName_ + ".png";
    
    try {
        std::filesystem::copy_file(pngFile, upscaleFile, std::filesystem::copy_options::overwrite_existing);
        Log(DEBUG, "BMP", "Copied {} to upscale directory", resourceName_);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "BMP", "Failed to copy file: {}", e.what());
        return false;
    }
}

// Batch operations (implemented by PluginManager)
bool BMP::extractAll() {
    return ProjectIE4k::PluginManager::getInstance().extractAllResourcesOfType(IE_BMP_CLASS_ID);
}

bool BMP::upscaleAll() {
    return ProjectIE4k::PluginManager::getInstance().upscaleAllResourcesOfType(IE_BMP_CLASS_ID);
}

bool BMP::assembleAll() {
    return ProjectIE4k::PluginManager::getInstance().assembleAllResourcesOfType(IE_BMP_CLASS_ID);
}

void BMP::registerCommands(CommandTable& commandTable) {
    commandTable["bmp"] = {
        "BMP file operations",
        {
            {"extract", {
                "Extract BMP resource to PNG image (e.g., bmp extract ar0110ht)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: bmp extract <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().extractResource(args[0], IE_BMP_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {
                "Upscale BMP frames (e.g., bmp upscale ar0110ht)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: bmp upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().upscaleResource(args[0], IE_BMP_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {
                "Assemble PNG image into BMP file (e.g., bmp assemble ar0110ht)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: bmp assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().assembleResource(args[0], IE_BMP_CLASS_ID) ? 0 : 1;
                }
            }},
        }
    };
}

// Area Map Data Structure Methods

bool BMP::extractAreaMapBmp() {
    Log(DEBUG, "BMP", "Extracting area map BMP: {}", resourceName_);
    
    // Create output directory
    std::string outputDir = getExtractDir(true);
    std::string outputPath = outputDir + "/" + resourceName_ + originalExtension;
    
    // For area maps, we just copy the original BMP data
    std::ofstream outputFile(outputPath, std::ios::binary);
    if (!outputFile.is_open()) {
        Log(ERROR, "BMP", "Failed to create output file: {}", outputPath);
        return false;
    }
    
    outputFile.write(reinterpret_cast<const char*>(originalFileData.data()), originalFileData.size());
    
    if (outputFile.fail()) {
        Log(ERROR, "BMP", "Failed to write BMP data to: {}", outputPath);
        return false;
    }
    
    Log(DEBUG, "BMP", "Successfully extracted area map BMP: {} bytes -> {}", originalFileData.size(), outputPath);
    return true;
}

bool BMP::upscaleAreaMapBmp() {
    Log(DEBUG, "BMP", "Upscaling area map BMP: {}", resourceName_);
    
    // Get upscale factor
    uint32_t upscaleFactor = PIE4K_CFG.UpScaleFactor;
    
    // Determine area map type and handle accordingly
    std::string resourceUpper = resourceName_;
    std::transform(resourceUpper.begin(), resourceUpper.end(), resourceUpper.begin(), ::toupper);
    
    if (resourceUpper.find("SR") != std::string::npos) {
        // Search Map (SR)
        SearchMap searchMap;
        if (!searchMap.deserialize(originalFileData)) {
            Log(ERROR, "BMP", "Failed to deserialize search map: {}", resourceName_);
            return false;
        }
        
        Log(DEBUG, "BMP", "Search map loaded: {}x{}", searchMap.width, searchMap.height);
        
        // Upscale the search map with exact nearest-neighbor replication for maximum fidelity.
        // This preserves the original 4-bit values exactly (each source pixel becomes a factor x factor block).
        // If future tuning is needed, we can expose a CFG switch to enable upscaleConservative.
        searchMap.upscale(upscaleFactor);
        
        Log(DEBUG, "BMP", "Search map upscaled (exact NN) to: {}x{}", searchMap.width, searchMap.height);
        
        // Serialize the upscaled search map
        std::vector<uint8_t> upscaledData = searchMap.serialize();
        
        // Save to upscale directory
        std::string upscalePath = getUpscaledDir(true) + "/" + resourceName_ + originalExtension;
        std::ofstream outputFile(upscalePath, std::ios::binary);
        if (!outputFile.is_open()) {
            Log(ERROR, "BMP", "Failed to create upscaled file: {}", upscalePath);
            return false;
        }
        
        outputFile.write(reinterpret_cast<const char*>(upscaledData.data()), upscaledData.size());
        
        if (outputFile.fail()) {
            Log(ERROR, "BMP", "Failed to write upscaled search map: {}", upscalePath);
            return false;
        }
        
        Log(DEBUG, "BMP", "Successfully upscaled search map: {} bytes -> {}", upscaledData.size(), upscalePath);
        return true;
        
    } else if (resourceUpper.find("LM") != std::string::npos) {
        // Light Map (LM)
        LightMap lightMap;
        if (!lightMap.deserialize(originalFileData)) {
            Log(ERROR, "BMP", "Failed to deserialize light map: {}", resourceName_);
            return false;
        }
        
        Log(DEBUG, "BMP", "Light map loaded: {}x{}", lightMap.width, lightMap.height);
        
        // Upscale the light map
        lightMap.upscale(upscaleFactor);
        
        Log(DEBUG, "BMP", "Light map upscaled to: {}x{}", lightMap.width, lightMap.height);
        
        // Serialize the upscaled light map
        std::vector<uint8_t> upscaledData = lightMap.serialize();
        
        // Save to upscale directory
        std::string upscalePath = getUpscaledDir(true) + "/" + resourceName_ + originalExtension;
        std::ofstream outputFile(upscalePath, std::ios::binary);
        if (!outputFile.is_open()) {
            Log(ERROR, "BMP", "Failed to create upscaled file: {}", upscalePath);
            return false;
        }
        
        outputFile.write(reinterpret_cast<const char*>(upscaledData.data()), upscaledData.size());
        
        if (outputFile.fail()) {
            Log(ERROR, "BMP", "Failed to write upscaled light map: {}", upscalePath);
            return false;
        }
        
        Log(DEBUG, "BMP", "Successfully upscaled light map: {} bytes -> {}", upscaledData.size(), upscalePath);
        return true;
        
    } else if (resourceUpper.find("HT") != std::string::npos) {
        // Height Map (HT)
        HeightMap heightMap;
        if (!heightMap.deserialize(originalFileData)) {
            Log(ERROR, "BMP", "Failed to deserialize height map: {}", resourceName_);
            return false;
        }
        
        Log(DEBUG, "BMP", "Height map loaded: {}x{}", heightMap.width, heightMap.height);
        
        // Upscale the height map
        heightMap.upscale(upscaleFactor);
        
        Log(DEBUG, "BMP", "Height map upscaled to: {}x{}", heightMap.width, heightMap.height);
        
        // Serialize the upscaled height map
        std::vector<uint8_t> upscaledData = heightMap.serialize();
        
        // Save to upscale directory
        std::string upscalePath = getUpscaledDir(true) + "/" + resourceName_ + originalExtension;
        std::ofstream outputFile(upscalePath, std::ios::binary);
        if (!outputFile.is_open()) {
            Log(ERROR, "BMP", "Failed to create upscaled file: {}", upscalePath);
            return false;
        }
        
        outputFile.write(reinterpret_cast<const char*>(upscaledData.data()), upscaledData.size());
        
        if (outputFile.fail()) {
            Log(ERROR, "BMP", "Failed to write upscaled height map: {}", upscalePath);
            return false;
        }
        
        Log(DEBUG, "BMP", "Successfully upscaled height map: {} bytes -> {}", upscaledData.size(), upscalePath);
        return true;
    }
    
    Log(ERROR, "BMP", "Unknown area map type: {}", resourceName_);
    return false;
}

bool BMP::assembleAreaMapBmp() {
    Log(DEBUG, "BMP", "Assembling area map BMP: {}", resourceName_);
    
    // For area maps, we just copy the upscaled BMP to the assemble directory
    std::string upscalePath = getUpscaledDir(false) + "/" + resourceName_ + originalExtension;
    std::string assemblePath = getAssembleDir(true) + "/" + resourceName_ + originalExtension;
    
    if (!std::filesystem::exists(upscalePath)) {
        Log(ERROR, "BMP", "Upscaled BMP file not found: {}", upscalePath);
        return false;
    }
    
    try {
        std::filesystem::copy_file(upscalePath, assemblePath, std::filesystem::copy_options::overwrite_existing);
        Log(DEBUG, "BMP", "Successfully assembled area map BMP: {} -> {}", upscalePath, assemblePath);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "BMP", "Failed to copy upscaled BMP: {}", e.what());
        return false;
    }
}

// Multi-resolution handling (L/M/S pattern)
bool BMP::isMultiResolutionSet() const {
    // Check if resource name ends with L, M, or S
    if (resourceName_.length() < 2) return false;
    
    char lastChar = std::toupper(resourceName_.back());
    if (lastChar != 'L' && lastChar != 'M' && lastChar != 'S') {
        return false;
    }
    
    // Get the base name (without L/M/S suffix)
    std::string baseName = resourceName_.substr(0, resourceName_.length() - 1);
    std::transform(baseName.begin(), baseName.end(), baseName.begin(), ::toupper);
    
    // Check if all three versions (L, M, S) actually exist using ResourceCoordinatorService
    auto* resourceCoordinator = dynamic_cast<ResourceCoordinatorService*>(
        ServiceManager::getService("ResourceCoordinatorService"));
    
    if (!resourceCoordinator) {
        Log(DEBUG, "BMP", "ResourceCoordinatorService not available for multi-resolution check");
        return false;
    }
    
    std::string largeVersion = baseName + "L";
    std::string mediumVersion = baseName + "M";
    std::string smallVersion = baseName + "S";
    
    bool hasLarge = resourceCoordinator->hasResource(largeVersion, IE_BMP_CLASS_ID);
    bool hasMedium = resourceCoordinator->hasResource(mediumVersion, IE_BMP_CLASS_ID);
    bool hasSmall = resourceCoordinator->hasResource(smallVersion, IE_BMP_CLASS_ID);
    
    // Only consider it a multi-resolution set if all three versions exist
    bool isMultiRes = hasLarge && hasMedium && hasSmall;
    
    if (isMultiRes) {
        Log(DEBUG, "BMP", "Multi-resolution set detected: {} (L:{}, M:{}, S:{})", 
            baseName, hasLarge, hasMedium, hasSmall);
    }
    
    return isMultiRes;
}

std::string BMP::getBaseName() const {
    if (!isMultiResolutionSet()) return resourceName_;
    
    // Remove the last character (L/M/S) to get base name
    return resourceName_.substr(0, resourceName_.length() - 1);
}

std::string BMP::getResolutionSuffix() const {
    if (!isMultiResolutionSet()) return "";
    
    return std::string(1, std::toupper(resourceName_.back()));
}


REGISTER_PLUGIN(BMP, IE_BMP_CLASS_ID);

} // namespace ProjectIE4k
