#include "ARE.h"

#include <fstream>
#include <vector>
#include <iostream>
#include <filesystem>

#include "core/SClassID.h"
#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {
// Auto-register the ARE plugin
REGISTER_PLUGIN(ARE, IE_ARE_CLASS_ID);

// ARE Class Implementation
ARE::ARE(const std::string& resourceName) : PluginBase(resourceName, IE_ARE_CLASS_ID) {
    if (resourceName.empty()) {
        valid_ = false;
        return;
    }

    Log(DEBUG, "ARE", "ARE plugin initialized for resource: {}", resourceName);
    
    std::vector<uint8_t> areData = originalFileData;

    // set original extension
    originalExtension = ".ARE";
    
    Log(DEBUG, "ARE", "Loaded ARE resource: {} bytes", areData.size());
    
    if (!areFile.deserialize(areData)) {
        Log(ERROR, "ARE", "Failed to deserialize ARE data for resource: {}", resourceName);
        return;
    }

    // Log the loaded ARE file structure
    Log(DEBUG, "ARE", "Loaded ARE file structure for {}:", resourceName);
    Log(DEBUG, "ARE", "  Signature: {:.4s}", areFile.header.signature);
    Log(DEBUG, "ARE", "  Version: {:.4s}", areFile.header.version);
    Log(DEBUG, "ARE", "  Area WED: {:.8s}", areFile.header.areaWED);
    Log(DEBUG, "ARE", "  Actors: {} (offset: {})", areFile.header.actorsCount, areFile.header.actorsOffset);
    Log(DEBUG, "ARE", "  Regions: {} (offset: {})", areFile.header.regionsCount, areFile.header.regionsOffset);
    Log(DEBUG, "ARE", "  Spawn Points: {} (offset: {})", areFile.header.spawnPointsCount, areFile.header.spawnPointsOffset);
    Log(DEBUG, "ARE", "  Entrances: {} (offset: {})", areFile.header.entrancesCount, areFile.header.entrancesOffset);
    Log(DEBUG, "ARE", "  Containers: {} (offset: {})", areFile.header.containersCount, areFile.header.containersOffset);
    Log(DEBUG, "ARE", "  Items: {} (offset: {})", areFile.header.itemsCount, areFile.header.itemsOffset);
    Log(DEBUG, "ARE", "  Vertices: {} (offset: {})", areFile.header.verticesCount, areFile.header.verticesOffset);
    Log(DEBUG, "ARE", "  Ambients: {} (offset: {})", areFile.header.ambientsCount, areFile.header.ambientsOffset);
    Log(DEBUG, "ARE", "  Variables: {} (offset: {})", areFile.header.variablesCount, areFile.header.variablesOffset);
    Log(DEBUG, "ARE", "  Doors: {} (offset: {})", areFile.header.doorsCount, areFile.header.doorsOffset);
    Log(DEBUG, "ARE", "  Animations: {} (offset: {})", areFile.header.animationsCount, areFile.header.animationsOffset);
    Log(DEBUG, "ARE", "  Tiled Objects: {} (offset: {})", areFile.header.tiledObjectsCount, areFile.header.tiledObjectsOffset);
    Log(DEBUG, "ARE", "  Automap Notes: {} (offset: {})", areFile.header.automapNoteCount, areFile.header.automapNoteOffset);
    Log(DEBUG, "ARE", "  Projectile Traps: {} (offset: {})", areFile.header.projectileTrapsCount, areFile.header.projectileTrapsOffset);
    Log(DEBUG, "ARE", "  Tiled Object Flags: {} (offset: {})", areFile.header.tiledObjectFlagsCount, areFile.header.tiledObjectFlagsOffset);
    Log(DEBUG, "ARE", "  Song Entries Offset: {}", areFile.header.songEntriesOffset);
    Log(DEBUG, "ARE", "  Rest Interruptions Offset: {}", areFile.header.restInterruptionsOffset);
    Log(DEBUG, "ARE", "  Explored Bitmask: {} bytes (offset: {})", areFile.header.exploredBitmaskSize, areFile.header.exploredBitmaskOffset);

    valid_ = true;
}

