#include "PRO.h"

#include <fstream>
#include <vector>
#include <iostream>
#include <filesystem>

#include "core/SClassID.h"
#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

// Auto-register the PRO plugin
REGISTER_PLUGIN(PRO, IE_PRO_CLASS_ID);

PRO::PRO(const std::string& resourceName)
    : PluginBase(resourceName, IE_PRO_CLASS_ID)
{
    if (resourceName.empty()) {
        valid_ = false;
        return;
    }

    Log(DEBUG, "PRO", "PRO plugin initialized for resource: {}", resourceName);

    // Basic format detection
    if (!detectFormat()) {
        Log(ERROR, "PRO", "Failed to detect PRO format");
        valid_ = false;
        return;
    }

    // Attempt to parse as PROV1
    std::string err;
    if (!PROV1File::deserialize(originalFileData, proFile, err)) {
        Log(ERROR, "PRO", "Failed to deserialize PRO data: {}", err);
        valid_ = false;
        return;
    }

    // Log basic summary
    Log(DEBUG, "PRO", "Loaded PRO: type={} speed={} totalSize={}",
        static_cast<unsigned>(proFile.base.projectileType),
        static_cast<unsigned>(proFile.base.projectileSpeed),
        originalFileData.size());

    valid_ = true;
}

PRO::~PRO() {}

bool PRO::detectFormat() {
    if (originalFileData.size() < 8) {
        Log(ERROR, "PRO", "Data too small for PRO signature/version");
        return false;
    }
    // Signature "PRO " and Version "V1.0"
    if (std::memcmp(originalFileData.data(), "PRO ", 4) != 0) {
        Log(ERROR, "PRO", "Invalid PRO signature");
        return false;
    }
    if (std::memcmp(originalFileData.data() + 4, "V1.0", 4) != 0) {
        Log(ERROR, "PRO", "Unsupported PRO version");
        return false;
    }
    return true;
}

// Clean directories before operations - operation-specific
bool PRO::cleanExtractDirectory() {
    Log(DEBUG, "PRO", "Cleaning extract directory for resource: {}", resourceName_);
    return cleanDirectory(getExtractDir(false));
}

bool PRO::cleanUpscaleDirectory() {
    Log(DEBUG, "PRO", "Cleaning upscale directory for resource: {}", resourceName_);
    return cleanDirectory(getUpscaledDir(false));
}

bool PRO::cleanAssembleDirectory() {
    Log(DEBUG, "PRO", "Cleaning assemble directory for resource: {}", resourceName_);
    return cleanDirectory(getAssembleDir(false));
}

bool PRO::cleanDirectory(const std::string& dir) {
    if (std::filesystem::exists(dir)) {
        try {
            std::filesystem::remove_all(dir);
            Log(DEBUG, "PRO", "Cleaned directory: {}", dir);
            return true;
        } catch (const std::filesystem::filesystem_error& e) {
            Log(ERROR, "PRO", "Failed to clean directory {}: {}", dir, e.what());
            return false;
        }
    }
    return true; // Directory doesn't exist, nothing to clean
}

// Path management
std::string PRO::getOutputDir(bool ensureDir) const {
    return constructPath("-pro", ensureDir);
}

std::string PRO::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-pro-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string PRO::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-pro-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string PRO::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-pro-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

// Core operations
bool PRO::extract() {
    if (!valid_) {
        Log(ERROR, "PRO", "Invalid PRO resource: {}", resourceName_);
        return false;
    }

    Log(MESSAGE, "PRO", "Starting PRO extraction for resource: {}", resourceName_);

    // Save original bytes to extracted directory (pass-through)
    std::string outputDir = getExtractDir(true);
    std::string outputPath = outputDir + "/" + resourceName_ + originalExtension;

    std::ofstream f(outputPath, std::ios::binary);
    if (!f.is_open()) {
        Log(ERROR, "PRO", "Failed to open output file: {}", outputPath);
        return false;
    }
    f.write(reinterpret_cast<const char*>(originalFileData.data()), originalFileData.size());
    if (f.fail()) {
        Log(ERROR, "PRO", "Failed to write extracted PRO: {}", outputPath);
        return false;
    }

    Log(MESSAGE, "PRO", "Successfully extracted PRO: {}", resourceName_);
    return true;
}

