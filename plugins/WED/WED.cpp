
#include "WED.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <filesystem>

#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

WED::WED(const std::string& resourceName_) 
    : PluginBase(resourceName_, IE_WED_CLASS_ID) {
    // Load the WED data from the common buffer
    if (!loadFromData()) {
        Log(ERROR, "WED", "Failed to load WED data");
        return;
    }
    
    // Mark plugin as valid since we successfully loaded the WED resource
    valid_ = true;
}

WED::~WED() {
    // Clean up large data structures in WEDFile to prevent memory leaks
    wedFile.overlays.clear();
    wedFile.overlays.shrink_to_fit();
    
    wedFile.doors.clear();
    wedFile.doors.shrink_to_fit();
    
    // Clean up nested tilemaps vector
    wedFile.tilemaps.clear();
    wedFile.tilemaps.shrink_to_fit();
    
    wedFile.doorTileCells.clear();
    wedFile.doorTileCells.shrink_to_fit();
    
    // Clean up nested tileIndices vector
    wedFile.tileIndices.clear();
    wedFile.tileIndices.shrink_to_fit();
    
    wedFile.wallGroups.clear();
    wedFile.wallGroups.shrink_to_fit();
    
    wedFile.polygons.clear();
    wedFile.polygons.shrink_to_fit();
    
    wedFile.polygonIndices.clear();
    wedFile.polygonIndices.shrink_to_fit();
    
    wedFile.vertices.clear();
    wedFile.vertices.shrink_to_fit();
}

bool WED::loadFromData() {
    if (originalFileData.empty()) {
        Log(ERROR, "WED", "No WED data loaded");
        return false;
    }

    // Deserialize the WED data
    if (!wedFile.deserialize(originalFileData)) {
        Log(ERROR, "WED", "Failed to deserialize WED data");
        return false;
    }

    Log(DEBUG, "WED", "Successfully loaded WED resource: {}", resourceName_);
    Log(DEBUG, "WED", "  Overlays: {}", wedFile.overlays.size());
    Log(DEBUG, "WED", "  Doors: {}", wedFile.doors.size());
    Log(DEBUG, "WED", "  Polygons: {}", wedFile.polygons.size());
    Log(DEBUG, "WED", "  Vertices: {}", wedFile.vertices.size());

    return true;
}

bool WED::extract() {
    if (!valid_) {
        Log(ERROR, "WED", "WED file not loaded or invalid");
        return false;
    }
    
    // Get extract directory
    std::string extractPath = getExtractDir();
    
    // Create output filename
    std::string outputPath = extractPath + "/" + resourceName_ + originalExtension;
    
    // Save the extracted WED file
    if (!saveToFile(outputPath)) {
        Log(ERROR, "WED", "Failed to save extracted WED file: {}", outputPath);
        return false;
    }
    
    Log(MESSAGE, "WED", "Successfully extracted WED: {}", resourceName_);
    return true;
}