ARE::~ARE() {
    // Clean up large data structures in AREFile to prevent memory leaks
    areFile.actors.clear();
    areFile.actors.shrink_to_fit();
    
    areFile.regions.clear();
    areFile.regions.shrink_to_fit();
    
    areFile.spawnPoints.clear();
    areFile.spawnPoints.shrink_to_fit();
    
    areFile.entrances.clear();
    areFile.entrances.shrink_to_fit();
    
    areFile.containers.clear();
    areFile.containers.shrink_to_fit();
    
    areFile.items.clear();
    areFile.items.shrink_to_fit();
    
    areFile.vertices.clear();
    areFile.vertices.shrink_to_fit();
    
    areFile.ambients.clear();
    areFile.ambients.shrink_to_fit();
    
    areFile.variables.clear();
    areFile.variables.shrink_to_fit();
    
    areFile.exploredBitmask.clear();
    areFile.exploredBitmask.shrink_to_fit();
    
    areFile.doors.clear();
    areFile.doors.shrink_to_fit();
    
    areFile.animations.clear();
    areFile.animations.shrink_to_fit();
    
    areFile.automapNotes.clear();
    areFile.automapNotes.shrink_to_fit();
    
    areFile.tiledObjects.clear();
    areFile.tiledObjects.shrink_to_fit();
    
    areFile.projectileTraps.clear();
    areFile.projectileTraps.shrink_to_fit();
    
    areFile.songEntries.clear();
    areFile.songEntries.shrink_to_fit();
    
    areFile.restInterrupts.clear();
    areFile.restInterrupts.shrink_to_fit();
    
    areFile.tiledObjectFlags.clear();
    areFile.tiledObjectFlags.shrink_to_fit();
}

bool ARE::extract() {
    if (!valid_) {
        Log(ERROR, "ARE", "ARE data is not valid, cannot extract.");
        return false;
    }

    std::string outputDir = getExtractDir(true);
    if (outputDir.empty()) {
        Log(ERROR, "ARE", "Failed to create output directory.");
        return false;
    }

    std::string outputPath = outputDir + "/" + resourceName_ + originalExtension;
    
    if (!saveToFile(outputPath)) {
        Log(ERROR, "ARE", "Failed to extract ARE file to: {}", outputPath);
        return false;
    }

    Log(MESSAGE, "ARE", "Successfully extracted ARE file to: {}", outputPath);
    return true;
}

bool ARE::assemble() {
    if (!valid_) {
        Log(ERROR, "ARE", "ARE data is not valid, cannot assemble.");
        return false;
    }

    Log(MESSAGE, "ARE", "Starting ARE assembly for resource: {}", resourceName_);

    // Get the upscaled file path
    std::string upscaledPath = getUpscaledDir(false) + "/" + resourceName_ + originalExtension;
    if (!std::filesystem::exists(upscaledPath)) {
        Log(ERROR, "ARE", "Upscaled ARE file not found: {}", upscaledPath);
        return false;
    }

    // Get the assemble directory
    std::string assembleDir = getAssembleDir(true);
    if (assembleDir.empty()) {
        Log(ERROR, "ARE", "Failed to create assemble directory.");
        return false;
    }

    std::string assemblePath = assembleDir + "/" + originalFileName;
    
    // Copy the upscaled file to the assembled directory
    try {
        std::filesystem::copy_file(upscaledPath, assemblePath, std::filesystem::copy_options::overwrite_existing);
        Log(MESSAGE, "ARE", "Successfully assembled ARE file to: {} (copied from upscaled)", assemblePath);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "ARE", "Failed to copy upscaled ARE file: {}", e.what());
        return false;
    }
}

