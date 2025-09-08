#include "GAM.h"

#include <fstream>
#include <filesystem>
#include <cstring>

#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include "plugins/CommandRegistry.h"
#include "plugins/PluginManager.h"

namespace ProjectIE4k {

GAM::GAM(const std::string& resourceName)
    : PluginBase(resourceName, IE_GAM_CLASS_ID) {
    if (resourceName.empty()) {
        return;
    }

    if (!loadFromData()) {
        Log(ERROR, "GAM", "Failed to load GAM data");
        return;
    }

    valid_ = true;
}

GAM::~GAM() {
    // nothing special; large buffers are in PluginBase
}

bool GAM::loadFromData() {
    if (originalFileData.empty()) {
        Log(ERROR, "GAM", "No GAM data loaded");
        return false;
    }

    if (!readHeader()) {
        return false;
    }
    if (detectedVersion_ == std::string("V1.1", 4)) {
        return parseV11();
    }
    if (detectedVersion_ == std::string("V2.0", 4) || detectedVersion_ == std::string("V2.1", 4)) {
        return parseV20();
    }
    if (detectedVersion_ == std::string("V2.2", 4)) {
        return parseV22();
    }
    return true;
}

bool GAM::readHeader() {
    if (originalFileData.size() < 8) {
        Log(ERROR, "GAM", "File too small for header");
        return false;
    }

    const char* sig = reinterpret_cast<const char*>(originalFileData.data());
    const char* ver = reinterpret_cast<const char*>(originalFileData.data() + 4);

    if (std::memcmp(sig, "GAME", 4) != 0) {
        Log(ERROR, "GAM", "Invalid signature: {:.4s}", sig);
        return false;
    }

    detectedVersion_.assign(ver, ver + 4);
    Log(DEBUG, "GAM", "Detected version: {:.4s}", ver);

    // Parsed per version in loadFromData
    return true;
}

bool GAM::parseV11() {
    v11_ = std::make_unique<GAMV11File>();
    GAMEV11Variant variant = GAMEV11VariantFromString(PIE4K_CFG.GameType);
    if (!v11_->deserialize(originalFileData, variant)) {
        Log(ERROR, "GAM", "Failed to parse GAME V1.1");
        v11_.reset();
        return false;
    }
    return true;
}

std::vector<uint8_t> GAM::serializeV11() const {
    if (!v11_) return originalFileData;
    return v11_->serialize();
}

bool GAM::parseV20() {
    v20_ = std::make_unique<GAMV20File>();
    if (!v20_->deserialize(originalFileData)) {
        Log(ERROR, "GAM", "Failed to parse GAME V2.0");
        v20_.reset();
        return false;
    }
    return true;
}

std::vector<uint8_t> GAM::serializeV20() const {
    if (!v20_) return originalFileData;
    return v20_->serialize();
}

bool GAM::parseV22() {
    v22_ = std::make_unique<GAMV22File>();
    if (!v22_->deserialize(originalFileData)) {
        Log(ERROR, "GAM", "Failed to parse GAME V2.2");
        v22_.reset();
        return false;
    }
    return true;
}

std::vector<uint8_t> GAM::serializeV22() const {
    if (!v22_) return originalFileData;
    return v22_->serialize();
}

bool GAM::writeFile(const std::string& path, const std::vector<uint8_t>& data) const {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        Log(ERROR, "GAM", "Failed to open output file: {}", path);
        return false;
    }
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (!f) {
        Log(ERROR, "GAM", "Failed to write file: {}", path);
        return false;
    }
    return true;
}

bool GAM::extract() {
    Log(MESSAGE, "GAM", "Starting GAM extraction for resource: {}", resourceName_);

    std::string outDir = getExtractDir(true);
    std::string outPath = outDir + "/" + originalFileName;

    if (!writeFile(outPath, originalFileData)) {
        return false;
    }

    Log(MESSAGE, "GAM", "Extracted to {} ({} bytes)", outPath, originalFileData.size());
    return true;
}

