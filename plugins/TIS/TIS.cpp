#include "TIS.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
// #include <future>
#include <iostream>
#include <set>
#include <string>
// #include <thread>

#include "core/Logging/Logging.h"
// #include "core/OperationsMonitor/OperationsMonitor.h"
#include "core/SClassID.h"
#include "plugins/ColorReducer.h"
#include "plugins/CommandRegistry.h"
#include "plugins/PVRZ/PVRZ.h"
#include "plugins/WED/WEDV1.3.hpp"

namespace ProjectIE4k {

TIS::TIS(const std::string& resourceName_) 
    : PluginBase(resourceName_, IE_TIS_CLASS_ID), tilesPerRow(0), tilesPerColumn(0), isPvrzBased(false) {
    Log(DEBUG, "TIS", "TIS constructor called for resource: {}", resourceName_);
    
    // Detect format (V1 palette-based vs V2 PVRZ-based)
    if (!detectFormat()) {
        Log(ERROR, "TIS", "Failed to detect TIS format");
        return;
    }
    
    // Load WED file for tile information using our Resource Service
    // For night variants (e.g., AR1900N.TIS), use the base resource name (AR1900) for WED
    std::string baseResourceName = getBaseResourceName();
    std::vector<uint8_t> wedData = loadResourceFromService(baseResourceName, IE_WED_CLASS_ID);
    if (!wedData.empty()) {
        // Store the WED data directly
        originalWedFileData = wedData;
        Log(DEBUG, "TIS", "Successfully loaded WED resource: {} bytes (using base name: {})", wedData.size(), baseResourceName);
    } else {
        Log(WARNING, "TIS", "No WED resource found for base name '{}' (original: '{}')", baseResourceName, resourceName_);
        // TODO : previously had return; here, not sure if some TIS have no WED file in game check it!
    }
    
    // Mark plugin as valid since we successfully loaded the TIS resource
    valid_ = true;
}

TIS::~TIS() {
}

bool TIS::extract() {
    Log(DEBUG, "TIS", "Starting TIS extraction for resource: {}", resourceName_);
    
    // Parse WED file to get additional PVRZ resources and grid information
    if (!parseWEDFile()) {
        Log(WARNING, "TIS", "Failed to parse WED file, using calculated grid");
        calculateGridFromTileCount();
    }
    
    // First extract the main TIS to PNG
    if (!convertTisToPng()) {
        Log(ERROR, "TIS", "Failed to convert TIS to PNG");
        return false;
    }
    
    if(isPvrzBased) {
        // Then extract additional PVRZ files referenced in WED
        if (!extractAdditionalPVRZFiles()) {
            Log(WARNING, "TIS", "Failed to extract additional PVRZ files from WED");
        }
    }
    
    return true;
}

bool TIS::assemble() {
    Log(DEBUG, "TIS", "Starting TIS assembly for resource: {}", resourceName_);
    
    // Choose conversion method based on original format
    if (isPvrzBased) {
        return convertPngToTis(); // PVRZ-based conversion
    } else {
        return convertPngToTisV1Palette(); // Palette-based conversion
    }
}

bool TIS::parseWEDFile() {
    Log(DEBUG, "TIS", "Parsing WED file for resource: {}", resourceName_);

    if (originalWedFileData.empty()) {
        Log(WARNING, "TIS", "No WED resource found for tile map creation");
        return false;
    }
    
    // Parse WED file using the WEDV1.3 structures
    WEDFile wedFile;
    if (!wedFile.deserialize(originalWedFileData)) {
        Log(ERROR, "TIS", "Failed to deserialize WED file");
        return false;
    }
    
    if (!wedFile.isValid()) {
        Log(ERROR, "TIS", "Invalid WED file structure");
        return false;
    }
    
    Log(DEBUG, "TIS", "Parsing {} WED overlays for tile map creation", wedFile.header.overlayCount);
    
    // Parse primary overlay (overlay 0) to create tile map like Near Infinity
    if (wedFile.header.overlayCount > 0 && !wedFile.overlays.empty()) {
        const auto& primaryOverlay = wedFile.overlays[0];
        
        // Extract overlay information from the structure
        uint16_t gridWidth = primaryOverlay.width;
        uint16_t gridHeight = primaryOverlay.height;
        std::string tilesetName = primaryOverlay.getTilesetName();
        uint32_t tilemapOffset = primaryOverlay.tilemapOffset;
        uint32_t tileIndexOffset = primaryOverlay.tileIndexOffset;
        
        Log(DEBUG, "TIS", "Primary overlay: {}x{} tiles, tileset '{}'", 
            gridWidth, gridHeight, tilesetName);
        
        // Store the WED grid dimensions for tile layout
        tilesPerRow = gridWidth;
        tilesPerColumn = gridHeight;
        
        // Verify this overlay references the same tileset
        // Clean up tileset name by removing null padding and trimming whitespace
        std::string tilesetNameStr = tilesetName;
        size_t nullPos = tilesetNameStr.find('\0');
        if (nullPos != std::string::npos) {
            tilesetNameStr = tilesetNameStr.substr(0, nullPos);
        }
        // Trim whitespace
        tilesetNameStr.erase(0, tilesetNameStr.find_first_not_of(" \t\r\n"));
        tilesetNameStr.erase(tilesetNameStr.find_last_not_of(" \t\r\n") + 1);
        
        std::string resourceName_Upper = resourceName_;
        std::transform(tilesetNameStr.begin(), tilesetNameStr.end(), tilesetNameStr.begin(), ::toupper);
        std::transform(resourceName_Upper.begin(), resourceName_Upper.end(), resourceName_Upper.begin(), ::toupper);
        
        // For night variants, compare against the base resource name
        std::string baseResourceName = getBaseResourceName();
        std::string baseResourceName_Upper = baseResourceName;
        std::transform(baseResourceName_Upper.begin(), baseResourceName_Upper.end(), baseResourceName_Upper.begin(), ::toupper);
        
        if (tilesetNameStr != baseResourceName_Upper) {
            Log(WARNING, "TIS", "Primary overlay tileset '{}' (cleaned: '{}') doesn't match base resource '{}'", 
                tilesetName, tilesetNameStr, baseResourceName);
            return false;
        }
        
        // Get tile indices from the WED file structure
        std::vector<uint16_t> tileIndices;
        if (!wedFile.tileIndices.empty()) {
            tileIndices = wedFile.tileIndices[0]; // Primary overlay tile indices
        }
        
        Log(DEBUG, "TIS", "Tile index lookup table has {} entries", tileIndices.size());
        
        // Read tilemap structures and collect all tile indices like Near Infinity
        uint32_t totalTiles = gridWidth * gridHeight;
        
        std::set<uint16_t> allTileIndices; // Collect all unique tile indices
        
        // Get tilemaps from the WED file structure
        if (!wedFile.tilemaps.empty()) {
            const auto& overlayTilemaps = wedFile.tilemaps[0]; // Primary overlay tilemaps
            
            for (const auto& tilemap : overlayTilemaps) {
            
                // Collect primary tile indices
                if (!tileIndices.empty() && tilemap.startIndex != 0xFFFF && tilemap.tileCount > 0) {
                    for (uint16_t i = 0; i < tilemap.tileCount && (tilemap.startIndex + i) < tileIndices.size(); i++) {
                        uint16_t actualTileIndex = tileIndices[tilemap.startIndex + i];
                        allTileIndices.insert(actualTileIndex);
                    }
                }
                
                // Collect secondary tile index (direct index like Near Infinity)
                if (tilemap.secondaryIndex != 0xFFFF) {
                    allTileIndices.insert(tilemap.secondaryIndex);
                }
            }
        }
        
        Log(DEBUG, "TIS", "Collected {} unique tile indices from WED overlay", allTileIndices.size());
        
        // Find the highest tile index to determine if we need additional PVRZ files
        uint16_t maxTileIndex = 0;
        for (uint16_t idx : allTileIndices) {
            if (idx > maxTileIndex) {
                maxTileIndex = idx;
            }
        }
        
        Log(DEBUG, "TIS", "Highest tile index referenced: {} (main TIS has {} tiles)", 
            maxTileIndex, tisV2File.tiles.size());
        
        // If we have tile indices beyond the main TIS, we need additional PVRZ files
        if (maxTileIndex >= tisV2File.tiles.size()) {
            Log(DEBUG, "TIS", "Found tile indices beyond main TIS - need additional PVRZ files");
            return extractAdditionalPVRZFiles();
        } else {
            Log(DEBUG, "TIS", "All tile indices are within main TIS range");
        }
    }
    
    return true;
}

bool TIS::extractAdditionalPVRZFiles() {
    Log(DEBUG, "TIS", "Extracting additional PVRZ files for tiles beyond main TIS");
    
    // Find the highest tile index referenced in WED
    uint16_t maxTileIndex = 0;
    
    if (originalWedFileData.empty()) {
        Log(WARNING, "TIS", "No WED resource found for additional tile extraction");
        return false;
    }
    
    // Parse WED file using the WEDV1.3 structures
    ProjectIE4k::WEDFile wedFile;
    if (!wedFile.deserialize(originalWedFileData)) {
        Log(ERROR, "TIS", "Failed to deserialize WED file for additional tile extraction");
        return false;
    }
    
    if (wedFile.header.overlayCount > 0 && !wedFile.overlays.empty()) {
        const auto& primaryOverlay = wedFile.overlays[0];
        
        // Extract overlay information from the structure
        uint16_t gridWidth = primaryOverlay.width;
        uint16_t gridHeight = primaryOverlay.height;
        std::string tilesetName = primaryOverlay.getTilesetName();
        
        // Get tile indices from the WED file structure
        std::vector<uint16_t> tileIndices;
        if (!wedFile.tileIndices.empty()) {
            tileIndices = wedFile.tileIndices[0]; // Primary overlay tile indices
        }
        
        // Read tilemap structures to find highest tile index
        uint32_t totalTiles = gridWidth * gridHeight;
        
        // Get tilemaps from the WED file structure
        if (!wedFile.tilemaps.empty()) {
            const auto& overlayTilemaps = wedFile.tilemaps[0]; // Primary overlay tilemaps
            
            for (const auto& tilemap : overlayTilemaps) {
            
                // Check primary tiles
                if (!tileIndices.empty() && tilemap.startIndex != 0xFFFF && tilemap.tileCount > 0) {
                    for (uint16_t i = 0; i < tilemap.tileCount && (tilemap.startIndex + i) < tileIndices.size(); i++) {
                        uint16_t actualTileIndex = tileIndices[tilemap.startIndex + i];
                        if (actualTileIndex > maxTileIndex) {
                            maxTileIndex = actualTileIndex;
                        }
                    }
                }
                
                // Check secondary tile
                if (tilemap.secondaryIndex != 0xFFFF && tilemap.secondaryIndex > maxTileIndex) {
                    maxTileIndex = tilemap.secondaryIndex;
                }
            }
        }
    }
    
    Log(DEBUG, "TIS", "Highest tile index referenced: {} (main TIS has {} tiles)", 
        maxTileIndex, tisV2File.tiles.size());
    
    if (maxTileIndex < tisV2File.tiles.size()) {
        Log(DEBUG, "TIS", "No additional tiles needed");
        return true;
    }
    
    if(saveSeconaryAsPng) {
        extractSeconaryTilesAsPngs(tisV2File, maxTileIndex);
    }
    
    return true;
}

void TIS::extractSeconaryTilesAsPngs(TISV2File &tisV2File, uint16_t &maxTileIndex) {
      // Calculate how many additional PVRZ files we need
    // Assuming 64 tiles per PVRZ file (8x8 grid)
    uint32_t tilesPerPvrz = 64;
    uint32_t mainTisTiles = tisV2File.tiles.size();
    uint32_t additionalTilesNeeded = maxTileIndex - mainTisTiles + 1;
    uint32_t additionalPvrzFiles = (additionalTilesNeeded + tilesPerPvrz - 1) / tilesPerPvrz;
    
    Log(DEBUG, "TIS", "Need {} additional PVRZ files for {} additional tiles", 
        additionalPvrzFiles, additionalTilesNeeded);
    
    // Find the highest PVRZ page in main TIS to determine starting page for additional files
    uint32_t highestMainPage = 0;
    for (const auto& tile : tisV2File.tiles) {
        if (tile.page != 0xFFFFFFFF && tile.page > highestMainPage) {
            highestMainPage = tile.page;
        }
    }
    
    uint32_t startPage = highestMainPage + 1;
    Log(DEBUG, "TIS", "Additional PVRZ files start from page {}", startPage);
    
    // Create output directory for additional tiles
    std::string outputDir = getExtractDir();
    
    // Load and extract tiles from additional PVRZ files
    for (uint32_t pageOffset = 0; pageOffset < additionalPvrzFiles; pageOffset++) {
        uint32_t page = startPage + pageOffset;
        std::string pvrzResourceName = PluginManager::getInstance().generatePVRZNameInternal(resourceName_, page, IE_TIS_CLASS_ID);
        
        Log(DEBUG, "TIS", "Loading additional PVRZ page {}: {}", page, pvrzResourceName);
        
        // Load PVRZ data using our Resource Service
        std::vector<uint8_t> pvrzData = loadResourceFromService(pvrzResourceName, IE_PVRZ_CLASS_ID);
        
        if (!pvrzData.empty()) {
            
            // Extract tiles from this PVRZ file
            uint32_t tilesInThisPage = std::min(tilesPerPvrz, additionalTilesNeeded - pageOffset * tilesPerPvrz);
            
            for (uint32_t tileInPage = 0; tileInPage < tilesInThisPage; tileInPage++) {
                uint32_t tileIndex = mainTisTiles + pageOffset * tilesPerPvrz + tileInPage;
                
                // Calculate tile position in PVRZ (8x8 grid)
                uint32_t tileX = (tileInPage % 8) * 64;
                uint32_t tileY = (tileInPage / 8) * 64;
                
                // Extract tile pixels from PVRZ data
                std::vector<uint32_t> tilePixels = extractTilePixels(tileX, tileY, 64, 64);
                
                if (!tilePixels.empty()) {
                    // Save the tile as a PNG file
                    std::string tileFilename = outputDir + "/tile_" + std::to_string(tileIndex) + "_page" + std::to_string(page) + ".png";
                    if (savePNG(tileFilename, tilePixels, 64, 64)) {
                        Log(DEBUG, "TIS", "Saved additional tile {} to {}", tileIndex, tileFilename);
                    } else {
                        Log(WARNING, "TIS", "Failed to save additional tile {} to {}", tileIndex, tileFilename);
                    }
                } else {
                    Log(WARNING, "TIS", "Failed to extract pixels for additional tile {} from page {}", tileIndex, page);
                }
            }
        } else {
            Log(WARNING, "TIS", "Failed to load additional PVRZ page {}: {}", page, pvrzResourceName);
        }
    }
}

bool TIS::detectFormat() {
    // Check minimum size for header
    if (originalFileData.size() < sizeof(TISHeader)) {
        Log(ERROR, "TIS", "TIS data too small for header");
        return false;
    }
    
    // Read header to get format information
    TISHeader header;
    memcpy(&header, originalFileData.data(), sizeof(TISHeader));
    
    // Debug: Print raw header data
    Log(DEBUG, "TIS", "Raw header data:");
    Log(DEBUG, "TIS", "  Signature: {:.4s}", header.signature);
    
    // Handle version string more carefully to avoid garbage characters
    std::string versionStr(header.version, 4);
    // Trim any null bytes or garbage
    versionStr.erase(std::remove(versionStr.begin(), versionStr.end(), '\0'), versionStr.end());
    Log(DEBUG, "TIS", "  Version: '{}'", versionStr);
    Log(DEBUG, "TIS", "  Tile count: {}", header.tileCount);
    Log(DEBUG, "TIS", "  Tile size: {} bytes", header.tileSize);
    Log(DEBUG, "TIS", "  Header size: {} bytes", header.headerSize);
    Log(DEBUG, "TIS", "  Tile dimension: {} pixels", header.tileDimension);
    
    // Calculate expected file size based on header
    uint64_t expectedSize = header.headerSize + (uint64_t)header.tileCount * header.tileSize;
    Log(DEBUG, "TIS", "Expected file size: {} bytes, Actual file size: {} bytes", expectedSize, originalFileData.size());
    
    Log(DEBUG, "TIS", "Tile count: {}, Tile size: {} bytes", header.tileCount, header.tileSize);
    
    if (header.isPvrzBased()) {
        Log(DEBUG, "TIS", "Detected TIS V2 (PVRZ-based) format");
        isPvrzBased = true;

        std::string pvrzFileName = PluginManager::getInstance().generatePVRZNameInternal(resourceName_, 0, IE_TIS_CLASS_ID);
        // For PVRZ files, we need to get the extension from the global manager since it's a different resource type
        pvrzOriginalExtension = SClass::getExtensionWithDot(IE_PVRZ_CLASS_ID);
        
        // Deserialize as V2 file
        if (!tisV2File.deserialize(originalFileData)) {
            Log(ERROR, "TIS", "Failed to deserialize TIS V2 data");
            return false;
        }
        
        Log(DEBUG, "TIS", "TIS V2 file: {} tiles", tisV2File.header.tileCount);
        return true;
    } else if (header.isPaletteBased()) {
        Log(DEBUG, "TIS", "Detected TIS V1 (palette-based) format");
        isPvrzBased = false;
        
        // Deserialize as V1 file
        if (!tisV1File.deserialize(originalFileData)) {
            Log(ERROR, "TIS", "Failed to deserialize TIS V1 data");
            return false;
        }
        
        Log(DEBUG, "TIS", "TIS V1 file: {} tiles", tisV1File.header.tileCount);
        return true;
    } else {
        Log(ERROR, "TIS", "Unknown TIS format: tile size {} bytes", header.tileSize);
        return false;
    }
}

bool TIS::convertTisToPng() {
    Log(DEBUG, "TIS", "Converting TIS to PNG format");
    
    // Get the output PNG file path
    std::string outputFile = getExtractDir() + "/" + resourceName_ + ".png";
    
    if (originalFileData.empty()) {
        Log(ERROR, "TIS", "No file data available");
        return false;
    }
    
    // Use WED grid dimensions if available, otherwise use calculated
    if (tilesPerRow == 0 || tilesPerColumn == 0) {
        Log(WARNING, "TIS", "No WED grid info, calculating from tile count");
        calculateGridFromTileCount();
    } else {
        Log(DEBUG, "TIS", "Using WED grid dimensions: {}x{}", tilesPerRow, tilesPerColumn);
    }
    
    Log(DEBUG, "TIS", "Grid dimensions: {}x{} tiles", tilesPerRow, tilesPerColumn);
    
    // Create stitched image - use WED grid dimensions as foundation
    uint32_t totalTiles = isPvrzBased ? tisV2File.header.tileCount : tisV1File.header.tileCount;
    
    // Use WED grid dimensions as the primary grid, then extend to accommodate secondary tiles
    int primaryTiles = tilesPerRow * tilesPerColumn; // 54 * 42 = 2268
    int secondaryTiles = totalTiles - primaryTiles;  // 2370 - 2268 = 102
    
    // Calculate how many additional rows we need for secondary tiles
    int additionalRows = (secondaryTiles + tilesPerRow - 1) / tilesPerRow; // 102 / 54 = 2
    
    int totalTilesPerRow = tilesPerRow;  // Keep the WED width (54)
    int totalTilesPerColumn = tilesPerColumn + additionalRows; // 42 + 2 = 44
    
    int imageWidth = totalTilesPerRow * 64;
    int imageHeight = totalTilesPerColumn * 64;
    std::vector<uint32_t> pixels(imageWidth * imageHeight, 0);
    
    Log(DEBUG, "TIS", "Primary grid (from WED): {}x{} = {} tiles", tilesPerRow, tilesPerColumn, primaryTiles);
    Log(DEBUG, "TIS", "Secondary tiles: {} (need {} additional rows)", secondaryTiles, additionalRows);
    Log(DEBUG, "TIS", "Total image grid: {}x{} = {} tiles", totalTilesPerRow, totalTilesPerColumn, totalTilesPerRow * totalTilesPerColumn);
    
    if (isPvrzBased) {
        // Handle TIS V2 (PVRZ-based)
        Log(DEBUG, "TIS", "Processing TIS V2 (PVRZ-based) tiles");
        
        // Create PVRZ instance for loading textures
        PVRZ pvrzLoader;
        
        // Track which PVRZ pages we've loaded to avoid reloading
        std::map<uint32_t, std::vector<uint8_t>> loadedPages;
        std::map<uint32_t, int> pageWidths;
        std::map<uint32_t, int> pageHeights;
        
        // Process each tile based on PVRZ coordinates
        for (uint32_t tileIndex = 0; tileIndex < tisV2File.header.tileCount; tileIndex++) {
            int tileX = tileIndex % totalTilesPerRow;
            int tileY = tileIndex / totalTilesPerRow;
            
            if (tileIndex < tisV2File.tiles.size()) {
                const TISV2Tile& tile = tisV2File.tiles[tileIndex];
                
                // Check if this is a solid black tile (page = -1)
                if (tile.page == 0xFFFFFFFF) {
                    // Solid black tile
                    for (int y = 0; y < 64; y++) {
                        for (int x = 0; x < 64; x++) {
                            int pixelX = tileX * 64 + x;
                            int pixelY = tileY * 64 + y;
                            if (pixelX < imageWidth && pixelY < imageHeight) {
                                pixels[pixelY * imageWidth + pixelX] = 0xFF000000; // Black
                            }
                        }
                    }
                } else {
                    // PVRZ-based tile - load actual texture data
                    // Use PluginManager to generate the correct PVRZ resource name
                    std::string pvrzResourceName = PluginManager::getInstance().generatePVRZNameInternal(resourceName_, 0, IE_TIS_CLASS_ID);
                    
                    // Load PVRZ page if not already loaded
                    if (loadedPages.find(tile.page) == loadedPages.end()) {
                        std::vector<uint8_t> argbData;
                        int width, height;
                        
                        if (pvrzLoader.loadPVRZResourceAsARGB(pvrzResourceName, argbData, width, height)) {
                            loadedPages[tile.page] = argbData;
                            pageWidths[tile.page] = width;
                            pageHeights[tile.page] = height;
                            Log(DEBUG, "TIS", "Loaded PVRZ page {}: {}x{}", tile.page, width, height);
                        } else {
                            Log(ERROR, "TIS", "Failed to load PVRZ page {} ({})", tile.page, pvrzResourceName);
                            // Fall back to visualization for failed loads
                            loadedPages[tile.page] = std::vector<uint8_t>();
                        }
                    }
                    
                    // Extract tile from PVRZ texture
                    if (!loadedPages[tile.page].empty()) {
                        const std::vector<uint8_t>& argbData = loadedPages[tile.page];
                        int pageWidth = pageWidths[tile.page];
                        int pageHeight = pageHeights[tile.page];
                        
                        // Extract 64x64 tile region from the PVRZ texture
                        for (int y = 0; y < 64; y++) {
                            for (int x = 0; x < 64; x++) {
                                int srcX = tile.x + x;
                                int srcY = tile.y + y;
                                
                                // Check bounds
                                if (srcX < pageWidth && srcY < pageHeight) {
                                    int srcIndex = (srcY * pageWidth + srcX) * 4;
                                    if (srcIndex + 3 < static_cast<int>(argbData.size())) {
                                        // Read ARGB data and convert to RGBA
                                        uint8_t a = argbData[srcIndex + 0];
                                        uint8_t r = argbData[srcIndex + 1];
                                        uint8_t g = argbData[srcIndex + 2];
                                        uint8_t b = argbData[srcIndex + 3];
                                        
                                        int pixelX = tileX * 64 + x;
                                        int pixelY = tileY * 64 + y;
                                        if (pixelX < imageWidth && pixelY < imageHeight) {
                                            pixels[pixelY * imageWidth + pixelX] = (a << 24) | (r << 16) | (g << 8) | b;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        // Fallback visualization for failed loads
                        for (int y = 0; y < 64; y++) {
                            for (int x = 0; x < 64; x++) {
                                int pixelX = tileX * 64 + x;
                                int pixelY = tileY * 64 + y;
                                if (pixelX < imageWidth && pixelY < imageHeight) {
                                    // Create error visualization
                                    uint8_t r = (tile.page * 20) % 256;
                                    uint8_t g = (tile.x / 64) % 256;
                                    uint8_t b = (tile.y / 64) % 256;
                                    
                                    if ((x + y) % 16 < 8) {
                                        r = (r + 128) % 256;
                                        g = (g + 128) % 256;
                                        b = (b + 128) % 256;
                                    }
                                    
                                    pixels[pixelY * imageWidth + pixelX] = 0xFF000000 | (r << 16) | (g << 8) | b;
                                }
                            }
                        }
                    }
                    
                    // Log first few tiles for debugging
                    if (tileIndex < 5) {
                        Log(DEBUG, "TIS", "Tile {}: Page={}, X={}, Y={}", tileIndex, tile.page, tile.x, tile.y);
                    }
                }
            }
        }
    } else {
        // Handle TIS V1 (palette-based)
        Log(DEBUG, "TIS", "Processing TIS V1 (palette-based) tiles");
        
        // Process each tile
        for (uint32_t tileIndex = 0; tileIndex < tisV1File.header.tileCount; tileIndex++) {
            int tileX = tileIndex % totalTilesPerRow;
            int tileY = tileIndex / totalTilesPerRow;
            
            if (tileIndex < tisV1File.tiles.size()) {
                const TISV1Tile& tile = tisV1File.tiles[tileIndex];
                
                // Copy tile pixels to stitched image
                for (int y = 0; y < 64; y++) {
                    for (int x = 0; x < 64; x++) {
                        uint8_t paletteIndex = tile.getPixel(x, y);
                        
                        // Handle transparency: use magic green detection like Near Infinity
                        if (paletteIndex == 0) {
                            // Check if this is the magic green transparent color
                            if (ColorReducer::isMagicGreenBGRA(tile.palette[0])) {
                                // Transparent pixel (magic green)
                                if (tileIndex < 3) { // Only log for first few tiles to avoid spam
                                    Log(DEBUG, "TIS", "Found magic green transparent pixel at tile {}, pos ({},{})", 
                                        tileIndex, x, y);
                                }
                                int pixelX = tileX * 64 + x;
                                int pixelY = tileY * 64 + y;
                                
                                if (pixelX < imageWidth && pixelY < imageHeight) {
                                    pixels[pixelY * imageWidth + pixelX] = 0x00000000; // Fully transparent
                                }
                            } else {
                                // Opaque pixel with color from palette index 0
                                if (tileIndex < 3) { // Only log for first few tiles to avoid spam
                                    uint8_t b = tile.palette[0][0];
                                    uint8_t g = tile.palette[0][1];
                                    uint8_t r = tile.palette[0][2];
                                    uint8_t a = tile.palette[0][3];
                                    Log(DEBUG, "TIS", "Found non-magic-green palette index 0 at tile {}, pos ({},{}): B={}, G={}, R={}, A={}", 
                                        tileIndex, x, y, b, g, r, a);
                                }
                                uint32_t color = tile.getColor(paletteIndex);
                                int pixelX = tileX * 64 + x;
                                int pixelY = tileY * 64 + y;
                                if (pixelX < imageWidth && pixelY < imageHeight) {
                                    pixels[pixelY * imageWidth + pixelX] = color;
                                }
                            }
                        } else {
                            // Opaque pixel - get color from palette
                            uint32_t color = tile.getColor(paletteIndex);
                            int pixelX = tileX * 64 + x;
                            int pixelY = tileY * 64 + y;
                            if (pixelX < imageWidth && pixelY < imageHeight) {
                                pixels[pixelY * imageWidth + pixelX] = color;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Get output directory (automatically created by PluginManager)
    std::string outputDir = getExtractDir();
    
    // Save as PNG
    if (!savePNG(outputFile, pixels, imageWidth, imageHeight)) {
        Log(ERROR, "TIS", "Failed to save PNG file: {}", outputFile);
        return false;
    }
    
    Log(DEBUG, "TIS", "Successfully created PNG file: {}", outputFile);
    return true;
}

bool TIS::convertPngToTis() {
    Log(DEBUG, "TIS", "Converting PNG to TIS V1 (PVRZ-based) format");
    
    // Get the input PNG file path (from upscaled directory)
    std::string inputFile = getUpscaledDir() + "/" + resourceName_ + ".png";
    std::string outputFile = getAssembleDir() + "/" + resourceName_ + originalExtension;
    
    // Load PNG image
    std::vector<uint32_t> pixels;
    int imageWidth, imageHeight;
    
    if (!loadPNG(inputFile, pixels, imageWidth, imageHeight)) {
        Log(ERROR, "TIS", "Failed to load PNG file: {}", inputFile);
        return false;
    }
    
    Log(DEBUG, "TIS", "Loaded image: {}x{}", imageWidth, imageHeight);
    
    // Calculate tile grid from image
    int totalTilesPerRow = imageWidth / 64;
    int totalTilesPerColumn = imageHeight / 64;
    int totalTileCount = totalTilesPerRow * totalTilesPerColumn;
    
    Log(DEBUG, "TIS", "Image grid dimensions: {}x{} tiles ({} total)", totalTilesPerRow, totalTilesPerColumn, totalTileCount);
    
    // Parse WED file to get primary grid dimensions
    if (!parseWEDFile()) {
        Log(WARNING, "TIS", "Failed to parse WED file, using single row layout");
        // Default to single row layout if WED parsing fails
        tilesPerRow = totalTileCount;
        tilesPerColumn = 1;
    }
    
    Log(DEBUG, "TIS", "Primary grid (from WED): {}x{} = {} tiles", tilesPerRow, tilesPerColumn, tilesPerRow * tilesPerColumn);
    
    // Calculate secondary tiles
    int primaryTileCount = tilesPerRow * tilesPerColumn;
    int secondaryTileCount = totalTileCount - primaryTileCount;
    
    Log(DEBUG, "TIS", "Secondary tiles: {}", secondaryTileCount);
    
    // Create PVRZ texture atlases
    PVRZ pvrz;
    std::vector<std::string> pvrzFiles;
    std::map<int, std::pair<uint32_t, std::pair<int, int>>> tileToPageMapping; // tileIndex -> (page, (x, y))
    
    // Calculate optimal PVRZ atlas size based on tile count
    int tilesPerPvrz, tilesPerPvrzRow, tilesPerPvrzColumn, atlasWidth, atlasHeight;
    
    if (totalTileCount <= 64) {
        // Small tile count: use 512x512 atlas (8x8 grid, 64 tiles)
        tilesPerPvrz = 64;
        tilesPerPvrzRow = 8;
        tilesPerPvrzColumn = 8;
        atlasWidth = 512;
        atlasHeight = 512;
    } else if (totalTileCount <= 128) {
        // Medium tile count: use 512x1024 atlas (8x16 grid, 128 tiles)
        tilesPerPvrz = 128;
        tilesPerPvrzRow = 8;
        tilesPerPvrzColumn = 16;
        atlasWidth = 512;
        atlasHeight = 1024;
    } else {
        // Large tile count: use 1024x1024 atlas (16x16 grid, 256 tiles)
        // This matches the original TIS structure where most pages are 1024x1024
        tilesPerPvrz = 256;
        tilesPerPvrzRow = 16;
        tilesPerPvrzColumn = 16;
        atlasWidth = 1024;
        atlasHeight = 1024;
    }
    
    Log(DEBUG, "TIS", "Using {}x{} PVRZ atlases ({} tiles per atlas, {}x{} grid)", 
        atlasWidth, atlasHeight, tilesPerPvrz, tilesPerPvrzRow, tilesPerPvrzColumn);
    
    for (int pageIndex = 0; pageIndex < (totalTileCount + tilesPerPvrz - 1) / tilesPerPvrz; pageIndex++) {
        std::vector<std::vector<uint32_t>> tilePixels; // In-memory tile pixel buffers
        std::vector<std::pair<int, int>> tilePositions;
        
        // Extract tiles for this PVRZ page
        for (int tileInPage = 0; tileInPage < tilesPerPvrz; tileInPage++) {
            int tileIndex = pageIndex * tilesPerPvrz + tileInPage;
            
            if (tileIndex >= totalTileCount) {
                break;
            }
            
            // Calculate tile position in the input image
            int tileX = tileIndex % totalTilesPerRow;
            int tileY = tileIndex / totalTilesPerRow;
            
            // Extract tile pixels from the loaded image
            std::vector<uint32_t> tileBuf;
            tileBuf.reserve(64 * 64);
            for (int y = 0; y < 64; y++) {
                for (int x = 0; x < 64; x++) {
                    int srcX = tileX * 64 + x;
                    int srcY = tileY * 64 + y;
                    if (srcX < imageWidth && srcY < imageHeight) {
                        tileBuf.push_back(pixels[srcY * imageWidth + srcX]);
                    } else {
                        tileBuf.push_back(0); // Transparent
                    }
                }
            }
            
            // Calculate position in PVRZ using the correct grid dimensions
            int pvrzX = (tileInPage % tilesPerPvrzRow) * 64;
            int pvrzY = (tileInPage / tilesPerPvrzRow) * 64;
            tilePositions.push_back({static_cast<unsigned int>(pvrzX), static_cast<unsigned int>(pvrzY)});
            tilePixels.push_back(std::move(tileBuf));
            
            // Store mapping
            tileToPageMapping[tileIndex] = {static_cast<uint32_t>(pageIndex), {static_cast<uint32_t>(pvrzX), static_cast<uint32_t>(pvrzY)}};
        }
        
        if (!tilePixels.empty()) {
            // Create PVRZ file directly from pixel buffers
            auto [pvrzFileName, pageNum] = PluginManager::getInstance().generatePVRZName(resourceName_, IE_TIS_CLASS_ID);
            std::string pvrzFilePath = getAssembleDir() + "/" + pvrzFileName + pvrzOriginalExtension;
            if (pvrz.createTextureAtlasPVRZFromPixels(tilePixels, tilePositions, pvrzFilePath, atlasWidth, atlasHeight, PVRZFormat::DXT5)) {
                pvrzFiles.push_back(pvrzFilePath);
                Log(DEBUG, "TIS", "Created PVRZ page {}: {}", pageIndex, pvrzFilePath);
            } else {
                Log(ERROR, "TIS", "Failed to create PVRZ page {}", pageIndex);
                return false;
            }
        }
    }
    
    // Create TIS V2 file with PVRZ-based tiles
    TISV2File tisFile;
    tisFile.header.setTileCount(totalTileCount); // Use total tile count for proper upscaled file
    tisFile.header.tileSize = 12; // PVRZ-based tiles are 12 bytes
    tisFile.tiles.resize(totalTileCount); // Keep all tiles in the data structure
    
    // Set up tile entries for PVRZ-based format
    for (int tileIndex = 0; tileIndex < totalTileCount; tileIndex++) {
        auto it = tileToPageMapping.find(tileIndex);
        if (it != tileToPageMapping.end()) {
            // For PVRZ-based tiles, we use TISV2Tile structure
            // The tile data contains: page (4 bytes), x (4 bytes), y (4 bytes)
            TISV2Tile& tile = tisFile.tiles[tileIndex];
            
            // Store PVRZ info in the tile data
            tile.page = it->second.first;
            tile.x = it->second.second.first;
            tile.y = it->second.second.second;
            
            Log(DEBUG, "TIS", "Tile {}: Page={}, X={}, Y={}", tileIndex, tile.page, tile.x, tile.y);
        } else {
            Log(ERROR, "TIS", "No mapping found for tile {}", tileIndex);
            return false;
        }
    }
    
    // Serialize to binary data
    originalTisFileData = tisFile.serialize();
    
    // Get output directory
    std::string outputDir = getAssembleDir();
    
    // Write TIS file
    std::ofstream outFile(outputFile, std::ios::binary);
    if (!outFile) {
        Log(ERROR, "TIS", "Cannot write to {}", outputFile);
        return false;
    }
    
    outFile.write(reinterpret_cast<const char*>(originalTisFileData.data()), originalTisFileData.size());
    outFile.close();
    
    Log(DEBUG, "TIS", "Successfully created TIS V1 (PVRZ-based) file: {}", outputFile);
    Log(DEBUG, "TIS", "Created {} PVRZ files", pvrzFiles.size());
    
    return true;
}

std::vector<uint32_t> TIS::extractTilePixels(int tileX, int tileY, int tileWidth, int tileHeight) {
    std::vector<uint32_t> tilePixels;
    tilePixels.reserve(tileWidth * tileHeight);
    
    // Load the stitched image to get pixel data
    std::string stitchedFile = getExtractDir() + "/" + resourceName_ + ".png";
    std::vector<uint32_t> pixels;
    int imageWidth, imageHeight;
    
    if (!loadPNG(stitchedFile, pixels, imageWidth, imageHeight)) {
        Log(ERROR, "TIS", "Failed to load stitched image for tile extraction");
        return tilePixels;
    }
    
    // Extract tile region
    for (int y = 0; y < tileHeight; y++) {
        for (int x = 0; x < tileWidth; x++) {
            int srcX = tileX + x;
            int srcY = tileY + y;
            
            if (srcX < imageWidth && srcY < imageHeight) {
                tilePixels.push_back(pixels[srcY * imageWidth + srcX]);
            } else {
                tilePixels.push_back(0); // Transparent
            }
        }
    }
    
    return tilePixels;
}

bool TIS::createTile(const std::vector<uint32_t>& tilePixels, TISV1Tile& tile) {
    // Use ColorReducer to create a palette with magic green reserved for transparency
    std::vector<uint32_t> palette;
    
    if (!ColorReducer::createPaletteWithMagicGreen(tilePixels, 256, palette)) {
        Log(ERROR, "TIS", "Failed to create palette with magic green");
        return false;
    }
    
    // Copy colors to tile palette (in BGRA order as per TIS spec)
    for (size_t i = 0; i < palette.size() && i < 256; i++) {
        ColorReducer::argbToBGRA(palette[i], tile.palette[i]);
    }
    
    // Convert pixels to palette indices with magic green transparency handling
    std::vector<uint8_t> indices;
    if (!ColorReducer::pixelsToIndicesWithMagicGreen(tilePixels, palette, indices)) {
        Log(ERROR, "TIS", "Failed to convert pixels to indices");
        return false;
    }
    
    // Copy indices to tile data
    for (size_t i = 0; i < indices.size() && i < 64 * 64; i++) {
        int x = i % 64;
        int y = i / 64;
        tile.setPixel(x, y, indices[i]);
    }
    
    return true;
}

bool TIS::processTileBatch(int batchStart, int batchEnd, int totalTilesPerRow,
                           const std::vector<uint32_t> &pixels, int imageWidth,
                           TISV1File &tisFile) {
  // Compute image height once
  const int imageHeight = static_cast<int>(pixels.size() / static_cast<size_t>(imageWidth));
  const bool exactTiling = (imageWidth % 64 == 0) && (imageHeight % 64 == 0);

  // Reusable buffer for one 64x64 tile worth of pixels
  std::vector<uint32_t> tilePixels(64 * 64);

  // Process a batch of tiles in parallel
  for (int tileIndex = batchStart; tileIndex < batchEnd; tileIndex++) {
    // Calculate tile position in the input image
    int tileX = tileIndex % totalTilesPerRow;
    int tileY = tileIndex / totalTilesPerRow;

    // Fast path: exact tiling, no bounds checks needed
    if (exactTiling) {
      for (int y = 0; y < 64; y++) {
        const uint32_t *srcRow = pixels.data() + (tileY * 64 + y) * imageWidth + tileX * 64;
        uint32_t *dstRow = tilePixels.data() + y * 64;
        std::copy_n(srcRow, 64, dstRow);
      }
    } else {
      // Safe path: handle partial tiles at image edges
      std::fill(tilePixels.begin(), tilePixels.end(), 0u);
      const int maxCopyRows = std::min(64, std::max(0, imageHeight - tileY * 64));
      const int maxCopyCols = std::min(64, std::max(0, imageWidth - tileX * 64));
      for (int y = 0; y < maxCopyRows; y++) {
        const uint32_t *srcRow = pixels.data() + (tileY * 64 + y) * imageWidth + tileX * 64;
        uint32_t *dstRow = tilePixels.data() + y * 64;
        std::copy_n(srcRow, static_cast<size_t>(maxCopyCols), dstRow);
      }
    }

    // Create palette-based tile
    if (!createTile(tilePixels, tisFile.tiles[tileIndex])) {
      Log(ERROR, "TIS", "Failed to create tile {} in batch {}-{}", tileIndex,
          batchStart, batchEnd);
      return false;
    }

    Log(DEBUG, "TIS", "Created palette-based tile {}: {}x{} (batch {}-{})",
        tileIndex, tileX, tileY, batchStart, batchEnd);
  }

  return true;
}

void TIS::calculateGridFromTileCount() {
    uint32_t tileCount = 0;
    
    if (isPvrzBased) {
        tileCount = tisV2File.header.tileCount;
    } else {
        tileCount = tisV1File.header.tileCount;
    }
    
    if (tileCount > 0) {
        // Default to single row layout when no WED file is available
        tilesPerRow = static_cast<int>(tileCount);
        tilesPerColumn = 1;
        
        Log(DEBUG, "TIS", "Using single row layout: {}x{} ({} tiles)", tilesPerRow, tilesPerColumn, tileCount);
    } else {
        Log(ERROR, "TIS", "No tiles found for grid calculation");
        tilesPerRow = 0;
        tilesPerColumn = 0;
    }
}

bool TIS::convertPngToTisV1Palette() {
    Log(DEBUG, "TIS", "Converting PNG to TIS V1 (palette-based) format");
    
    // Get the input PNG file path (from upscaled directory)
    std::string inputFile = getUpscaledDir() + "/" + resourceName_ + ".png";
    std::string outputFile = getAssembleDir() + "/" + originalFileName;

    // We'll stream-read the PNG and write TIS tiles on the fly
    std::ofstream outFile; // opened after we know dimensions

    // State captured across rows
    int imageWidth = 0, imageHeight = 0;
    int totalTilesPerRow = 0, totalTilesPerColumn = 0, totalTileCount = 0;
    bool headerWritten = false;
    int stripeRow = 0; // row within 64-row stripe
    std::vector<uint32_t> stripeBuffer; // holds up to 64 rows
    uint64_t tilesWritten = 0;

    auto onRow = [&](int width, int height, int rowIndex, const std::vector<uint32_t> &argbRow) -> bool {
        if (!headerWritten) {
            imageWidth = width;
            imageHeight = height;
            totalTilesPerRow = imageWidth / 64;
            totalTilesPerColumn = imageHeight / 64;
            totalTileCount = totalTilesPerRow * totalTilesPerColumn;

            Log(DEBUG, "TIS", "Loaded image: {}x{}", imageWidth, imageHeight);
            Log(DEBUG, "TIS", "Image grid dimensions: {}x{} tiles ({} total)", totalTilesPerRow, totalTilesPerColumn, totalTileCount);

            // Optionally parse WED for layout logs
            if (!parseWEDFile()) {
                Log(WARNING, "TIS", "Failed to parse WED file, using single row layout");
                tilesPerRow = totalTileCount;
                tilesPerColumn = 1;
            }
            Log(DEBUG, "TIS", "Primary grid (from WED): {}x{} = {} tiles", tilesPerRow, tilesPerColumn, tilesPerRow * tilesPerColumn);
            int primaryTileCount = tilesPerRow * tilesPerColumn;
            int secondaryTileCount = totalTileCount - primaryTileCount;
            Log(DEBUG, "TIS", "Secondary tiles: {}", secondaryTileCount);

            // Open output and write header
            outFile.open(outputFile, std::ios::binary);
            if (!outFile) {
                Log(ERROR, "TIS", "Cannot write to {}", outputFile);
                return false;
            }
            TISHeader header;
            header.setTileCount(static_cast<uint32_t>(totalTileCount));
            header.tileSize = 5120;
            outFile.write(reinterpret_cast<const char*>(&header), sizeof(TISHeader));

            // Prepare stripe buffer for 64 rows
            stripeBuffer.assign(static_cast<size_t>(imageWidth) * 64, 0u);
            headerWritten = true;
        }

        // Ignore rows that fall outside complete 64-row stripes (for non-multiples)
        if (rowIndex >= totalTilesPerColumn * 64) {
            return true; // done with full stripes
        }

        // Copy this row into the stripe buffer
        std::copy(argbRow.begin(), argbRow.end(), stripeBuffer.begin() + static_cast<size_t>(stripeRow) * imageWidth);
        stripeRow++;

        // When stripe is full, process tiles across this stripe
        if (stripeRow == 64) {
            for (int tileCol = 0; tileCol < totalTilesPerRow; ++tileCol) {
                // Build a 64x64 tile from the stripe
                std::vector<uint32_t> tilePixels(64 * 64);
                for (int y = 0; y < 64; ++y) {
                    const uint32_t *srcRow = stripeBuffer.data() + y * imageWidth + tileCol * 64;
                    std::copy_n(srcRow, 64, tilePixels.data() + y * 64);
                }
                // Create palette-based tile and write directly
                TISV1Tile tile;
                if (!createTile(tilePixels, tile)) {
                    Log(ERROR, "TIS", "Failed to create tile at row stripe {}, col {}", rowIndex / 64, tileCol);
                    return false;
                }
                outFile.write(reinterpret_cast<const char*>(&tile), sizeof(TISV1Tile));
                tilesWritten++;
            }
            stripeRow = 0; // reset for next stripe
        }

        return true;
    };

    if (!loadPNGRows(inputFile, onRow)) {
        Log(ERROR, "TIS", "Failed to stream PNG rows from: {}", inputFile);
        if (outFile.is_open()) outFile.close();
        return false;
    }

    if (outFile.is_open()) outFile.close();

    Log(DEBUG, "TIS", "Successfully created TIS V1 (palette-based) file: {}", outputFile);
    Log(DEBUG, "TIS", "Created {} palette-based tiles (expected {})", tilesWritten, static_cast<uint64_t>(totalTileCount));
    
    return tilesWritten == static_cast<uint64_t>(totalTileCount);
}

// Batch operations (implemented by PluginManager)
bool TIS::extractAll() {
    return ProjectIE4k::PluginManager::getInstance().extractAllResourcesOfType(IE_TIS_CLASS_ID);
}

bool TIS::upscaleAll() {
    return ProjectIE4k::PluginManager::getInstance().upscaleAllResourcesOfType(IE_TIS_CLASS_ID);
}

bool TIS::assembleAll() {
    return ProjectIE4k::PluginManager::getInstance().assembleAllResourcesOfType(IE_TIS_CLASS_ID);
}

// Clean directories before operations - operation-specific
bool TIS::cleanExtractDirectory() {
    Log(DEBUG, "TIS", "Cleaning extract directory for resource: {}", resourceName_);
    std::string dir = getExtractDir(false);
    if (std::filesystem::exists(dir)) {
        try {
            std::filesystem::remove_all(dir);
            Log(DEBUG, "TIS", "Cleaned extract directory: {}", dir);
            return true;
        } catch (const std::filesystem::filesystem_error& e) {
            Log(ERROR, "TIS", "Failed to clean extract directory {}: {}", dir, e.what());
            return false;
        }
    }
    return true; // Directory doesn't exist, nothing to clean
}

bool TIS::cleanUpscaleDirectory() {
    Log(DEBUG, "TIS", "Cleaning upscale directory for resource: {}", resourceName_);
    std::string dir = getUpscaledDir(false);
    if (std::filesystem::exists(dir)) {
        try {
            std::filesystem::remove_all(dir);
            Log(DEBUG, "TIS", "Cleaned upscale directory: {}", dir);
            return true;
        } catch (const std::filesystem::filesystem_error& e) {
            Log(ERROR, "TIS", "Failed to clean upscale directory {}: {}", dir, e.what());
            return false;
        }
    }
    return true; // Directory doesn't exist, nothing to clean
}

bool TIS::cleanAssembleDirectory() {
    Log(DEBUG, "TIS", "Cleaning assemble directory for resource: {}", resourceName_);
    std::string dir = getAssembleDir(false);
    if (std::filesystem::exists(dir)) {
        try {
            std::filesystem::remove_all(dir);
            Log(DEBUG, "TIS", "Cleaned assemble directory: {}", dir);
            return true;
        } catch (const std::filesystem::filesystem_error& e) {
            Log(ERROR, "TIS", "Failed to clean assemble directory {}: {}", dir, e.what());
            return false;
        }
    }
    return true; // Directory doesn't exist, nothing to clean
}

std::string TIS::getBaseResourceName() const {
    // Handle night variants (e.g., AR1900N.TIS -> AR1900)
    // Check if the resource name ends with 'N' (night variant)
    if (resourceName_.length() > 1 && resourceName_.back() == 'N') {
        std::string baseName = resourceName_.substr(0, resourceName_.length() - 1);
        Log(DEBUG, "TIS", "Night variant detected: '{}' -> base name: '{}'", resourceName_, baseName);
        return baseName;
    }
    
    // Not a night variant, return the original name
    return resourceName_;
}

// Path management - TIS-specific paths
std::string TIS::getOutputDir(bool ensureDir) const {
    return constructPath("-tis", ensureDir);
}

std::string TIS::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-tis-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string TIS::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-tis-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string TIS::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-tis-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

void TIS::registerCommands(CommandTable& commandTable) {
    commandTable["tis"] = {
        "TIS file operations",
        {
            {"extract", {
                "Extract TIS resource to PNG tiles (e.g., tis extract ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: tis extract <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().extractResource(args[0], IE_TIS_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {
                "Upscale TIS frames (e.g., tis upscale ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: tis upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().upscaleResource(args[0], IE_TIS_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {
                "Assemble PNG tiles into TIS file (e.g., tis assemble ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: tis assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return ProjectIE4k::PluginManager::getInstance().assembleResource(args[0], IE_TIS_CLASS_ID) ? 0 : 1;
                }
            }},
        }
    };
}

REGISTER_PLUGIN(TIS, IE_TIS_CLASS_ID);

} // namespace ProjectIE4k