bool ARE::upscale() {
    if (!valid_) {
        Log(ERROR, "ARE", "ARE data is not valid, cannot upscale.");
        return false;
    }

    Log(MESSAGE, "ARE", "Starting ARE upscale for resource: {}", resourceName_);

    // ARE upscaling involves scaling coordinates and dimensions
    // Get the upscale factor from configuration
    const float scaleFactor = static_cast<float>(PIE4K_CFG.UpScaleFactor);
    
    Log(MESSAGE, "ARE", "Using upscale factor: {}x", scaleFactor);

    // Scale actor positions
    for (auto& actor : areFile.actors) {
        actor.currentX = static_cast<uint16_t>(actor.currentX * scaleFactor);
        actor.currentY = static_cast<uint16_t>(actor.currentY * scaleFactor);
        actor.destX = static_cast<uint16_t>(actor.destX * scaleFactor);
        actor.destY = static_cast<uint16_t>(actor.destY * scaleFactor);
    }

    // Scale region bounding boxes and vertices
    for (auto& region : areFile.regions) {
        for (int i = 0; i < 4; ++i) {
            region.boundingBox[i] = static_cast<int16_t>(region.boundingBox[i] * scaleFactor);
        }
    }

    // Scale spawn point positions
    for (auto& spawnPoint : areFile.spawnPoints) {
        spawnPoint.x = static_cast<uint16_t>(spawnPoint.x * scaleFactor);
        spawnPoint.y = static_cast<uint16_t>(spawnPoint.y * scaleFactor);
    }

    // Scale entrance positions
    for (auto& entrance : areFile.entrances) {
        entrance.x = static_cast<uint16_t>(entrance.x * scaleFactor);
        entrance.y = static_cast<uint16_t>(entrance.y * scaleFactor);
    }

    // Scale container positions and bounding boxes
    for (auto& container : areFile.containers) {
        container.x = static_cast<uint16_t>(container.x * scaleFactor);
        container.y = static_cast<uint16_t>(container.y * scaleFactor);
        for (int i = 0; i < 4; ++i) {
            container.boundingBox[i] = static_cast<int16_t>(container.boundingBox[i] * scaleFactor);
        }
    }

    // Scale ambient positions
    for (auto& ambient : areFile.ambients) {
        ambient.x = static_cast<uint16_t>(ambient.x * scaleFactor);
        ambient.y = static_cast<uint16_t>(ambient.y * scaleFactor);
        ambient.radius = static_cast<uint16_t>(ambient.radius * scaleFactor);
        ambient.height = static_cast<uint16_t>(ambient.height * scaleFactor);
    }

    // Scale door positions and bounding boxes
    for (auto& door : areFile.doors) {
        for (int i = 0; i < 4; ++i) {
            door.openBoundingBox[i] = static_cast<int16_t>(door.openBoundingBox[i] * scaleFactor);
            door.closedBoundingBox[i] = static_cast<int16_t>(door.closedBoundingBox[i] * scaleFactor);
        }
        for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
                door.usePoints[i][j] = static_cast<int16_t>(door.usePoints[i][j] * scaleFactor);
            }
        }
        // Scale trap launch target coordinates
        door.trapLaunchTargetX = static_cast<int16_t>(door.trapLaunchTargetX * scaleFactor);
        door.trapLaunchTargetY = static_cast<int16_t>(door.trapLaunchTargetY * scaleFactor);
    }

    // Scale animation positions
    for (auto& animation : areFile.animations) {
        animation.x = static_cast<uint16_t>(animation.x * scaleFactor);
        animation.y = static_cast<uint16_t>(animation.y * scaleFactor);
        animation.height = static_cast<uint16_t>(animation.height * scaleFactor);
        animation.animationWidth = static_cast<uint16_t>(animation.animationWidth * scaleFactor);
        animation.animationHeight = static_cast<uint16_t>(animation.animationHeight * scaleFactor);
    }

    // Scale automap note positions
    for (auto& note : areFile.automapNotes) {
        note.x = static_cast<uint16_t>(note.x * scaleFactor);
        note.y = static_cast<uint16_t>(note.y * scaleFactor);
    }

    // Scale projectile trap positions
    for (auto& trap : areFile.projectileTraps) {
        trap.x = static_cast<uint16_t>(trap.x * scaleFactor);
        trap.y = static_cast<uint16_t>(trap.y * scaleFactor);
        trap.z = static_cast<uint16_t>(trap.z * scaleFactor);
    }

    // Scale vertices
    for (auto& vertex : areFile.vertices) {
        vertex.x = static_cast<int16_t>(vertex.x * scaleFactor);
        vertex.y = static_cast<int16_t>(vertex.y * scaleFactor);
    }

    // Save upscaled file
    std::string upscaledDir = getUpscaledDir(true);
    if (upscaledDir.empty()) {
        Log(ERROR, "ARE", "Failed to create upscaled directory.");
        return false;
    }

    std::string upscaledPath = upscaledDir + "/" + resourceName_ + originalExtension;
    
    if (!saveToFile(upscaledPath)) {
        Log(ERROR, "ARE", "Failed to save upscaled ARE file to: {}", upscaledPath);
        return false;
    }

    Log(MESSAGE, "ARE", "Successfully upscaled ARE file to: {}", upscaledPath);
    return true;
}