bool PRO::upscale() {
    if (!valid_) {
        Log(ERROR, "PRO", "Invalid PRO resource: {}", resourceName_);
        return false;
    }

    Log(MESSAGE, "PRO", "Upscaling PRO for resource: {}", resourceName_);

    // Apply scale to select radius fields based on game/type.
    const uint32_t factor = PIE4K_CFG.UpScaleFactor > 0 ? PIE4K_CFG.UpScaleFactor : 1u;
    auto sat_mul_u16 = [&](uint16_t v) -> uint16_t {
        uint32_t r = static_cast<uint32_t>(v) * factor;
        return static_cast<uint16_t>(r > 0xFFFFu ? 0xFFFFu : r);
    };

    // PST / PSTEE specific base fields (radiusMin / radiusMax at 0x0060 / 0x0062)
    // Detect PST-like games via GameType containing "pst" (case-insensitive)
    {
        std::string gt = PIE4K_CFG.GameType;
        for (auto &c : gt) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        bool isPST = (gt.find("pst") != std::string::npos);
        if (isPST && factor > 1) {
            uint16_t beforeMin = proFile.base.radiusMin;
            uint16_t beforeMax = proFile.base.radiusMax;
            proFile.base.radiusMin = sat_mul_u16(proFile.base.radiusMin);
            proFile.base.radiusMax = sat_mul_u16(proFile.base.radiusMax);
            Log(DEBUG, "PRO", "Scaled PST radii: min {}->{} max {}->{} (factor {} )",
                (unsigned)beforeMin, (unsigned)proFile.base.radiusMin,
                (unsigned)beforeMax, (unsigned)proFile.base.radiusMax,
                (unsigned)factor);
        }
    }

    // Area-of-effect radii (present only when projectileType == 3)
    if (static_cast<ProjectileType>(proFile.base.projectileType) == ProjectileType::AreaOfEffect && factor > 1) {
        uint16_t beforeTrigger = proFile.area.triggerRadius;
        uint16_t beforeAoE = proFile.area.areaOfEffect;
        proFile.area.triggerRadius = sat_mul_u16(proFile.area.triggerRadius);
        proFile.area.areaOfEffect  = sat_mul_u16(proFile.area.areaOfEffect);
        Log(DEBUG, "PRO", "Scaled AoE radii: trigger {}->{} aoe {}->{} (factor {})",
            (unsigned)beforeTrigger, (unsigned)proFile.area.triggerRadius,
            (unsigned)beforeAoE, (unsigned)proFile.area.areaOfEffect,
            (unsigned)factor);
    }

    // Re-serialize the parsed structure (round-trip) to ensure fidelity after modifications
    std::string err;
    std::vector<uint8_t> out = proFile.serialize(err);
    if (!err.empty()) {
        Log(ERROR, "PRO", "Serialization error: {}", err);
        return false;
    }

    // Save to upscale dir
    std::string outputPath = getUpscaledDir(true) + "/" + resourceName_ + originalExtension;
    std::ofstream f(outputPath, std::ios::binary);
    if (!f.is_open()) {
        Log(ERROR, "PRO", "Failed to create upscaled PRO: {}", outputPath);
        return false;
    }
    f.write(reinterpret_cast<const char*>(out.data()), out.size());
    if (f.fail()) {
        Log(ERROR, "PRO", "Failed to write upscaled PRO: {}", outputPath);
        return false;
    }

    Log(DEBUG, "PRO", "Wrote upscaled PRO: {} bytes -> {}", out.size(), outputPath);
    return true;
}

bool PRO::assemble() {
    if (!valid_) {
        Log(ERROR, "PRO", "Invalid PRO resource: {}", resourceName_);
        return false;
    }

    Log(MESSAGE, "PRO", "Assembling PRO for resource: {}", resourceName_);

    // Copy from upscaled dir to assembled dir (pass-through)
    std::string upscaledPath = getUpscaledDir(false) + "/" + resourceName_ + originalExtension;
    std::string assemblePath = getAssembleDir(true) + "/" + originalFileName;

    if (!std::filesystem::exists(upscaledPath)) {
        Log(ERROR, "PRO", "Upscaled PRO file not found: {}", upscaledPath);
        return false;
    }

    try {
        std::filesystem::copy_file(upscaledPath, assemblePath, std::filesystem::copy_options::overwrite_existing);
        Log(DEBUG, "PRO", "Successfully assembled PRO: {} -> {}", upscaledPath, assemblePath);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "PRO", "Failed to copy upscaled PRO: {}", e.what());
        return false;
    }
}

// Command registration
void PRO::registerCommands(CommandTable& commandTable) {
    commandTable["pro"] = {
        "PRO file operations",
        {
            {"extract", {
                "Extract PRO resource (e.g., pro extract spwi112)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: pro extract <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().extractResource(args[0], IE_PRO_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {
                "Upscale/transform PRO (round-trip serialize) (e.g., pro upscale spwi112)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: pro upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().upscaleResource(args[0], IE_PRO_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {
                "Assemble PRO (copy from upscaled to assembled) (e.g., pro assemble spwi112)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: pro assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().assembleResource(args[0], IE_PRO_CLASS_ID) ? 0 : 1;
                }
            }},
        }
    };
}

} // namespace ProjectIE4k