#include "CHU.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <iomanip>

#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

CHU::CHU(const std::string& resourceName_) 
    : PluginBase(resourceName_, IE_CHU_CLASS_ID) {
    if (!loadFromData()) {
        Log(ERROR, "CHU", "Failed to load CHU data");
        return;
    }
    
    // Mark plugin as valid since we successfully loaded the CHU resource
    valid_ = true;
}

CHU::~CHU() {
    // Clean up large data structures to prevent memory leaks
    v1_.reset();
}

bool CHU::extract() {
    Log(MESSAGE, "CHU", "Starting CHU extraction for resource: {}", resourceName_);
    
    // Create output directory
    std::string outputDir = getExtractDir(true);
    std::string outputPath = outputDir + "/" + originalFileName;
    
    // Write the original CHU data to file
    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "CHU", "Failed to open output file: {}", outputPath);
        return false;
    }
    file.write(reinterpret_cast<const char*>(originalFileData.data()), originalFileData.size());
    file.close();
    
    Log(MESSAGE, "CHU", "Successfully extracted CHU file: {} ({} bytes)", outputPath, originalFileData.size());
    return true;
}

bool CHU::assemble() {
    Log(MESSAGE, "CHU", "Starting CHU assembly for resource: {}", resourceName_);
    
    std::string upscaledPath = getUpscaledDir();
    std::string assemblePath = getAssembleDir();
    
    if (!std::filesystem::exists(upscaledPath)) {
        Log(ERROR, "CHU", "Upscaled directory not found: {}", upscaledPath);
        return false;
    }
    
    // Create assemble directory if it doesn't exist
    if (!std::filesystem::exists(assemblePath)) {
        std::filesystem::create_directories(assemblePath);
        Log(MESSAGE, "CHU", "Created assemble directory: {}", assemblePath);
    }
    
    // Copy all files from upscaled to assembled
    try {
        for (const auto& entry : std::filesystem::directory_iterator(upscaledPath)) {
            if (entry.is_regular_file()) {
                std::string sourceFile = entry.path().string();
                std::string destFile = assemblePath + "/" + originalFileName;
                
                std::filesystem::copy_file(sourceFile, destFile, std::filesystem::copy_options::overwrite_existing);
                Log(MESSAGE, "CHU", "Copied {} to {}", entry.path().filename().string(), destFile);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "CHU", "Filesystem error during assembly: {}", e.what());
        return false;
    }
    
    Log(MESSAGE, "CHU", "Successfully assembled CHU: {}", resourceName_);
    return true;
}

bool CHU::upscale() {
    if (!valid_) {
        Log(ERROR, "CHU", "CHU file not loaded or invalid");
        return false;
    }

    int ups = PIE4K_CFG.UpScaleFactor;
    Log(DEBUG, "CHU", "Upscaling CHU coordinates by factor: {}", ups);

    // Work strictly on parsed CHUV1File
    if (!v1_) {
        Log(ERROR, "CHU", "Parsed CHU V1 data not available");
        return false;
    }

    // Scale window rectangles
    for (auto &w : v1_->windows) {
        w.x *= ups; w.y *= ups; w.width *= ups; w.height *= ups;
    }
    // Scale each control common block (x,y,width,height) and type-specific coordinates
    for (auto &blob : v1_->controls) {
        if (blob.size() >= sizeof(CHUControlCommon)) {
            auto *cc = reinterpret_cast<CHUControlCommon*>(blob.data());
            cc->x *= ups; cc->y *= ups; cc->width *= ups; cc->height *= ups;
            
            // Scale type-specific coordinate fields based on control type
            uint8_t controlType = cc->type;
            switch (controlType) {
                case 0: // Button/Toggle/Pixmap
                    if (blob.size() >= sizeof(CHUControlButton)) {
                        auto *btn = reinterpret_cast<CHUControlButton*>(blob.data());
                        btn->anchorX1 = static_cast<uint8_t>(std::min(255, static_cast<int>(btn->anchorX1) * ups));
                        btn->anchorX2 = static_cast<uint8_t>(std::min(255, static_cast<int>(btn->anchorX2) * ups));
                        btn->anchorY1 = static_cast<uint8_t>(std::min(255, static_cast<int>(btn->anchorY1) * ups));
                        btn->anchorY2 = static_cast<uint8_t>(std::min(255, static_cast<int>(btn->anchorY2) * ups));
                    }
                    break;
                case 2: // Slider
                    if (blob.size() >= sizeof(CHUControlSlider)) {
                        auto *slider = reinterpret_cast<CHUControlSlider*>(blob.data());
                        slider->knobXOffset *= ups;
                        slider->knobYOffset *= ups;
                        slider->knobJumpWidth *= ups;
                    }
                    break;
                case 3: // TextEdit
                    if (blob.size() >= sizeof(CHUControlTextEdit)) {
                        auto *textEdit = reinterpret_cast<CHUControlTextEdit*>(blob.data());
                        textEdit->xCoord *= ups;
                        textEdit->yCoord *= ups;
                    }
                    break;
                // Types 5, 6, 7 (TextArea, Label, Scrollbar) don't have additional coordinate fields
                default:
                    break;
            }
        }
    }

    // Serialize updated structure
    std::vector<uint8_t> out = v1_->serialize();

    std::string outDir = getUpscaledDir(true);
    std::string outPath = outDir + "/" + originalFileName;
    std::ofstream outFile(outPath, std::ios::binary);
    if (!outFile) {
        Log(ERROR, "CHU", "Failed to open output file: {}", outPath);
        return false;
    }
    outFile.write(reinterpret_cast<const char*>(out.data()), out.size());
    outFile.close();
    Log(MESSAGE, "CHU", "Upscaled CHU written to {} ({} bytes)", outPath, out.size());
    return true;
}

bool CHU::loadFromData() {
    if (originalFileData.empty()) {
        Log(ERROR, "CHU", "No CHU data loaded");
        return false;
    }

    // Preferred: full-structure parse via CHUV1File
    v1_ = std::make_unique<CHUV1File>();
    if (!v1_->deserialize(originalFileData)) {
        Log(ERROR, "CHU", "Failed to parse CHU V1");
        v1_.reset();
        return false;
    }

    // Keep header snapshot for legacy helpers
    std::memcpy(&header, originalFileData.data(), sizeof(CHUHeader));
    Log(DEBUG, "CHU", "Successfully loaded CHU resource: {} (windows={}, controls={})", resourceName_, v1_->windows.size(), v1_->controls.size());
    return true;
}

bool CHU::readHeader() {
    if (originalFileData.size() < sizeof(CHUHeader)) {
        return false;
    }
    
    memcpy(&header, originalFileData.data(), sizeof(CHUHeader));
    Log(DEBUG, "CHU", "Header: controlTableOffset=0x{:08x} ({}), windowOffset=0x{:08x} ({}), windowCount={}", 
        header.controlTableOffset, header.controlTableOffset, header.windowOffset, header.windowOffset, header.windowCount);
    
    if (header.windowCount == 0 || header.windowCount > 256) {
        Log(ERROR, "CHU", "Unrealistic windowCount: {}", header.windowCount);
        return false;
    }
    return true;
}

bool CHU::writeHeader(std::ofstream& file) {
    Log(DEBUG, "CHU", "Writing header: controlTableOffset=0x{:08x} ({}), windowOffset=0x{:08x} ({}), windowCount={}", 
        header.controlTableOffset, header.controlTableOffset, header.windowOffset, header.windowOffset, header.windowCount);
    if (header.controlTableOffset != 20) {
        Log(ERROR, "CHU", "[WRITE] controlTableOffset is not 20! Found: {}", header.controlTableOffset);
        return false;
    }
    if (header.windowOffset <= header.controlTableOffset) {
        Log(ERROR, "CHU", "[WRITE] windowOffset ({}) is not after controlTableOffset ({})!", header.windowOffset, header.controlTableOffset);
        return false;
    }
    if (header.windowCount == 0 || header.windowCount > 256) {
        Log(ERROR, "CHU", "[WRITE] Unrealistic windowCount: {}", header.windowCount);
        return false;
    }
    file.write(reinterpret_cast<const char*>(&header), sizeof(CHUHeader));
    return !file.fail();
}

void CHU::registerCommands(CommandTable& commandTable) {
    commandTable["chu"] = {
        "CHU file operations",
        {
            {"extract", {"Extract CHU resource to file (e.g., chu extract mainmenu)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: chu extract <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().extractResource(args[0], IE_CHU_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {"Upscale CHU coordinates (e.g., chu upscale mainmenu)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: chu upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().upscaleResource(args[0], IE_CHU_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {"Assemble CHU files (e.g., chu assemble mainmenu)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: chu assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().assembleResource(args[0], IE_CHU_CLASS_ID) ? 0 : 1;
                }
            }}
        }
    };
}

// PluginBase interface implementations
bool CHU::extractAll() {
    return PluginManager::getInstance().extractAllResourcesOfType(IE_CHU_CLASS_ID);
}

bool CHU::upscaleAll() {
    return PluginManager::getInstance().upscaleAllResourcesOfType(IE_CHU_CLASS_ID);
}

bool CHU::assembleAll() {
    return PluginManager::getInstance().assembleAllResourcesOfType(IE_CHU_CLASS_ID);
}

bool CHU::cleanExtractDirectory() {
    return cleanDirectory(getOutputDir(false));
}

bool CHU::cleanUpscaleDirectory() {
    return cleanDirectory(getUpscaledDir(false));
}

bool CHU::cleanAssembleDirectory() {
    return cleanDirectory(getAssembleDir(false));
}

std::string CHU::getOutputDir(bool ensureDir) const {
    return constructPath("-chu", ensureDir);
}

std::string CHU::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-chu-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string CHU::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-chu-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string CHU::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-chu-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

bool CHU::cleanDirectory(const std::string& dir) {
    if (!std::filesystem::exists(dir)) {
        return true; // Directory doesn't exist, nothing to clean
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::filesystem::remove(entry.path());
                Log(DEBUG, "CHU", "Cleaned file: {}", entry.path().filename().string());
            }
        }
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "CHU", "Failed to clean directory {}: {}", dir, e.what());
        return false;
    }
}

REGISTER_PLUGIN(CHU, IE_CHU_CLASS_ID);

} // namespace ProjectIE4k