bool GAM::upscale() {
    Log(DEBUG, "GAM", "Upscaling NPC coordinates and stored locations where applicable");

    std::string outDir = getUpscaledDir(true);
    std::string outPath = outDir + "/" + originalFileName;

    int ups = PIE4K_CFG.UpScaleFactor;
    if (detectedVersion_ == std::string("V1.1", 4) && v11_) {
        // Scale NPCs in parsed vectors and re-serialize
        for (auto &npc : v11_->partyNPCs) { npc.x *= ups; npc.y *= ups; }
        for (auto &npc : v11_->nonPartyNPCs) { npc.x *= ups; npc.y *= ups; }
        return writeFile(outPath, serializeV11());
    } else if ((detectedVersion_ == std::string("V2.0", 4) || detectedVersion_ == std::string("V2.1", 4)) && v20_) {
        // Scale parsed V2.0 vectors and serialize
        Log(DEBUG, "GAM", "Before upscaling - Non-party NPC count: {}, upscale factor: {}", v20_->nonPartyNPCs.size(), ups);
        for (size_t i = 0; i < v20_->nonPartyNPCs.size(); i++) {
            Log(DEBUG, "GAM", "Before upscaling NPC {}: {} x {} y {}", i, v20_->nonPartyNPCs[i].characterName8, v20_->nonPartyNPCs[i].x, v20_->nonPartyNPCs[i].y);
        }
        
        for (auto &npc : v20_->partyNPCs) { npc.x *= ups; npc.y *= ups; }
        for (auto &npc : v20_->nonPartyNPCs) { npc.x *= ups; npc.y *= ups; }
        for (auto &loc : v20_->storedLocations) { loc.x *= ups; loc.y *= ups; }
        for (auto &loc : v20_->pocketPlaneLocations) { loc.x *= ups; loc.y *= ups; }
        
        Log(DEBUG, "GAM", "After upscaling - Non-party NPC count: {}", v20_->nonPartyNPCs.size());
        for (size_t i = 0; i < v20_->nonPartyNPCs.size(); i++) {
            Log(DEBUG, "GAM", "After upscaling NPC {}: {} x {} y {}", i, v20_->nonPartyNPCs[i].name, v20_->nonPartyNPCs[i].x, v20_->nonPartyNPCs[i].y);
        }
        
        return writeFile(outPath, v20_->serialize());
    } else if (detectedVersion_ == std::string("V2.2", 4) && v22_) {
        // Scale parsed V2.2 vectors and serialize
        for (auto &npc : v22_->partyNPCs) { npc.x *= ups; npc.y *= ups; }
        for (auto &npc : v22_->nonPartyNPCs) { npc.x *= ups; npc.y *= ups; }
        return writeFile(outPath, v22_->serialize());
    }
    Log(ERROR, "GAM", "Unsupported or unparsed GAM version: {}", detectedVersion_);
    return false;
}

bool GAM::assemble() {
    Log(MESSAGE, "GAM", "Starting GAM assembly for resource: {}", resourceName_);

    std::string upscaledDir = getUpscaledDir(false);
    std::string upscaledPath = upscaledDir + "/" + originalFileName;
    if (!std::filesystem::exists(upscaledPath)) {
        Log(ERROR, "GAM", "Upscaled file not found: {}", upscaledPath);
        return false;
    }

    std::string assembleDir = getAssembleDir(true);
    std::string assemblePath = assembleDir + "/" + originalFileName;

    try {
        std::filesystem::copy_file(upscaledPath, assemblePath, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "GAM", "Filesystem error during assembly: {}", e.what());
        return false;
    }

    Log(MESSAGE, "GAM", "Assembled GAM to {}", assemblePath);
    return true;
}

void GAM::registerCommands(CommandTable& commandTable) {
    commandTable["gam"] = {
        "GAM file operations",
        {
            {"extract", {"Extract GAM resource to file (e.g., gam extract baldur)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: gam extract <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().extractResource(args[0], IE_GAM_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {"No-op upscale for GAM (e.g., gam upscale baldur)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: gam upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().upscaleResource(args[0], IE_GAM_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {"Assemble GAM file (e.g., gam assemble baldur)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: gam assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().assembleResource(args[0], IE_GAM_CLASS_ID) ? 0 : 1;
                }
            }}
        }
    };
}

// Batch operations
bool GAM::extractAll() {
    return PluginManager::getInstance().extractAllResourcesOfType(IE_GAM_CLASS_ID);
}

bool GAM::upscaleAll() {
    return PluginManager::getInstance().upscaleAllResourcesOfType(IE_GAM_CLASS_ID);
}

bool GAM::assembleAll() {
    return PluginManager::getInstance().assembleAllResourcesOfType(IE_GAM_CLASS_ID);
}

// Directory management
bool GAM::cleanExtractDirectory() { return cleanDirectory(getOutputDir(false)); }
bool GAM::cleanUpscaleDirectory() { return cleanDirectory(getUpscaledDir(false)); }
bool GAM::cleanAssembleDirectory() { return cleanDirectory(getAssembleDir(false)); }

std::string GAM::getOutputDir(bool ensureDir) const {
    return constructPath("-gam", ensureDir);
}

std::string GAM::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-gam-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string GAM::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-gam-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string GAM::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-gam-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

bool GAM::cleanDirectory(const std::string& dir) {
    if (!std::filesystem::exists(dir)) {
        return true;
    }
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::filesystem::remove(entry.path());
            }
        }
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "GAM", "Failed to clean directory {}: {}", dir, e.what());
        return false;
    }
}

REGISTER_PLUGIN(GAM, IE_GAM_CLASS_ID);

} // namespace ProjectIE4k


