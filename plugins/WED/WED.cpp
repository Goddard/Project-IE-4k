
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

bool WED::saveToFile13(const std::string& filePath) const {
    // Debug: Calculate expected sizes for each section
    size_t expectedHeaderSize = sizeof(WEDHeader);
    size_t expectedOverlaysSize = wedFile13.overlays.size() * sizeof(WEDOverlay);
    size_t expectedSecHeaderSize = sizeof(WEDSecondaryHeader);
    size_t expectedDoorsSize = wedFile13.doors.size() * sizeof(WEDDoor);
    
    size_t expectedTilemapsSize = 0;
    for (const auto& overlay_tilemaps : wedFile13.tilemaps) {
        expectedTilemapsSize += overlay_tilemaps.size() * sizeof(WEDTilemap);
    }
    
    size_t expectedDoorTilesSize = wedFile13.doorTileCells.size() * sizeof(uint16_t);
    
    size_t expectedTileIndicesSize = 0;
    for (const auto& overlay_indices : wedFile13.tileIndices) {
        expectedTileIndicesSize += overlay_indices.size() * sizeof(uint16_t);
    }
    
    size_t expectedWallGroupsSize = wedFile13.wallGroups.size() * sizeof(WEDWallGroup);
    size_t expectedPolygonsSize = wedFile13.polygons.size() * sizeof(WEDPolygon);
    size_t expectedPolygonIndicesSize = wedFile13.polygonIndices.size() * sizeof(uint16_t);
    size_t expectedVerticesSize = wedFile13.vertices.size() * sizeof(WEDVertex);
    
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
    
    std::vector<uint8_t> data = wedFile13.serialize();
    
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

    // 1.3
    wedFile13.overlays.clear();
    wedFile13.overlays.shrink_to_fit();
    
    wedFile13.doors.clear();
    wedFile13.doors.shrink_to_fit();
    
    // Clean up nested tilemaps vector
    wedFile13.tilemaps.clear();
    wedFile13.tilemaps.shrink_to_fit();
    
    wedFile13.doorTileCells.clear();
    wedFile13.doorTileCells.shrink_to_fit();
    
    // Clean up nested tileIndices vector
    wedFile13.tileIndices.clear();
    wedFile13.tileIndices.shrink_to_fit();
    
    wedFile13.wallGroups.clear();
    wedFile13.wallGroups.shrink_to_fit();
    
    wedFile13.polygons.clear();
    wedFile13.polygons.shrink_to_fit();
    
    wedFile13.polygonIndices.clear();
    wedFile13.polygonIndices.shrink_to_fit();
    
    wedFile13.vertices.clear();
    wedFile13.vertices.shrink_to_fit();
}