bool WED::upscale() {
    if (!valid_) {
        Log(ERROR, "WED", "WED file not loaded or invalid");
        return false;
    }
    
    Log(MESSAGE, "WED", "Starting WED upscaling for resource: {}", resourceName_);
    
    // Scale coordinates by upscale factor
    uint32_t upscaleFactor = PIE4K_CFG.UpScaleFactor;
    
    Log(DEBUG, "WED", "Using upscale factor: {}x", upscaleFactor);
    
    // Track mapping from original tilemap indices to expanded tilemap ranges for overlay 0
    // This is needed for door tile cells that reference specific tilemap entries
    std::map<uint16_t, std::pair<uint16_t, uint16_t>> origToExpandedMap; // origIndex -> (startIndex, count)

    // Step 1: Expand tilemaps first (before scaling overlay dimensions)
    for (size_t overlayIdx = 0; overlayIdx < wedFile.overlays.size(); overlayIdx++) {
        if (overlayIdx >= wedFile.tilemaps.size()) continue;

        const auto& overlay = wedFile.overlays[overlayIdx];
        auto& overlayTilemaps = wedFile.tilemaps[overlayIdx];

        size_t origWidth = overlay.width;
        size_t origHeight = overlay.height;

        Log(DEBUG, "WED", "Expanding tilemap for overlay {}: {}x{} -> {}x{}",
            overlayIdx, origWidth, origHeight,
            origWidth * upscaleFactor, origHeight * upscaleFactor);

        // Precompute a no-gap sequential mapping for secondary tiles on overlay 0.
        // Sequence order: dy -> oy -> ox -> dx (only originals that had a secondary)
        std::vector<uint32_t> secSeqMap; // maps (oy,ox,dy,dx) to new secondary index; 0xFFFFFFFF if none
        size_t mainTileCountUpscaled = (origWidth * upscaleFactor) * (origHeight * upscaleFactor);
        if (overlayIdx == 0) {
            secSeqMap.assign(origWidth * origHeight * upscaleFactor * upscaleFactor, 0xFFFFFFFFu);
            uint32_t seq = 0;
            for (uint32_t dy = 0; dy < upscaleFactor; ++dy) {
                for (size_t oy = 0; oy < origHeight; ++oy) {
                    for (size_t ox = 0; ox < origWidth; ++ox) {
                        size_t oi = oy * origWidth + ox;
                        if (oi < overlayTilemaps.size()) {
                            const WEDTilemap &otm = overlayTilemaps[oi];
                            if (otm.secondaryIndex != 0xFFFF) {
                                for (uint32_t dx = 0; dx < upscaleFactor; ++dx) {
                                    size_t pos = (((oi * upscaleFactor) + dy) * upscaleFactor) + dx;
                                    secSeqMap[pos] = static_cast<uint32_t>(mainTileCountUpscaled + seq);
                                    ++seq;
                                }
                            }
                        }
                    }
                }
            }
            Log(DEBUG, "WED", "Computed sequential secondary mapping: {} tiles", 
                std::count_if(secSeqMap.begin(), secSeqMap.end(), [](uint32_t v){return v!=0xFFFFFFFFu;}));
        }

        // Create expanded tilemap grid in strict row-major order of the upscaled grid
        std::vector<WEDTilemap> newTilemaps;
        newTilemaps.reserve(origWidth * origHeight * upscaleFactor * upscaleFactor);

        for (size_t y = 0; y < origHeight; y++) {
            for (uint32_t dy = 0; dy < upscaleFactor; dy++) {
                for (size_t x = 0; x < origWidth; x++) {
                    size_t origIndex = y * origWidth + x;
                    if (origIndex < overlayTilemaps.size()) {
                        const WEDTilemap& origTilemap = overlayTilemaps[origIndex];

                        // Debug: Log original tilemap data for first few tiles
                        if (origIndex < 5 && dy == 0) {
                            Log(DEBUG, "WED", "Original tilemap[{}]: startIndex={}, tileCount={}, secondaryIndex={}",
                                origIndex, origTilemap.startIndex, origTilemap.tileCount, origTilemap.secondaryIndex);
                        }

                        for (uint32_t dx = 0; dx < upscaleFactor; dx++) {
                            WEDTilemap newTilemap = origTilemap;

                            // Each new tilemap gets the next sequential index (row-major in upscaled grid)
                            newTilemap.startIndex = static_cast<uint16_t>(newTilemaps.size());
                            newTilemap.tileCount = 1; // Each tilemap now references exactly one tile

                            // Handle secondary tile index mapping ONLY for overlay 0 using precomputed sequential map
                            if (overlayIdx == 0) {
                                size_t oi = y * origWidth + x;
                                size_t pos = (((oi * upscaleFactor) + dy) * upscaleFactor) + dx;
                                if (pos < secSeqMap.size() && secSeqMap[pos] != 0xFFFFFFFFu) {
                                    newTilemap.secondaryIndex = static_cast<uint16_t>(secSeqMap[pos]);
                                } else {
                                    newTilemap.secondaryIndex = 0xFFFF;
                                }
                            }

                            newTilemaps.push_back(newTilemap);
                        }
                    }
                }
            }
        }

        overlayTilemaps = std::move(newTilemaps);

        // After creating the primary grid, we previously had an explicit pass to place secondary tiles
        // for doors that might not be referenced by any primary tilemap entry. That post-pass caused
        // misaligned rows and incorrect placement in some areas (e.g., AR0020). Since the initial expansion
        // already sets the correct secondary tiles for door-referenced cells in these cases, we skip the
        // explicit post-pass placement to preserve correct stacking and position.
        // If future WEDs need it, we can reintroduce a guarded, position-based placement here.

        Log(MESSAGE, "WED", "Created {} tilemaps for overlay {} (expected: {}x{} = {})",
            overlayTilemaps.size(), overlayIdx,
            origWidth * upscaleFactor, origHeight * upscaleFactor,
            origWidth * origHeight * upscaleFactor * upscaleFactor);
    }
    
    // Step 2: Scale overlay dimensions to match expanded grid
    for (auto& overlay : wedFile.overlays) {
        uint16_t origWidth = overlay.width;
        uint16_t origHeight = overlay.height;
        
        overlay.width = static_cast<uint16_t>(overlay.width * upscaleFactor);
        overlay.height = static_cast<uint16_t>(overlay.height * upscaleFactor);
        
        Log(DEBUG, "WED", "Scaled overlay dimensions: {}x{} -> {}x{}", 
            origWidth, origHeight, overlay.width, overlay.height);
    }
    
    // Step 3: Create sequential tile indices for the upscaled grid
    for (size_t overlayIdx = 0; overlayIdx < wedFile.overlays.size(); overlayIdx++) {
        if (overlayIdx >= wedFile.tileIndices.size()) continue;
        
        auto& overlayIndices = wedFile.tileIndices[overlayIdx];
        
        // Get upscaled overlay dimensions
        size_t newWidth = wedFile.overlays[overlayIdx].width;
        size_t newHeight = wedFile.overlays[overlayIdx].height;
        size_t totalTiles = newWidth * newHeight;
        
        Log(DEBUG, "WED", "Creating sequential tile indices for overlay {}: {}x{} = {} indices", 
            overlayIdx, newWidth, newHeight, totalTiles);
        
        // Create sequential indices 0, 1, 2, 3, ..., totalTiles-1
        overlayIndices.clear();
        overlayIndices.reserve(totalTiles);
        
        for (size_t i = 0; i < totalTiles; i++) {
            overlayIndices.push_back(static_cast<uint16_t>(i));
        }
    }
    
    // Step 4: Update door tile cells to reference expanded tilemap
    Log(DEBUG, "WED", "Updating door tile cells for expanded tilemap (per-door rebuild)");

    // Door tile cells are indices into the tilemap array. We must expand them per-door so that
    // firstDoorTile offsets remain correct after expansion. Each original entry expands to factor×factor
    // contiguous positions based on its original (x,y).
    if (!wedFile.overlays.empty()) {
        size_t origWidth = wedFile.overlays[0].width / upscaleFactor;  // Original width before scaling
        size_t origHeight = wedFile.overlays[0].height / upscaleFactor; // Original height before scaling
        size_t newWidth = wedFile.overlays[0].width;                   // Current (scaled) width
        size_t newHeight = wedFile.overlays[0].height;                 // Current (scaled) height

        std::vector<uint16_t> rebuiltDoorTileCells;
        rebuiltDoorTileCells.reserve(wedFile.doorTileCells.size() * upscaleFactor * upscaleFactor);

        size_t writeOffset = 0; // new offset into rebuiltDoorTileCells

        for (size_t doorIdx = 0; doorIdx < wedFile.doors.size(); ++doorIdx) {
            auto& door = wedFile.doors[doorIdx];

            uint16_t origFirst = door.firstDoorTile;
            uint16_t origCount = door.doorTileCount;

            // New first offset is current write position
            door.firstDoorTile = static_cast<uint16_t>(writeOffset);

            // Expand each original cell referenced by this door
            for (uint16_t i = 0; i < origCount; ++i) {
                size_t doorTileIdx = static_cast<size_t>(origFirst) + i;
                if (doorTileIdx >= wedFile.doorTileCells.size()) {
                    Log(ERROR, "WED", "Door {} references invalid doorTileIdx {} (max: {})",
                        doorIdx, doorTileIdx, wedFile.doorTileCells.size() - 1);
                    continue;
                }

                uint16_t origTilemapIndex = wedFile.doorTileCells[doorTileIdx];

                // Calculate original x, y from tilemap index
                size_t origX = origTilemapIndex % origWidth;
                size_t origY = origTilemapIndex / origWidth;

                // Add all factor×factor positions for this original tile in row-major order of the upscaled grid
                for (uint32_t dy = 0; dy < upscaleFactor; ++dy) {
                    for (uint32_t dx = 0; dx < upscaleFactor; ++dx) {
                        size_t newX = origX * upscaleFactor + dx;
                        size_t newY = origY * upscaleFactor + dy;
                        uint32_t newTilemapIndex = static_cast<uint32_t>(newY * newWidth + newX);

                        if (newTilemapIndex >= newWidth * newHeight) {
                            Log(ERROR, "WED", "Generated invalid tilemap index {} (max: {}) for door {} cell {}",
                                newTilemapIndex, newWidth * newHeight - 1, doorIdx, i);
                            continue;
                        }

                        rebuiltDoorTileCells.push_back(static_cast<uint16_t>(newTilemapIndex));
                        ++writeOffset;
                    }
                }
            }

            uint16_t newCount = static_cast<uint16_t>(origCount * upscaleFactor * upscaleFactor);
            door.doorTileCount = newCount;

            std::string doorName = door.getName();
            Log(DEBUG, "WED", "Door {}: '{}' firstDoorTile={} count={}",
                doorIdx, doorName.c_str(), door.firstDoorTile, door.doorTileCount);
        }

        // Replace old door tile cells with per-door rebuilt version
        wedFile.doorTileCells = std::move(rebuiltDoorTileCells);
        Log(DEBUG, "WED", "Rebuilt door tile cells: {} entries",
            wedFile.doorTileCells.size());
    }
    
    // Step 5: Recalculate wall groups for new overlay dimensions
    // Wall groups are organized by grid sections, so when overlay dimensions change,
    // we need to recalculate the grid layout
    Log(DEBUG, "WED", "Recalculating wall groups for upscaled overlay dimensions");
    
    if (!wedFile.overlays.empty()) {
        size_t newWidth = wedFile.overlays[0].width;    // Now upscaled (100)
        size_t newHeight = wedFile.overlays[0].height * 2;  // Now upscaled * 2 (144)
        size_t newGroupSize = ((newWidth + 9) / 10) * ((newHeight + 14) / 15);
        size_t oldGroupSize = wedFile.wallGroups.size();
        
        Log(DEBUG, "WED", "Wall groups: {} -> {} groups for {}x{} grid", 
            oldGroupSize, newGroupSize, newWidth/2, newHeight/2);
        
        // For now, extend with empty wall groups to match expected count
        // This preserves original wall group data
        wedFile.wallGroups.resize(newGroupSize);
        
        // Initialize new wall groups as empty (startIndex=0, indexCount=0)
        for (size_t i = oldGroupSize; i < newGroupSize; i++) {
            wedFile.wallGroups[i] = WEDWallGroup{};
        }
    }
    
    // Step 6: Scale coordinate data
    
    // Scale vertices
    for (auto& vertex : wedFile.vertices) {
        vertex.x = static_cast<uint16_t>(vertex.x * upscaleFactor);
        vertex.y = static_cast<uint16_t>(vertex.y * upscaleFactor);
    }
    
    // Scale polygon bounding boxes
    for (auto& polygon : wedFile.polygons) {
        polygon.minX = static_cast<uint16_t>(polygon.minX * upscaleFactor);
        polygon.maxX = static_cast<uint16_t>(polygon.maxX * upscaleFactor);
        polygon.minY = static_cast<uint16_t>(polygon.minY * upscaleFactor);
        polygon.maxY = static_cast<uint16_t>(polygon.maxY * upscaleFactor);
    }
    
    // Save the upscaled WED file
    std::string upscaledPath = getUpscaledDir() + "/" + resourceName_ + originalExtension;
    if (!saveToFile(upscaledPath)) {
        Log(ERROR, "WED", "Failed to save upscaled WED file: {}", upscaledPath);
        return false;
    }
    
    Log(MESSAGE, "WED", "Successfully upscaled WED: {}", resourceName_);
    return true;
}

