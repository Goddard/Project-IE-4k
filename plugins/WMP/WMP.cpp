#include "WMP.h"

#include <fstream>
#include <filesystem>
#include <iostream>

#include "core/SClassID.h"
#include "core/Logging/Logging.h"
#include "plugins/CommandRegistry.h"
#include "core/CFG.h"

namespace fs = std::filesystem;

namespace ProjectIE4k {

// Auto-register the WMP plugin
REGISTER_PLUGIN(WMP, IE_WMP_CLASS_ID);

WMP::WMP(const std::string &resourceName)
    : PluginBase(resourceName, IE_WMP_CLASS_ID) {
    if (resourceName.empty()) {
        valid_ = false;
        return;
    }

    Log(DEBUG, "WMP", "WMP plugin initialized for resource: {}", resourceName);

    if (!detectFormat()) {
        Log(ERROR, "WMP", "Failed to detect WMP format");
        valid_ = false;
        return;
    }

    if (!loadFromData()) {
        Log(ERROR, "WMP", "Failed to parse WMP data");
        valid_ = false;
        return;
    }

    valid_ = true;
}

WMP::~WMP() {}

bool WMP::detectFormat() {
    if (originalFileData.size() < sizeof(WMPHeaderV1)) {
        Log(ERROR, "WMP", "Data too small for WMP header");
        return false;
    }
    if (std::memcmp(originalFileData.data(), "WMAP", 4) != 0) {
        Log(ERROR, "WMP", "Invalid WMP signature");
        return false;
    }
    if (std::memcmp(originalFileData.data() + 4, "V1.0", 4) != 0) {
        Log(ERROR, "WMP", "Unsupported WMP version");
        return false;
    }
    return true;
}

bool WMP::loadFromData() {
    if (!wmp_.deserialize(originalFileData)) {
        Log(ERROR, "WMP", "Deserialize failed for WMP V1");
        return false;
    }
    Log(DEBUG, "WMP", "Loaded WMP: worldmaps={}, areas(flat)={}, links(flat)={}",
        wmp_.worldmaps.size(), wmp_.areas.size(), wmp_.areaLinks.size());
    return true;
}

// Cleaning
bool WMP::cleanExtractDirectory() {
    return cleanDirectory(getExtractDir(false));
}

bool WMP::cleanUpscaleDirectory() {
    return cleanDirectory(getUpscaledDir(false));
}

bool WMP::cleanAssembleDirectory() {
    return cleanDirectory(getAssembleDir(false));
}

bool WMP::cleanDirectory(const std::string &dir) {
    if (fs::exists(dir)) {
        try {
            fs::remove_all(dir);
            Log(DEBUG, "WMP", "Cleaned directory: {}", dir);
            return true;
        } catch (const fs::filesystem_error &e) {
            Log(ERROR, "WMP", "Failed to clean directory {}: {}", dir, e.what());
            return false;
        }
    }
    return true;
}

// Paths
std::string WMP::getOutputDir(bool ensureDir) const {
    return constructPath("-wmp", ensureDir);
}

std::string WMP::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-wmp-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string WMP::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-wmp-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string WMP::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-wmp-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

// Core operations
bool WMP::extract() {
    if (!valid_) {
        Log(ERROR, "WMP", "Invalid WMP resource: {}", resourceName_);
        return false;
    }

    Log(MESSAGE, "WMP", "Starting WMP extraction for resource: {}", resourceName_);

    std::string outDir = getExtractDir(true);
    std::string outPath = outDir + "/" + resourceName_ + originalExtension;

    std::ofstream f(outPath, std::ios::binary);
    if (!f.is_open()) {
        Log(ERROR, "WMP", "Failed to open output file: {}", outPath);
        return false;
    }
    f.write(reinterpret_cast<const char *>(originalFileData.data()), originalFileData.size());
    if (f.fail()) {
        Log(ERROR, "WMP", "Failed to write extracted WMP: {}", outPath);
        return false;
    }

    Log(MESSAGE, "WMP", "Successfully extracted WMP: {}", resourceName_);
    return true;
}

bool WMP::upscale() {
    if (!valid_) {
        Log(ERROR, "WMP", "Invalid WMP resource: {}", resourceName_);
        return false;
    }

    const uint32_t factor = PIE4K_CFG.UpScaleFactor > 0 ? static_cast<uint32_t>(PIE4K_CFG.UpScaleFactor) : 1u;

    auto sat_mul_u32 = [&](uint32_t v) -> uint32_t {
        uint64_t r = static_cast<uint64_t>(v) * static_cast<uint64_t>(factor);
        return r > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(r);
    };

    // Apply scaling
    for (auto &wm : wmp_.worldmaps) {
        wm.width = sat_mul_u32(wm.width);
        wm.height = sat_mul_u32(wm.height);
        wm.startCenterX = sat_mul_u32(wm.startCenterX);
        wm.startCenterY = sat_mul_u32(wm.startCenterY);
    }
    for (auto &a : wmp_.areas) {
        a.x = sat_mul_u32(a.x);
        a.y = sat_mul_u32(a.y);
    }

    // Serialize updated structure
    std::vector<uint8_t> outBytes = wmp_.serialize();
    if (outBytes.empty()) {
        Log(ERROR, "WMP", "Serialization failed for resource: {}", resourceName_);
        return false;
    }

    std::string outPath = getUpscaledDir(true) + "/" + resourceName_ + originalExtension;
    std::ofstream f(outPath, std::ios::binary);
    if (!f.is_open()) {
        Log(ERROR, "WMP", "Failed to create upscaled WMP: {}", outPath);
        return false;
    }
    f.write(reinterpret_cast<const char *>(outBytes.data()), outBytes.size());
    if (f.fail()) {
        Log(ERROR, "WMP", "Failed to write upscaled WMP: {}", outPath);
        return false;
    }

    Log(DEBUG, "WMP", "Wrote upscaled WMP: {} bytes -> {} (factor {})", outBytes.size(), outPath, factor);
    return true;
}

bool WMP::assemble() {
    if (!valid_) {
        Log(ERROR, "WMP", "Invalid WMP resource: {}", resourceName_);
        return false;
    }

    Log(MESSAGE, "WMP", "Assembling WMP for resource: {}", resourceName_);

    std::string upscaledPath = getUpscaledDir(false) + "/" + resourceName_ + originalExtension;
    std::string assemblePath = getAssembleDir(true) + "/" + originalFileName;

    if (!fs::exists(upscaledPath)) {
        Log(ERROR, "WMP", "Upscaled WMP file not found: {}", upscaledPath);
        return false;
    }

    try {
        fs::copy_file(upscaledPath, assemblePath, fs::copy_options::overwrite_existing);
        Log(DEBUG, "WMP", "Successfully assembled WMP: {} -> {}", upscaledPath, assemblePath);
        return true;
    } catch (const fs::filesystem_error &e) {
        Log(ERROR, "WMP", "Failed to copy upscaled WMP: {}", e.what());
        return false;
    }
}

// Batch ops
bool WMP::extractAll() { return PluginManager::getInstance().extractAllResourcesOfType(IE_WMP_CLASS_ID); }
bool WMP::upscaleAll() { return PluginManager::getInstance().upscaleAllResourcesOfType(IE_WMP_CLASS_ID); }
bool WMP::assembleAll() { return PluginManager::getInstance().assembleAllResourcesOfType(IE_WMP_CLASS_ID); }

// Command registration
void WMP::registerCommands(CommandTable &commandTable) {
    commandTable["wmp"] = {"WMP file operations",
                            {{"extract",
                              {"Extract WMP resource (e.g., wmp extract worldmap)",
                               [](const std::vector<std::string> &args) -> int {
                                   if (args.empty()) {
                                       std::cerr << "Usage: wmp extract <resource_name>" << std::endl;
                                       return 1;
                                   }
                                   return ProjectIE4k::PluginManager::getInstance().extractResource(args[0], IE_WMP_CLASS_ID) ? 0 : 1;
                               }}},
                             {"upscale",
                              {"Pass-through upscale (e.g., wmp upscale worldmap)",
                               [](const std::vector<std::string> &args) -> int {
                                   if (args.empty()) {
                                       std::cerr << "Usage: wmp upscale <resource_name>" << std::endl;
                                       return 1;
                                   }
                                   return ProjectIE4k::PluginManager::getInstance().upscaleResource(args[0], IE_WMP_CLASS_ID) ? 0 : 1;
                               }}},
                             {"assemble",
                              {"Assemble WMP (copy upscaled to assembled) (e.g., wmp assemble worldmap)",
                               [](const std::vector<std::string> &args) -> int {
                                   if (args.empty()) {
                                       std::cerr << "Usage: wmp assemble <resource_name>" << std::endl;
                                       return 1;
                                   }
                                   return ProjectIE4k::PluginManager::getInstance().assembleResource(args[0], IE_WMP_CLASS_ID) ? 0 : 1;
                               }}}}};
}

} // namespace ProjectIE4k