bool WED::loadFromData() {
    if (originalFileData.empty()) {
        Log(ERROR, "WED", "No WED data loaded");
        return false;
    }

    if (!wedFile13.deserialize(originalFileData)) {
        Log(ERROR, "WED", "Failed to deserialize WED 1.3 data");
        return false;
    }

    // Deserialize the WED data
    if (!wedFile.deserialize(originalFileData)) {
        Log(ERROR, "WED", "Failed to deserialize WED 1.4 data");
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
    
    // Save the extracted WED file as a byte-for-byte dump of the original data
    {
        std::ofstream file(outputPath, std::ios::binary);
        if (!file.is_open()) {
            Log(ERROR, "WED", "Could not create file: {}", outputPath);
            return false;
        }
        if (!originalFileData.empty()) {
            file.write(reinterpret_cast<const char*>(originalFileData.data()), originalFileData.size());
        }
        if (file.fail()) {
            Log(ERROR, "WED", "Failed to write file: {}", outputPath);
            return false;
        }
    }
    
    Log(MESSAGE, "WED", "Successfully extracted WED: {}", resourceName_);
    return true;
}

bool WED::upscale13() {
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
    for (size_t overlayIdx = 0; overlayIdx < wedFile13.overlays.size(); overlayIdx++) {
        if (overlayIdx >= wedFile13.tilemaps.size()) continue;

        const auto& overlay = wedFile13.overlays[overlayIdx];
        auto& overlayTilemaps = wedFile13.tilemaps[overlayIdx];

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
    for (auto& overlay : wedFile13.overlays) {
        uint16_t origWidth = overlay.width;
        uint16_t origHeight = overlay.height;
        
        overlay.width = static_cast<uint16_t>(overlay.width * upscaleFactor);
        overlay.height = static_cast<uint16_t>(overlay.height * upscaleFactor);
        
        Log(DEBUG, "WED", "Scaled overlay dimensions: {}x{} -> {}x{}", 
            origWidth, origHeight, overlay.width, overlay.height);
    }
    
    // Step 3: Create sequential tile indices for the upscaled grid
    for (size_t overlayIdx = 0; overlayIdx < wedFile13.overlays.size(); overlayIdx++) {
        if (overlayIdx >= wedFile13.tileIndices.size()) continue;
        
        auto& overlayIndices = wedFile13.tileIndices[overlayIdx];
        
        // Get upscaled overlay dimensions
        size_t newWidth = wedFile13.overlays[overlayIdx].width;
        size_t newHeight = wedFile13.overlays[overlayIdx].height;
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
    if (!wedFile13.overlays.empty()) {
        size_t origWidth = wedFile13.overlays[0].width / upscaleFactor;  // Original width before scaling
        size_t origHeight = wedFile13.overlays[0].height / upscaleFactor; // Original height before scaling
        size_t newWidth = wedFile13.overlays[0].width;                   // Current (scaled) width
        size_t newHeight = wedFile13.overlays[0].height;                 // Current (scaled) height

        std::vector<uint16_t> rebuiltDoorTileCells;
        rebuiltDoorTileCells.reserve(wedFile13.doorTileCells.size() * upscaleFactor * upscaleFactor);

        size_t writeOffset = 0; // new offset into rebuiltDoorTileCells

        for (size_t doorIdx = 0; doorIdx < wedFile13.doors.size(); ++doorIdx) {
            auto& door = wedFile13.doors[doorIdx];

            uint16_t origFirst = door.firstDoorTile;
            uint16_t origCount = door.doorTileCount;

            // New first offset is current write position
            door.firstDoorTile = static_cast<uint16_t>(writeOffset);

            // Expand each original cell referenced by this door
            for (uint16_t i = 0; i < origCount; ++i) {
                size_t doorTileIdx = static_cast<size_t>(origFirst) + i;
                if (doorTileIdx >= wedFile13.doorTileCells.size()) {
                    Log(ERROR, "WED", "Door {} references invalid doorTileIdx {} (max: {})",
                        doorIdx, doorTileIdx, wedFile13.doorTileCells.size() - 1);
                    continue;
                }

                uint16_t origTilemapIndex = wedFile13.doorTileCells[doorTileIdx];

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
        wedFile13.doorTileCells = std::move(rebuiltDoorTileCells);
        Log(DEBUG, "WED", "Rebuilt door tile cells: {} entries",
            wedFile13.doorTileCells.size());
    }
    
    // Step 5: Recalculate wall groups for new overlay dimensions
    // Wall groups are organized by grid sections, so when overlay dimensions change,
    // we need to recalculate the grid layout
    Log(DEBUG, "WED", "Recalculating wall groups for upscaled overlay dimensions");
    
    if (!wedFile13.overlays.empty()) {
        size_t newWidth = wedFile13.overlays[0].width;    // Now upscaled (100)
        size_t newHeight = wedFile13.overlays[0].height * 2;  // Now upscaled * 2 (144)
        size_t newGroupSize = ((newWidth + 9) / 10) * ((newHeight + 14) / 15);
        size_t oldGroupSize = wedFile13.wallGroups.size();
        
        Log(DEBUG, "WED", "Wall groups: {} -> {} groups for {}x{} grid", 
            oldGroupSize, newGroupSize, newWidth/2, newHeight/2);
        
        // For now, extend with empty wall groups to match expected count
        // This preserves original wall group data
        wedFile13.wallGroups.resize(newGroupSize);
        
        // Initialize new wall groups as empty (startIndex=0, indexCount=0)
        for (size_t i = oldGroupSize; i < newGroupSize; i++) {
            wedFile13.wallGroups[i] = WEDWallGroup{};
        }
    }
    
    // Step 6: Scale coordinate data
    
    // Scale vertices
    for (auto& vertex : wedFile13.vertices) {
        vertex.x = static_cast<uint16_t>(vertex.x * upscaleFactor);
        vertex.y = static_cast<uint16_t>(vertex.y * upscaleFactor);
    }
    
    // Scale polygon bounding boxes
    for (auto& polygon : wedFile13.polygons) {
        polygon.minX = static_cast<uint16_t>(polygon.minX * upscaleFactor);
        polygon.maxX = static_cast<uint16_t>(polygon.maxX * upscaleFactor);
        polygon.minY = static_cast<uint16_t>(polygon.minY * upscaleFactor);
        polygon.maxY = static_cast<uint16_t>(polygon.maxY * upscaleFactor);
    }
    
    // Save the upscaled WED file
    std::string upscaledPath = getUpscaledDir() + "/" + resourceName_ + "-13" + originalExtension;
    if (!saveToFile13(upscaledPath)) {
        Log(ERROR, "WED", "Failed to save upscaled WED file: {}", upscaledPath);
        return false;
    }
    
    Log(MESSAGE, "WED", "Successfully upscaled WED: {}", resourceName_);
    return true;
}

bool WED::upscale() {
    // if(!upscale13()) {
    //     Log(ERROR, "WED", "WED 1.3 failed to upscale");
    //     return false;
    // }

    // Build V1.4 structure directly from upscaled V1.3 in-memory data (keep formats separate)
    if (!valid_) {
        Log(ERROR, "WED", "WED file not loaded or invalid");
        return false;
    }

    const uint32_t upscaleFactor = PIE4K_CFG.UpScaleFactor;
    Log(MESSAGE, "WED", "Upscaling V1.4 directly from source V1.3 for resource: {}", resourceName_);

    // Reset V1.4 container
    wedFile = WEDFileV14{};

    // 1) Overlays: scale dimensions and copy tileset/unknown
    wedFile.overlays.resize(wedFile13.overlays.size());
    for (size_t i = 0; i < wedFile13.overlays.size(); ++i) {
        const auto &ov13 = wedFile13.overlays[i];
        auto &ov14 = wedFile.overlays[i];
        uint16_t origW = ov13.width;
        uint16_t origH = ov13.height;
        ov14.width = static_cast<uint16_t>(origW * upscaleFactor);
        ov14.height = static_cast<uint16_t>(origH * upscaleFactor);
        std::string tn = ov13.getTilesetName();
        memset(ov14.tilesetName, 0, 8);
        memcpy(ov14.tilesetName, tn.c_str(), std::min<size_t>(8, tn.size()));
        ov14.unknown = ov13.unknown;
        ov14.tilemapOffset = 0;
        ov14.tileIndexOffset = 0;
    }

    // 2) Tilemaps: expand grid, sequential startIndex, tileCount=1, secondaryIndex mapping on overlay 0
    wedFile.tilemaps.clear();
    wedFile.tilemaps.resize(wedFile13.tilemaps.size());
    for (size_t overlayIdx = 0; overlayIdx < wedFile13.overlays.size(); ++overlayIdx) {
        const auto &ov13 = wedFile13.overlays[overlayIdx];
        size_t origW = ov13.width;
        size_t origH = ov13.height;
        const auto &srcTilemaps = (overlayIdx < wedFile13.tilemaps.size()) ? wedFile13.tilemaps[overlayIdx] : std::vector<WEDTilemap>{};

        // Precompute secondary map only for overlay 0
        std::vector<uint32_t> secSeqMap;
        if (overlayIdx == 0) {
            secSeqMap.assign(origW * origH * upscaleFactor * upscaleFactor, 0xFFFFFFFFu);
            uint32_t seq = 0;
            const size_t mainTileCountUpscaled = (origW * upscaleFactor) * (origH * upscaleFactor);
            for (uint32_t dy = 0; dy < upscaleFactor; ++dy) {
                for (size_t oy = 0; oy < origH; ++oy) {
                    for (size_t ox = 0; ox < origW; ++ox) {
                        size_t oi = oy * origW + ox;
                        if (oi < srcTilemaps.size()) {
                            const WEDTilemap &otm = srcTilemaps[oi];
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
        }

        auto &dstTilemaps = wedFile.tilemaps[overlayIdx];
        dstTilemaps.reserve(origW * origH * upscaleFactor * upscaleFactor);
        for (size_t y = 0; y < origH; ++y) {
            for (uint32_t dy = 0; dy < upscaleFactor; ++dy) {
                for (size_t x = 0; x < origW; ++x) {
                    size_t oi = y * origW + x;
                    if (oi < srcTilemaps.size()) {
                        const WEDTilemap &otm = srcTilemaps[oi];
                        for (uint32_t dx = 0; dx < upscaleFactor; ++dx) {
                            WEDTilemapV14 tm14{};
                            tm14.startIndex = static_cast<uint32_t>(dstTilemaps.size());
                            tm14.tileCount = 1;
                            tm14.overlayFlags = otm.overlayFlags;
                            memcpy(tm14.unknown, otm.unknown, 3);
                            if (overlayIdx == 0) {
                                size_t pos = (((oi * upscaleFactor) + dy) * upscaleFactor) + dx;
                                tm14.secondaryIndex = (pos < secSeqMap.size() && secSeqMap[pos] != 0xFFFFFFFFu)
                                    ? secSeqMap[pos]
                                    : 0xFFFFFFFFu;
                            } else {
                                tm14.secondaryIndex = (otm.secondaryIndex == 0xFFFF) ? 0xFFFFFFFFu : static_cast<uint32_t>(otm.secondaryIndex);
                            }
                            dstTilemaps.push_back(tm14);
                        }
                    }
                }
            }
        }
        Log(MESSAGE, "WED", "Created {} tilemaps for overlay {} (expected: {}x{} = {})",
            dstTilemaps.size(), overlayIdx,
            ov13.width * upscaleFactor, ov13.height * upscaleFactor,
            ov13.width * ov13.height * upscaleFactor * upscaleFactor);
    }

    // 3) Tile indices per overlay: sequential uint32 0..W*H-1
    wedFile.tileIndices.clear();
    wedFile.tileIndices.resize(wedFile.overlays.size());
    for (size_t overlayIdx = 0; overlayIdx < wedFile.overlays.size(); ++overlayIdx) {
        const auto &ov14 = wedFile.overlays[overlayIdx];
        size_t total = static_cast<size_t>(ov14.width) * static_cast<size_t>(ov14.height);
        auto &idx = wedFile.tileIndices[overlayIdx];
        idx.reserve(total);
        for (size_t i = 0; i < total; ++i) idx.push_back(static_cast<uint32_t>(i));
    }

    // 4) Rebuild doors and door tile cells (uint32)
    wedFile.doors.clear();
    wedFile.doors.reserve(wedFile13.doors.size());
    wedFile.doorTileCells.clear();
    {
        size_t newWidth = wedFile.overlays.empty() ? 0 : wedFile.overlays[0].width;
        size_t newHeight = wedFile.overlays.empty() ? 0 : wedFile.overlays[0].height;
        size_t origWidth = wedFile13.overlays.empty() ? 0 : wedFile13.overlays[0].width;
        size_t origHeight = wedFile13.overlays.empty() ? 0 : wedFile13.overlays[0].height;
        
        for (const auto &d13 : wedFile13.doors) {
            WEDDoorV14 d14{};
            memcpy(d14.name, d13.name, 8);
            d14.openClosed = d13.openClosed;
            d14.openPolygonCount = d13.openPolygonCount;
            d14.closedPolygonCount = d13.closedPolygonCount;
            d14.openPolygonOffset = d13.openPolygonOffset;
            d14.closedPolygonOffset = d13.closedPolygonOffset;

            uint16_t origFirst = d13.firstDoorTile;
            uint16_t origCount = d13.doorTileCount;
            
            // Set firstDoorTile to current size of doorTileCells array
            d14.firstDoorTile = static_cast<uint32_t>(wedFile.doorTileCells.size());

            Log(DEBUG, "WED", "Door '{}' processing {} tiles starting at index {} -> {}",
                std::string(d14.name, 8), origCount, origFirst, d14.firstDoorTile);

            for (uint16_t i = 0; i < origCount; ++i) {
                size_t doorTileIdx = static_cast<size_t>(origFirst) + i;
                if (doorTileIdx >= wedFile13.doorTileCells.size()) {
                    Log(ERROR, "WED", "Door {} tile {} references invalid doorTileIdx {} (max: {})",
                        wedFile.doors.size(), i, doorTileIdx, wedFile13.doorTileCells.size() - 1);
                    continue;
                }
                uint16_t origTilemapIndex = wedFile13.doorTileCells[doorTileIdx];
                
                // Validate original tilemap index is within bounds
                if (origTilemapIndex >= origWidth * origHeight) {
                    Log(ERROR, "WED", "Door {} tile {} has invalid tilemap index {} (max: {})",
                        wedFile.doors.size(), i, origTilemapIndex, (origWidth * origHeight) - 1);
                    continue;
                }
                
                size_t ox = origTilemapIndex % origWidth;
                size_t oy = origTilemapIndex / origWidth;
                for (uint32_t dy = 0; dy < upscaleFactor; ++dy) {
                    for (uint32_t dx = 0; dx < upscaleFactor; ++dx) {
                        size_t nx = ox * upscaleFactor + dx;
                        size_t ny = oy * upscaleFactor + dy;
                        uint32_t newIdx = static_cast<uint32_t>(ny * newWidth + nx);
                        
                        // Validate new index is within bounds
                        if (newIdx >= newWidth * newHeight) {
                            Log(ERROR, "WED", "Door {} generated invalid tilemap index {} (max: {})",
                                wedFile.doors.size(), newIdx, (newWidth * newHeight) - 1);
                            continue;
                        }
                        
                        wedFile.doorTileCells.push_back(newIdx);
                    }
                }
            }
            d14.doorTileCount = static_cast<uint32_t>(origCount) * upscaleFactor * upscaleFactor;
            
            Log(DEBUG, "WED", "Door '{}' generated {} tile cells ({}x{} expansion)",
                std::string(d14.name, 8), d14.doorTileCount, upscaleFactor, upscaleFactor);
            
            wedFile.doors.push_back(d14);
        }
    }

    // 5) Build wall groups for V1.4 grid
    // Engine expects groupSize = ceil(W/10) * ceil((H*2)/15) for overlay 0
    wedFile.wallGroups.clear();
    if (!wedFile.overlays.empty()) {
        const size_t newW = wedFile.overlays[0].width;
        const size_t newH2 = static_cast<size_t>(wedFile.overlays[0].height) * 2; // doubled height for groups
        const size_t newGroupSize = ((newW + 9) / 10) * ((newH2 + 14) / 15);
        const size_t oldGroupSize = wedFile13.wallGroups.size();

        Log(DEBUG, "WED", "Wall groups: {} -> {} groups for W={}, H2={}",
            oldGroupSize, newGroupSize, newW, newH2);

        wedFile.wallGroups.resize(newGroupSize);
        // Initialize all groups to empty (safe defaults)
        for (auto &wg : wedFile.wallGroups) {
            wg.startIndex = 0;
            wg.indexCount = 0;
        }
        // Copy original V1.3 wall groups into the beginning (bounds permitting)
        const size_t copyN = std::min(oldGroupSize, newGroupSize);
        for (size_t i = 0; i < copyN; ++i) {
            const auto &wg13 = wedFile13.wallGroups[i];
            wedFile.wallGroups[i].startIndex = wg13.startIndex;
            wedFile.wallGroups[i].indexCount = wg13.indexCount;
        }
    }

    // 6) Polygons/Vertices: copy from source and scale by factor
    wedFile.polygons.clear();
    wedFile.polygons.reserve(wedFile13.polygons.size());
    // Debug: dump a few source polygons
    for (size_t i = 8; i < wedFile13.polygons.size() && i < 13; ++i) {
        const auto &p = wedFile13.polygons[i];
        Log(DEBUG, "WED", "[upscale V1.4] src Poly[{}] sv={} cnt={} flags=0x{:02X} h={} bbox=({},{})-({},{}).",
            (unsigned)i, (unsigned)p.startVertex, (unsigned)p.vertexCount, (unsigned)p.flags, (int)p.height,
            (unsigned)p.minX, (unsigned)p.minY, (unsigned)p.maxX, (unsigned)p.maxY);
    }
    for (const auto &p13 : wedFile13.polygons) {
        WEDPolygonV14 p14{};
        p14.startVertex = p13.startVertex;
        p14.vertexCount = p13.vertexCount;
        p14.flags = p13.flags;
        p14.height = p13.height;
        p14.minX = static_cast<uint16_t>(p13.minX * upscaleFactor);
        p14.maxX = static_cast<uint16_t>(p13.maxX * upscaleFactor);
        p14.minY = static_cast<uint16_t>(p13.minY * upscaleFactor);
        p14.maxY = static_cast<uint16_t>(p13.maxY * upscaleFactor);
        wedFile.polygons.push_back(p14);
    }
    for (size_t i = 8; i < wedFile.polygons.size() && i < 13; ++i) {
        const auto &p = wedFile.polygons[i];
        Log(DEBUG, "WED", "[upscale V1.4] dst Poly[{}] sv={} cnt={} flags=0x{:02X} h={} bbox=({},{})-({},{}).",
            (unsigned)i, (unsigned)p.startVertex, (unsigned)p.vertexCount, (unsigned)p.flags, (int)p.height,
            (unsigned)p.minX, (unsigned)p.minY, (unsigned)p.maxX, (unsigned)p.maxY);
    }
    wedFile.vertices.clear();
    wedFile.vertices.reserve(wedFile13.vertices.size());
    for (const auto &v13 : wedFile13.vertices) {
        WEDVertexV14 v14{ static_cast<uint16_t>(v13.x * upscaleFactor), static_cast<uint16_t>(v13.y * upscaleFactor) };
        wedFile.vertices.push_back(v14);
    }

    // 7) PLT (polygon indices lookup): copy from V1.3
    // PLT entries are uint16 polygon indices (0..polygonCount-1), not vertex indices.
    wedFile.polygonIndices.clear();
    wedFile.polygonIndices = wedFile13.polygonIndices; // direct copy preserves wall group -> polygon mapping

    // Validate PLT contents are within polygon table bounds
    {
        size_t bad = 0;
        for (size_t j = 0; j < wedFile.polygonIndices.size(); ++j) {
            uint16_t idx = wedFile.polygonIndices[j];
            if (idx >= wedFile.polygons.size()) {
                ++bad;
                if (bad <= 5) {
                    Log(ERROR, "WED", "PLT[{}] = {} out of range (polygonCount={})", (unsigned)j, (unsigned)idx, (unsigned)wedFile.polygons.size());
                }
            }
        }
        if (bad == 0) {
            Log(DEBUG, "WED", "Copied PLT from V1.3: {} entries; all within [0, {}]", 
                (unsigned)wedFile.polygonIndices.size(), (unsigned)(wedFile.polygons.size() ? wedFile.polygons.size()-1 : 0));
        } else {
            Log(ERROR, "WED", "PLT copy has {} out-of-range entries (of {})", (unsigned)bad, (unsigned)wedFile.polygonIndices.size());
        }
    }

    // Store original V1.3 polygon bounds for door relocation during serialization
    wedFile.secHeader.polygonOffset = wedFile13.secHeader.polygonOffset;
    wedFile.secHeader.polygonIndexOffset = wedFile13.secHeader.polygonIndexOffset;

    // Save the upscaled WED file (V1.4)
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
    size_t expectedHeaderSize = sizeof(WEDHeaderV14);
    size_t expectedOverlaysSize = wedFile.overlays.size() * sizeof(WEDOverlayV14);
    size_t expectedSecHeaderSize = sizeof(WEDSecondaryHeaderV14);
    size_t expectedDoorsSize = wedFile.doors.size() * sizeof(WEDDoorV14);
    
    size_t expectedTilemapsSize = 0;
    for (const auto& overlay_tilemaps : wedFile.tilemaps) {
        expectedTilemapsSize += overlay_tilemaps.size() * sizeof(WEDTilemapV14);
    }
    
    size_t expectedDoorTilesSize = wedFile.doorTileCells.size() * sizeof(uint32_t);
    
    size_t expectedTileIndicesSize = 0;
    for (const auto& overlay_indices : wedFile.tileIndices) {
        expectedTileIndicesSize += overlay_indices.size() * sizeof(uint32_t);
    }
    
    size_t expectedWallGroupsSize = wedFile.wallGroups.size() * sizeof(WEDWallGroupV14);
    size_t expectedPolygonsSize = wedFile.polygons.size() * sizeof(WEDPolygonV14);
    size_t expectedPolygonIndicesSize = wedFile.polygonIndices.size() * sizeof(uint16_t);
    size_t expectedVerticesSize = wedFile.vertices.size() * sizeof(WEDVertexV14);
    
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
            {"upscale13", {
                "Upscale WED v1.3 (legacy layout) (e.g., wed upscale13 ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: wed upscale13 <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().upscaleResource(args[0], IE_WED_CLASS_ID, /*useLegacy*/ true) ? 0 : 1;
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