bool ARE::saveToFile(const std::string& filePath) const {
    if (!valid_) {
        Log(ERROR, "ARE", "ARE data is not valid, cannot save.");
        return false;
    }

    // Add debugging information about the ARE file structure
    Log(DEBUG, "ARE", "Serializing ARE file with structure:");
    Log(DEBUG, "ARE", "  Actors: {}", areFile.actors.size());
    Log(DEBUG, "ARE", "  Regions: {}", areFile.regions.size());
    Log(DEBUG, "ARE", "  Spawn Points: {}", areFile.spawnPoints.size());
    Log(DEBUG, "ARE", "  Entrances: {}", areFile.entrances.size());
    Log(DEBUG, "ARE", "  Containers: {}", areFile.containers.size());
    Log(DEBUG, "ARE", "  Items: {}", areFile.items.size());
    Log(DEBUG, "ARE", "  Vertices: {}", areFile.vertices.size());
    Log(DEBUG, "ARE", "  Ambients: {}", areFile.ambients.size());
    Log(DEBUG, "ARE", "  Variables: {}", areFile.variables.size());
    Log(DEBUG, "ARE", "  Doors: {}", areFile.doors.size());
    Log(DEBUG, "ARE", "  Animations: {}", areFile.animations.size());
    Log(DEBUG, "ARE", "  Automap Notes: {}", areFile.automapNotes.size());
    Log(DEBUG, "ARE", "  Tiled Objects: {}", areFile.tiledObjects.size());
    Log(DEBUG, "ARE", "  Projectile Traps: {}", areFile.projectileTraps.size());
    Log(DEBUG, "ARE", "  Song Entries: {}", areFile.songEntries.size());
    Log(DEBUG, "ARE", "  Rest Interrupts: {}", areFile.restInterrupts.size());
    Log(DEBUG, "ARE", "  Tiled Object Flags: {}", areFile.tiledObjectFlags.size());
    Log(DEBUG, "ARE", "  Explored Bitmask Size: {}", areFile.exploredBitmask.size());

    std::vector<uint8_t> data = areFile.serialize();
    
    Log(DEBUG, "ARE", "Serialized data size: {} bytes", data.size());
    
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "ARE", "Could not create file: {}", filePath);
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    
    if (file.fail()) {
        Log(ERROR, "ARE", "Failed to write file: {}", filePath);
        return false;
    }
    
    Log(MESSAGE, "ARE", "Successfully saved ARE file to: {} ({} bytes)", filePath, data.size());
    return true;
}

void ARE::registerCommands(CommandTable& commandTable) {
    commandTable["are"] = {
        "ARE file operations",
        {
            {"extract", {
                "Extract ARE resource to file (e.g., are extract ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: are extract <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().extractResource(args[0], IE_ARE_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {
                "Upscale ARE coordinates (e.g., are upscale ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: are upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().upscaleResource(args[0], IE_ARE_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {
                "Assemble ARE file (e.g., are assemble ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: are assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().assembleResource(args[0], IE_ARE_CLASS_ID) ? 0 : 1;
                }
            }}
        }
    };
}

// Path management overrides
std::string ARE::getOutputDir(bool ensureDir) const {
    return constructPath("-are", ensureDir);
}

std::string ARE::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-are-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string ARE::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-are-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string ARE::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-are-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

// Clean directories before operations - operation-specific
bool ARE::cleanExtractDirectory() {
    Log(MESSAGE, "ARE", "Cleaning extract directory for resource: {}", resourceName_);
    return cleanDirectory(getOutputDir(false));
}

bool ARE::cleanUpscaleDirectory() {
    Log(MESSAGE, "ARE", "Cleaning upscale directory for resource: {}", resourceName_);
    return cleanDirectory(getUpscaledDir(false));
}

bool ARE::cleanAssembleDirectory() {
    Log(MESSAGE, "ARE", "Cleaning assemble directory for resource: {}", resourceName_);
    return cleanDirectory(getAssembleDir(false));
}

bool ARE::cleanDirectory(const std::string& dir) {
    if (std::filesystem::exists(dir)) {
        try {
            std::filesystem::remove_all(dir);
            Log(MESSAGE, "ARE", "Cleaned directory: {}", dir);
            return true;
        } catch (const std::filesystem::filesystem_error& e) {
            Log(ERROR, "ARE", "Failed to clean directory {}: {}", dir, e.what());
            return false;
        }
    }
    return true; // Directory doesn't exist, nothing to clean
}

} // namespace ProjectIE4k
