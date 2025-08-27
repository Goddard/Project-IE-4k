#include "PNG.h"

#include <fstream>
#include <filesystem>
#include <iostream>

#include "core/SClassID.h"
#include "core/Logging/Logging.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

PNG::PNG(const std::string& resourceName) 
    : PluginBase(resourceName, IE_PNG_CLASS_ID), valid_(false) {
    // Detect format
    if (!detectFormat()) {
        Log(ERROR, "PNG", "Failed to detect PNG format");
        return;
    }
    valid_ = true;
}

PNG::~PNG() {
    // Clean shutdown - no need to log destruction
}

bool PNG::extract() {
    Log(MESSAGE, "PNG", "Starting PNG extraction for resource: {}", resourceName_);
    
    // Check if this is a palette file that should be preserved
    if (isPaletteFile()) {
        Log(DEBUG, "PNG", "Palette file detected, preserving without processing: {}", resourceName_);
        return convertPngToPng(); // Just copy the PNG without any modifications
    }
    
    // Convert to PNG
    if (!convertPngToPng()) {
        Log(ERROR, "PNG", "Failed to convert PNG to PNG");
        return false;
    }
    
    Log(MESSAGE, "PNG", "Successfully extracted PNG: {}", resourceName_);
    return true;
}

bool PNG::assemble() {
    Log(MESSAGE, "PNG", "Starting PNG assembly for resource: {}", resourceName_);
    
    // Check if this is a palette file that should be preserved
    if (isPaletteFile()) {
        Log(DEBUG, "PNG", "Palette file detected, copying from extract directory: {}", resourceName_);
        
        // Get extract directory
        std::string extractPath = getExtractDir();
        
        // Look for PNG file in extract directory
        std::string inputFile = extractPath + "/" + resourceName_ + ".png";
        
        if (!std::filesystem::exists(inputFile)) {
            Log(ERROR, "PNG", "PNG file not found in extract directory: {}", inputFile);
            return false;
        }
        
        // Get assemble directory
        std::string assemblePath = getAssembleDir();
        
        // Create output PNG filename
        std::string outputFile = assemblePath + "/" + originalFileName;
        
        Log(DEBUG, "PNG", "Copying palette PNG from extract: {} -> {}", inputFile, outputFile);
        
        // Copy the file from extract to assemble directory
        try {
            std::filesystem::copy_file(inputFile, outputFile, std::filesystem::copy_options::overwrite_existing);
        } catch (const std::filesystem::filesystem_error& e) {
            Log(ERROR, "PNG", "Failed to copy palette PNG file: {}", e.what());
            return false;
        }
        
        Log(MESSAGE, "PNG", "Successfully copied palette PNG: {} -> {}", inputFile, outputFile);
        return true;
    }
    
    // Get upscaled directory
    std::string upscaledPath = getUpscaledDir();
    
    // Look for PNG file in upscaled directory
    std::string inputFile = upscaledPath + "/" + resourceName_ + ".png";
    
    if (!std::filesystem::exists(inputFile)) {
        Log(ERROR, "PNG", "PNG file not found: {}", inputFile);
        return false;
    }
    
    // Get assemble directory
    std::string assemblePath = getAssembleDir();
    
    // Create output PNG filename
    std::string outputFile = assemblePath + "/" + resourceName_ + originalExtension;
    
    Log(DEBUG, "PNG", "Copying PNG: {} -> {}", inputFile, outputFile);
    
    // Simply copy the file from upscaled to assemble directory
    try {
        std::filesystem::copy_file(inputFile, outputFile, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "PNG", "Failed to copy PNG file: {}", e.what());
        return false;
    }
    
    Log(MESSAGE, "PNG", "Successfully copied PNG: {} -> {}", inputFile, outputFile);
    return true;
}

// PluginBase interface implementation
// Using default service-based upscale() from PluginBase

// Path management overrides
std::string PNG::getOutputDir(bool ensureDir) const {
    return constructPath("-png", ensureDir);
}

std::string PNG::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-png-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string PNG::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-png-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string PNG::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-png-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

