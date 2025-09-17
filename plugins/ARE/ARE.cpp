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
    // Game-specific field access through union
    if (PIE4K_CFG.GameType == "pst") {
        Log(DEBUG, "ARE", "  Automap Notes: {} (offset: {})", areFile.header.gameSpecific.pst.automapNoteCount, areFile.header.gameSpecific.pst.automapNoteOffset);
        Log(DEBUG, "ARE", "  Projectile Traps: {} (offset: {})", areFile.header.gameSpecific.pst.projectileTrapsCount, areFile.header.gameSpecific.pst.projectileTrapsOffset);
    } else {
        Log(DEBUG, "ARE", "  Automap Notes: {} (offset: {})", areFile.header.gameSpecific.standard.automapNoteCount, areFile.header.gameSpecific.standard.automapNoteOffset);
        Log(DEBUG, "ARE", "  Projectile Traps: {} (offset: {})", areFile.header.gameSpecific.standard.projectileTrapsCount, areFile.header.gameSpecific.standard.projectileTrapsOffset);
    }
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

    // Keep explored bitmask as-is since area dimensions don't change during upscaling
    // areFile.exploredBitmask.clear();
    // areFile.exploredBitmask.shrink_to_fit();

    // Ensure explored bitmask header values are correct
    if (areFile.exploredBitmask.empty()) {
        areFile.header.exploredBitmaskSize = 0;
        areFile.header.exploredBitmaskOffset = 0;
        Log(DEBUG, "ARE", "Explored bitmask is empty, setting size=0, offset=0");
    } else {
        Log(DEBUG, "ARE", "Explored bitmask has {} bytes", areFile.exploredBitmask.size());
    }
    
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

    // FIRST: Collect original door impeded ranges BEFORE upscaling doors
    std::vector<std::pair<uint32_t, uint16_t>> doorImpededRanges; // {start_index, count}
    for (const auto& door : areFile.doors) {
        if (door.openImpededCellBlockCount > 0) {
            uint32_t endIdx = door.openImpededCellBlockIndex + door.openImpededCellBlockCount;
            if (endIdx <= areFile.vertices.size()) {
                doorImpededRanges.push_back({door.openImpededCellBlockIndex, door.openImpededCellBlockCount});
                Log(DEBUG, "ARE", "Original door impeded range: index={} count={}", door.openImpededCellBlockIndex, door.openImpededCellBlockCount);
            } else {
                Log(DEBUG, "ARE", "Skipping invalid door impeded range: index={} count={} (end={} > vertices={})",
                    door.openImpededCellBlockIndex, door.openImpededCellBlockCount, endIdx, areFile.vertices.size());
            }
        }
        if (door.closedImpededCellBlockCount > 0) {
            uint32_t endIdx = door.closedImpededCellBlockIndex + door.closedImpededCellBlockCount;
            if (endIdx <= areFile.vertices.size()) {
                doorImpededRanges.push_back({door.closedImpededCellBlockIndex, door.closedImpededCellBlockCount});
                Log(DEBUG, "ARE", "Original door impeded range: index={} count={}", door.closedImpededCellBlockIndex, door.closedImpededCellBlockCount);
            } else {
                Log(DEBUG, "ARE", "Skipping invalid door impeded range: index={} count={} (end={} > vertices={})",
                    door.closedImpededCellBlockIndex, door.closedImpededCellBlockCount, endIdx, areFile.vertices.size());
            }
        }
    }

    // Scale door positions and bounding boxes, and expand impeded cells
    for (size_t doorIdx = 0; doorIdx < areFile.doors.size(); doorIdx++) {
        auto& door = areFile.doors[doorIdx];
        
        Log(DEBUG, "ARE", "Door {}: Before scaling - Open bbox: [{},{},{},{}], Closed bbox: [{},{},{},{}]",
            doorIdx, door.openBoundingBox[0], door.openBoundingBox[1], door.openBoundingBox[2], door.openBoundingBox[3],
            door.closedBoundingBox[0], door.closedBoundingBox[1], door.closedBoundingBox[2], door.closedBoundingBox[3]);
        
        Log(DEBUG, "ARE", "Door {}: Before scaling - Open impeded: index={} count={}, Closed impeded: index={} count={}",
            doorIdx, door.openImpededCellBlockIndex, door.openImpededCellBlockCount,
            door.closedImpededCellBlockIndex, door.closedImpededCellBlockCount);
        
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
        
        // Expand impeded cell counts proportionally to upscale factor squared
        uint16_t originalOpenCount = door.openImpededCellBlockCount;
        uint16_t originalClosedCount = door.closedImpededCellBlockCount;
        uint32_t doorExpansionFactor = static_cast<uint32_t>(scaleFactor * scaleFactor);
        
        door.openImpededCellBlockCount = originalOpenCount * doorExpansionFactor;
        door.closedImpededCellBlockCount = originalClosedCount * doorExpansionFactor;
        
        Log(DEBUG, "ARE", "Door {}: After scaling - Open bbox: [{},{},{},{}], Closed bbox: [{},{},{},{}]",
            doorIdx, door.openBoundingBox[0], door.openBoundingBox[1], door.openBoundingBox[2], door.openBoundingBox[3],
            door.closedBoundingBox[0], door.closedBoundingBox[1], door.closedBoundingBox[2], door.closedBoundingBox[3]);
        
        Log(DEBUG, "ARE", "Door {}: After scaling - Open impeded: count {}->{}  Closed impeded: count {}->{}",
            doorIdx, originalOpenCount, door.openImpededCellBlockCount,
            originalClosedCount, door.closedImpededCellBlockCount);
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
        if (PIE4K_CFG.GameType == "pst") {
            note.pst.x = static_cast<uint32_t>(note.pst.x * scaleFactor);
            note.pst.y = static_cast<uint32_t>(note.pst.y * scaleFactor);
        } else {
            note.standard.x = static_cast<uint16_t>(note.standard.x * scaleFactor);
            note.standard.y = static_cast<uint16_t>(note.standard.y * scaleFactor);
        }
    }

    // Scale projectile trap positions
    for (auto& trap : areFile.projectileTraps) {
        trap.x = static_cast<uint16_t>(trap.x * scaleFactor);
        trap.y = static_cast<uint16_t>(trap.y * scaleFactor);
        trap.z = static_cast<uint16_t>(trap.z * scaleFactor);
    }

    // Expand and scale vertices (including impeded cells)
    uint32_t expansionFactor = static_cast<uint32_t>(scaleFactor * scaleFactor);
    std::vector<AREVertex> newVertices;
    newVertices.reserve(areFile.vertices.size() * expansionFactor);
    
    // Track which vertices are impeded cells for doors and calculate new indices
    std::map<uint32_t, uint32_t> oldToNewIndexMap; // Map old vertex index to new start index
    
    // Process each original vertex and build index mapping
    size_t impededCount = 0;
    size_t regularCount = 0;
    for (size_t vertexIdx = 0; vertexIdx < areFile.vertices.size(); vertexIdx++) {
        const auto& origVertex = areFile.vertices[vertexIdx];

        // Record where this original vertex starts in the new array
        oldToNewIndexMap[vertexIdx] = newVertices.size();

        // Check if this vertex is part of door impeded cells
        bool isImpededCell = false;
        for (const auto& range : doorImpededRanges) {
            if (vertexIdx >= range.first && vertexIdx < range.first + range.second) {
                isImpededCell = true;
                break;
            }
        }

        if (isImpededCell) {
            impededCount++;
            // Expand impeded cell vertex to fill upscaled grid
            for (uint32_t dy = 0; dy < static_cast<uint32_t>(scaleFactor); dy++) {
                for (uint32_t dx = 0; dx < static_cast<uint32_t>(scaleFactor); dx++) {
                    AREVertex newVertex;
                    newVertex.x = static_cast<int16_t>(origVertex.x * scaleFactor + dx);
                    newVertex.y = static_cast<int16_t>(origVertex.y * scaleFactor + dy);
                    newVertices.push_back(newVertex);
                }
            }
        } else {
            regularCount++;
            // Regular vertex - just scale coordinates
            AREVertex newVertex;
            newVertex.x = static_cast<int16_t>(origVertex.x * scaleFactor);
            newVertex.y = static_cast<int16_t>(origVertex.y * scaleFactor);
            newVertices.push_back(newVertex);
        }
    }

    Log(DEBUG, "ARE", "Vertex classification: {} impeded, {} regular, {} total",
        impededCount, regularCount, areFile.vertices.size());
    
    // Update all vertex indices to point to correct locations in new vertex array
    for (auto& door : areFile.doors) {
        if (door.openImpededCellBlockCount > 0) {
            uint32_t oldIndex = door.openImpededCellBlockIndex;
            door.openImpededCellBlockIndex = oldToNewIndexMap[oldIndex];
            Log(DEBUG, "ARE", "Door open impeded index updated: {} -> {}", oldIndex, door.openImpededCellBlockIndex);
        }
        if (door.closedImpededCellBlockCount > 0) {
            uint32_t oldIndex = door.closedImpededCellBlockIndex;
            door.closedImpededCellBlockIndex = oldToNewIndexMap[oldIndex];
            Log(DEBUG, "ARE", "Door closed impeded index updated: {} -> {}", oldIndex, door.closedImpededCellBlockIndex);
        }
        if (door.openVerticesCount > 0) {
            uint32_t oldIndex = door.openVerticesIndex;
            door.openVerticesIndex = oldToNewIndexMap[oldIndex];
            Log(DEBUG, "ARE", "Door open vertices index updated: {} -> {}", oldIndex, door.openVerticesIndex);
        }
        if (door.closedVerticesCount > 0) {
            uint32_t oldIndex = door.closedVerticesIndex;
            door.closedVerticesIndex = oldToNewIndexMap[oldIndex];
            Log(DEBUG, "ARE", "Door closed vertices index updated: {} -> {}", oldIndex, door.closedVerticesIndex);
        }
    }

    // Update container vertex indices
    for (auto& container : areFile.containers) {
        if (container.verticesCount > 0) {
            uint32_t oldIndex = container.verticesIndex;
            container.verticesIndex = oldToNewIndexMap[oldIndex];
            Log(DEBUG, "ARE", "Container vertices index updated: {} -> {}", oldIndex, container.verticesIndex);
        }
    }

    // Update region vertex indices
    for (auto& region : areFile.regions) {
        if (region.verticesCount > 0) {
            uint32_t oldIndex = region.verticesIndex;
            region.verticesIndex = oldToNewIndexMap[oldIndex];
            Log(DEBUG, "ARE", "Region vertices index updated: {} -> {}", oldIndex, region.verticesIndex);
        }
    }
    
    Log(DEBUG, "ARE", "Expanded vertices: {} -> {} (impeded cells expanded {}x)",
        areFile.vertices.size(), newVertices.size(), expansionFactor);

    areFile.vertices = std::move(newVertices);

    // Update header with correct vertices count and reset offset
    areFile.header.verticesCount = areFile.vertices.size();
    areFile.header.verticesOffset = 0; // Will be set correctly by serialize()

    // Force vertex offset to 0 if no vertices
    if (areFile.vertices.empty()) {
        areFile.header.verticesOffset = 0;
        areFile.header.verticesCount = 0;
    }

    // Save upscaled file
    std::string upscaledDir = getUpscaledDir(true);
    if (upscaledDir.empty()) {
        Log(ERROR, "ARE", "Failed to create upscaled directory.");
        return false;
    }

    std::string upscaledPath = upscaledDir + "/" + resourceName_ + originalExtension;

    Log(DEBUG, "ARE", "Before saveToFile - areFile header vertices: offset={}, count={}",
        areFile.header.verticesOffset, areFile.header.verticesCount);

    if (!saveToFile(upscaledPath)) {
        Log(ERROR, "ARE", "Failed to save upscaled ARE file to: {}", upscaledPath);
        return false;
    }

    Log(MESSAGE, "ARE", "Successfully upscaled ARE file to: {}", upscaledPath);
    return true;
}

bool ARE::saveToFile(const std::string& filePath) {
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

    // Debug: Read back the header from the file to verify it was written correctly
    file.close();
    // std::ifstream checkFile(filePath, std::ios::binary);
    // if (checkFile.is_open()) {
    //     char headerBytes[sizeof(AREHeader)];
    //     checkFile.read(headerBytes, sizeof(AREHeader));
    //     AREHeader fileHeader;
    //     std::memcpy(&fileHeader, headerBytes, sizeof(AREHeader));
    //     Log(DEBUG, "ARE", "Header written to file - vertices: offset={}, count={}",
    //         fileHeader.verticesOffset, fileHeader.verticesCount);
    //     checkFile.close();
    // }

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
                    return PluginManager::getInstance().extractResource(args[0], IE_ARE_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {
                "Upscale ARE coordinates (e.g., are upscale ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: are upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().upscaleResource(args[0], IE_ARE_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {
                "Assemble ARE file (e.g., are assemble ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: are assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().assembleResource(args[0], IE_ARE_CLASS_ID) ? 0 : 1;
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