bool WED::assemble() {
    if (!valid_) {
        Log(ERROR, "WED", "WED file not loaded or invalid");
        return false;
    }
    
    Log(MESSAGE, "WED", "Starting WED assembly for resource: {}", resourceName_);
    
    // Path of already upscaled WED
    std::string upscaledPath = getUpscaledDir(false) + "/" + resourceName_ + originalExtension;
    if (!std::filesystem::exists(upscaledPath)) {
        Log(ERROR, "WED", "Upscaled WED file not found: {}", upscaledPath);
        return false;
    }
    
    // Ensure assemble dir exists
    std::string assemblePath = getAssembleDir(true) + "/" + originalFileName;
    
    try {
        std::filesystem::copy_file(upscaledPath, assemblePath, std::filesystem::copy_options::overwrite_existing);
        Log(MESSAGE, "WED", "Successfully assembled WED (copied): {} -> {}", upscaledPath, assemblePath);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "WED", "Failed to copy upscaled WED: {}", e.what());
        return false;
    }
}

// Path management overrides
std::string WED::getOutputDir(bool ensureDir) const {
    return constructPath("-wed", ensureDir);
}

std::string WED::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-wed-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string WED::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-wed-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string WED::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-wed-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

// Using default service-based upscale() from PluginBase

bool WED::saveToFile(const std::string& filePath) const {
    // Debug: Calculate expected sizes for each section
    size_t expectedHeaderSize = sizeof(WEDHeader);
    size_t expectedOverlaysSize = wedFile.overlays.size() * sizeof(WEDOverlay);
    size_t expectedSecHeaderSize = sizeof(WEDSecondaryHeader);
    size_t expectedDoorsSize = wedFile.doors.size() * sizeof(WEDDoor);
    
    size_t expectedTilemapsSize = 0;
    for (const auto& overlay_tilemaps : wedFile.tilemaps) {
        expectedTilemapsSize += overlay_tilemaps.size() * sizeof(WEDTilemap);
    }
    
    size_t expectedDoorTilesSize = wedFile.doorTileCells.size() * sizeof(uint16_t);
    
    size_t expectedTileIndicesSize = 0;
    for (const auto& overlay_indices : wedFile.tileIndices) {
        expectedTileIndicesSize += overlay_indices.size() * sizeof(uint16_t);
    }
    
    size_t expectedWallGroupsSize = wedFile.wallGroups.size() * sizeof(WEDWallGroup);
    size_t expectedPolygonsSize = wedFile.polygons.size() * sizeof(WEDPolygon);
    size_t expectedPolygonIndicesSize = wedFile.polygonIndices.size() * sizeof(uint16_t);
    size_t expectedVerticesSize = wedFile.vertices.size() * sizeof(WEDVertex);
    
    size_t expectedTotalSize = expectedHeaderSize + expectedOverlaysSize + expectedSecHeaderSize + 
                             expectedDoorsSize + expectedTilemapsSize + expectedDoorTilesSize + 
                             expectedTileIndicesSize + expectedWallGroupsSize + expectedPolygonsSize + 
                             expectedPolygonIndicesSize + expectedVerticesSize;
    
    Log(DEBUG, "WED", "Expected total file size: {} bytes", expectedTotalSize);
    Log(DEBUG, "WED", "Section sizes:");
    Log(DEBUG, "WED", "  Header: {}, Overlays: {}, SecHeader: {}, Doors: {}", 
           expectedHeaderSize, expectedOverlaysSize, expectedSecHeaderSize, expectedDoorsSize);
    Log(DEBUG, "WED", "  Tilemaps: {}, DoorTiles: {}, TileIndices: {}", 
           expectedTilemapsSize, expectedDoorTilesSize, expectedTileIndicesSize);
    Log(DEBUG, "WED", "  WallGroups: {}, Polygons: {}, PolygonIndices: {}, Vertices: {}",
           expectedWallGroupsSize, expectedPolygonsSize, expectedPolygonIndicesSize, expectedVerticesSize);
    
    std::vector<uint8_t> data = wedFile.serialize();
    
    Log(DEBUG, "WED", "Actual file size: {} bytes (expected: {} bytes)", data.size(), expectedTotalSize);
    if (data.size() != expectedTotalSize) {
        Log(ERROR, "WED", "Size mismatch! Difference: {} bytes", 
               static_cast<int64_t>(data.size()) - static_cast<int64_t>(expectedTotalSize));
    }
    
    // Write file
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "WED", "Could not create file: {}", filePath);
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    
    if (file.fail()) {
        Log(ERROR, "WED", "Failed to write file: {}", filePath);
        return false;
    }
    
    return true;
}