// Clean directories before operations - operation-specific
bool PNG::cleanExtractDirectory() {
    Log(DEBUG, "PNG", "Cleaning extract directory for resource: {}", resourceName_);
    return cleanDirectory(getExtractDir(false));
}

bool PNG::cleanUpscaleDirectory() {
    Log(DEBUG, "PNG", "Cleaning upscale directory for resource: {}", resourceName_);
    return cleanDirectory(getUpscaledDir(false));
}

bool PNG::cleanAssembleDirectory() {
    Log(DEBUG, "PNG", "Cleaning assemble directory for resource: {}", resourceName_);
    return cleanDirectory(getAssembleDir(false));
}

bool PNG::cleanDirectory(const std::string& dir) {
    if (std::filesystem::exists(dir)) {
        try {
            std::filesystem::remove_all(dir);
            Log(DEBUG, "PNG", "Cleaned directory: {}", dir);
            return true;
        } catch (const std::filesystem::filesystem_error& e) {
            Log(ERROR, "PNG", "Failed to clean directory {}: {}", dir, e.what());
            return false;
        }
    }
    return true; // Directory doesn't exist, nothing to clean
}

bool PNG::detectFormat() {
    if (originalFileData.size() < 8) {
        Log(ERROR, "PNG", "PNG data too small to detect format");
        return false;
    }

    // Check PNG signature
    const uint8_t pngSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(originalFileData.data(), pngSignature, 8) != 0) {
        Log(ERROR, "PNG", "Invalid PNG signature");
        return false;
    }

    Log(DEBUG, "PNG", "Detected valid PNG format");
    return true;
}

bool PNG::isPaletteFile() const {
    // Check for specific palette files that should not be processed
    return resourceName_ == "pal16" || resourceName_ == "palette";
}

bool PNG::convertPngToPng() {
    // Get extract directory
    std::string extractPath = getExtractDir();
    
    // Create output PNG filename
    std::string outputFile = extractPath + "/" + resourceName_ + ".png";
    
    Log(MESSAGE, "PNG", "Extracting PNG: {} -> {}", resourceName_, outputFile);
    
    // Simply write the original PNG data directly to the extract directory
    std::ofstream outputStream(outputFile, std::ios::binary);
    if (!outputStream.is_open()) {
        Log(ERROR, "PNG", "Failed to create output file: {}", outputFile);
        return false;
    }
    
    outputStream.write(reinterpret_cast<const char*>(originalFileData.data()), originalFileData.size());
    
    if (outputStream.fail()) {
        Log(ERROR, "PNG", "Failed to write PNG data to: {}", outputFile);
        return false;
    }
    
    Log(DEBUG, "PNG", "Successfully extracted PNG: {} bytes -> {}", originalFileData.size(), outputFile);
    return true;
}

// Batch operations (implemented by PluginManager)
bool PNG::extractAll() {
    return PluginManager::getInstance().extractAllResourcesOfType(IE_PNG_CLASS_ID);
}

bool PNG::upscaleAll() {
    return PluginManager::getInstance().upscaleAllResourcesOfType(IE_PNG_CLASS_ID);
}

bool PNG::assembleAll() {
    return PluginManager::getInstance().assembleAllResourcesOfType(IE_PNG_CLASS_ID);
}

void PNG::registerCommands(CommandTable& commandTable) {
    commandTable["png"] = {
        "PNG file operations",
        {
            {"extract", {
                "Extract PNG resource to PNG image (e.g., png extract gemrb-logo)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: png extract <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().extractResource(args[0], IE_PNG_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {
                "Upscale PNG frames (e.g., png upscale gemrb-logo)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: png upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().upscaleResource(args[0], IE_PNG_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {
                "Copy PNG image to assemble directory (e.g., png assemble gemrb-logo)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: png assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().assembleResource(args[0], IE_PNG_CLASS_ID) ? 0 : 1;
                }
            }},
        }
    };
}

REGISTER_PLUGIN(PNG, IE_PNG_CLASS_ID);

} // namespace ProjectIE4k
