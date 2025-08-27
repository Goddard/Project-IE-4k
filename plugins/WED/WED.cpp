
#include "WED.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <filesystem>

#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

// Auto-register the WED plugin
REGISTER_PLUGIN(WED, IE_WED_CLASS_ID);

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
        
        // Create expanded tilemap grid
        std::vector<WEDTilemap> newTilemaps;
        newTilemaps.reserve(origWidth * origHeight * upscaleFactor * upscaleFactor);
        
        for (size_t y = 0; y < origHeight; y++) {
            for (uint32_t dy = 0; dy < upscaleFactor; dy++) {
                for (size_t x = 0; x < origWidth; x++) {
                    size_t origIndex = y * origWidth + x;
                    if (origIndex < overlayTilemaps.size()) {
                        // Replicate each original tilemap entry factor times horizontally
                        for (uint32_t dx = 0; dx < upscaleFactor; dx++) {
                            WEDTilemap newTilemap = overlayTilemaps[origIndex];
                            
                            // Calculate the new tilemap position in the expanded grid
                            size_t newY = y * upscaleFactor + dy;
                            size_t newX = x * upscaleFactor + dx;
                            size_t newIndex = newY * (origWidth * upscaleFactor) + newX;
                            
                            // Update the tilemap to reference the correct sequential tile index
                            newTilemap.startIndex = static_cast<uint16_t>(newIndex);
                            newTilemap.tileCount = 1; // Each tilemap now references exactly one tile
                            
                            newTilemaps.push_back(newTilemap);
                        }
                    }
                }
            }
        }
        
        overlayTilemaps = std::move(newTilemaps);
        
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
    Log(DEBUG, "WED", "Updating door tile cells for expanded tilemap");
    
    // Door tile cells are indices into the tilemap grid
    // When we expand from WxH to (W*factor)x(H*factor), we need to map
    // original index to the corresponding block in the expanded grid
    if (!wedFile.overlays.empty()) {
        size_t origWidth = wedFile.overlays[0].width / upscaleFactor;  // Get original width
        size_t newWidth = wedFile.overlays[0].width;                   // Current (scaled) width
        
        for (auto& doorTileCell : wedFile.doorTileCells) {
            // Convert linear index to 2D coordinates in original grid
            size_t origY = doorTileCell / origWidth;
            size_t origX = doorTileCell % origWidth;
            
            // Map to top-left tile of the corresponding block in expanded grid
            size_t newY = origY * upscaleFactor;
            size_t newX = origX * upscaleFactor;
            
            // Convert back to linear index in expanded grid
            doorTileCell = static_cast<uint16_t>(newY * newWidth + newX);
        }
        
        Log(DEBUG, "WED", "Updated {} door tile cells", wedFile.doorTileCells.size());
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

} // namespace ProjectIE4k