// Batch operations
bool WED::extractAll() {
    return PluginManager::getInstance().extractAllResourcesOfType(IE_WED_CLASS_ID);
}

bool WED::upscaleAll() {
    return PluginManager::getInstance().upscaleAllResourcesOfType(IE_WED_CLASS_ID);
}

bool WED::assembleAll() {
    return PluginManager::getInstance().assembleAllResourcesOfType(IE_WED_CLASS_ID);
}

// Clean directories
bool WED::cleanExtractDirectory() {
    return cleanDirectory(getOutputDir(false));
}

bool WED::cleanUpscaleDirectory() {
    return cleanDirectory(getUpscaledDir(false));
}

bool WED::cleanAssembleDirectory() {
    return cleanDirectory(getAssembleDir(false));
}

bool WED::cleanDirectory(const std::string& dir) {
    if (!std::filesystem::exists(dir)) {
        return true;
    }
    
    try {
        std::filesystem::remove_all(dir);
        Log(DEBUG, "WED", "Cleaned directory: {}", dir);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "WED", "Failed to clean directory {}: {}", dir, e.what());
        return false;
    }
}

// Command registration
void WED::registerCommands(CommandTable& commandTable) {
    commandTable["wed"] = {
        "WED file operations",
        {
            {"extract", {
                "Extract WED resource to file (e.g., wed extract ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: wed extract <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().extractResource(args[0], IE_WED_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {
                "Upscale WED coordinates (e.g., wed upscale ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: wed upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().upscaleResource(args[0], IE_WED_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {
                "Assemble WED file (e.g., wed assemble ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: wed assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().assembleResource(args[0], IE_WED_CLASS_ID) ? 0 : 1;
                }
            }}
        }
    };
}

REGISTER_PLUGIN(WED, IE_WED_CLASS_ID);

} // namespace ProjectIE4k
