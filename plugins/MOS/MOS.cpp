#include "MOS.h"

#include <fstream>
#include <algorithm>
#include <cstring>
#include <zlib.h>
#include <iostream>

#include <png.h>

#include "core/SClassID.h"
#include "core/Logging/Logging.h"
#include "plugins/ColorReducer.h"
#include "plugins/PVRZ/PVRZ.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

MOS::MOS(const std::string& resourceName_) 
    : PluginBase(resourceName_, IE_MOS_CLASS_ID), compressed(false), isV2(false) {
    // Extract signature and version
    if (!readSigAndVer(originalFileData)) {
        Log(ERROR, "MOS", "Failed to read signature and version");
        return;
    }
    
    // Mark plugin as valid since we successfully loaded the MOS resource
    valid_ = true;
}

MOS::~MOS() {
}

bool MOS::assemble() {
    Log(MESSAGE, "MOS", "Starting MOS assembly for resource: {}", resourceName_);
    
    if (isV2) {
        return convertPngToMosV2();
    } else {
        return convertPngToMosV1();
    }
}

bool MOS::extract() {
    Log(MESSAGE, "MOS", "Starting MOS extraction for resource: {}", resourceName_);
    
    if (isV2) {
        return convertMosV2ToPng();
    } else {
        return convertMosToPng();
    }
}

// Using default service-based upscale() from PluginBase

bool MOS::readSigAndVer(const std::vector<uint8_t>& fileData) {
    if (fileData.size() < 8) {
        Log(ERROR, "MOS", "File too small to read signature");
        return false;
    }
    
    // Copy the first 8 bytes to the signature array
    memcpy(referenceSignature, fileData.data(), 8);

    // Log the signature for debugging
    std::string signatureStr(referenceSignature, 8);
    Log(MESSAGE, "MOS", "Read signature: '{}'", signatureStr);

    // Check if it's a valid MOS signature
    if (memcmp(referenceSignature, "MOS V1  ", 8) == 0) {
        Log(MESSAGE, "MOS", "Detected MOS V1 format");
        isV2 = false;
        compressed = false;
        return true;
    } else if (memcmp(referenceSignature, "MOSCV1  ", 8) == 0) {
        Log(MESSAGE, "MOS", "Detected compressed MOS V1 format");
        isV2 = false;
        compressed = true;
        return true;
    } else if (memcmp(referenceSignature, "MOS ", 4) == 0) {
        Log(MESSAGE, "MOS", "Detected MOS V2 format");
        isV2 = true;
        compressed = false;
        return true;
    } else if (memcmp(referenceSignature, "MOSC", 4) == 0) {
        // Check version for MOSC
        if (fileData.size() >= 8 && memcmp(fileData.data() + 4, "V2  ", 4) == 0) {
            Log(MESSAGE, "MOS", "Detected compressed MOS V2 format");
            isV2 = true;
            compressed = true;
        } else {
            Log(MESSAGE, "MOS", "Detected compressed MOS V1 format");
            isV2 = false;
            compressed = true;
        }
        return true;
    } else {
        Log(ERROR, "MOS", "Unknown MOS signature: '{}'", signatureStr);
        return false;
    }
}

bool MOS::convertMosToPng() {
    const std::string& outputFile = getExtractDir() + "/" + resourceName_ + ".png";
    Log(DEBUG, "MOS", "Converting MOS data to {}", outputFile);

    if (originalFileData.empty()) {
        Log(ERROR, "MOS", "No file data available");
        return false;
    }
    
    size_t size = originalFileData.size();
    
    std::vector<uint8_t> mosData;
    if (size >= sizeof(MOSCHeader) && compressed) {
        MOSCHeader moscHeader;
        memcpy(&moscHeader, originalFileData.data(), sizeof(MOSCHeader));

        std::vector<uint8_t> compressedData(originalFileData.begin() + sizeof(MOSCHeader), originalFileData.end());
        mosData = decompressZlib(compressedData, moscHeader.uncompressedSize);
        if (mosData.empty()) {
            return false;
        }
    } else {
        mosData = originalFileData;
    }

    MOSV1File mosFile;
    if (!mosFile.deserialize(mosData)) {
        Log(ERROR, "MOS", "Error: Failed to deserialize MOS V1 data");
        return false;
    }
    
    std::vector<uint32_t> pixels(mosFile.header.width * mosFile.header.height, 0);
    const int tileSize = 64;
    
    for (int row = 0; row < mosFile.header.rows; ++row) {
        for (int col = 0; col < mosFile.header.cols; ++col) {
            int tileIndex = row * mosFile.header.cols + col;
            if (tileIndex >= static_cast<int>(mosFile.tileData.size())) continue;

            int tileX = col * tileSize;
            int tileY = row * tileSize;
            int tileW = std::min(tileSize, mosFile.header.width - tileX);
            int tileH = std::min(tileSize, mosFile.header.height - tileY);

            const auto& tileData = mosFile.tileData[tileIndex];
            const auto& tilePalette = mosFile.tilePalettes[tileIndex];

            for (int y = 0; y < tileH; y++) {
                for (int x = 0; x < tileW; x++) {
                    int tilePixelIndex = y * tileW + x;
                    if (tilePixelIndex >= static_cast<int>(tileData.size())) continue;
                    
                    uint8_t paletteIndex = tileData[tilePixelIndex];
                    if (paletteIndex >= tilePalette.size()) continue;
                    
                    const PaletteEntry& entry = tilePalette[paletteIndex];
                    uint32_t pixel = entry.toARGB();
                    
                    int imageX = tileX + x;
                    int imageY = tileY + y;
                    if (imageX < mosFile.header.width && imageY < mosFile.header.height) {
                        pixels[imageY * mosFile.header.width + imageX] = pixel;
                    }
                }
            }
        }
    }
    
    if (!savePNG(outputFile, pixels, mosFile.header.width, mosFile.header.height)) {
        return false;
    }

    // std::cout << "Successfully created PNG file: " << outputFile <<
    // std::endl;
    return true;
}

// Compress data using zlib
std::vector<uint8_t> MOS::compressZlib(const std::vector<uint8_t>& data) {
    uLongf compressedSize = compressBound(data.size());
    std::vector<uint8_t> compressed(compressedSize);
    
    int result = compress(compressed.data(), &compressedSize, data.data(), data.size());
    if (result != Z_OK) {
        std::cerr << "Error: zlib compression failed" << std::endl;
        return {};
    }
    
    compressed.resize(compressedSize);
    return compressed;
}

// Decompress data using zlib
std::vector<uint8_t> MOS::decompressZlib(const std::vector<uint8_t>& compressedData, uint32_t expectedSize) {
    std::vector<uint8_t> decompressed(expectedSize);
    uLongf decompressedSize = expectedSize;
    
    int result = uncompress(decompressed.data(), &decompressedSize, 
                           compressedData.data(), compressedData.size());
    if (result != Z_OK) {
        std::cerr << "Error: zlib decompression failed" << std::endl;
        return {};
    }
    
    if (decompressedSize != expectedSize) {
        std::cerr << "Error: Decompressed size mismatch" << std::endl;
        return {};
    }
    
    return decompressed;
}

bool MOS::convertPngToMosV1() {
    std::string inputFile = getUpscaledDir() + "/" + resourceName_ + ".png";
    std::string outputFile = getAssembleDir(true) + "/" + originalFileName;
    
    Log(DEBUG, "MOS", "convertPngToMosV1 called with compressed={}", compressed);
    Log(DEBUG, "MOS", "Converting {} to {}", inputFile, outputFile);
    
    // Load PNG using PluginBase
    std::vector<uint32_t> pixels;
    int width, height;
    if (!loadPNG(inputFile, pixels, width, height)) {
        Log(ERROR, "MOS", "Failed to load PNG file: {}", inputFile);
        return false;
    }
    
    Log(DEBUG, "MOS", "Loaded image: {}x{}", width, height);
    
    // Calculate tile layout
    const int tileSize = 64;
    int cols = (width + tileSize - 1) / tileSize;
    int rows = (height + tileSize - 1) / tileSize;
    int tileCount = cols * rows;
    
    Log(DEBUG, "MOS", "Tiling: {}x{} = {} tiles", cols, rows, tileCount);
    
    // Create MOS V1 file structure
    MOSV1File mosFile;
    mosFile.header.setDimensions(width, height, cols, rows);
    
    // Process each tile
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int tileX = col * tileSize;
            int tileY = row * tileSize;
            int tileW = std::min(tileSize, width - tileX);
            int tileH = std::min(tileSize, height - tileY);
            
            // Extract tile pixels
            std::vector<uint32_t> tilePixels(tileW * tileH);
            for (int y = 0; y < tileH; y++) {
                for (int x = 0; x < tileW; x++) {
                    int srcX = tileX + x;
                    int srcY = tileY + y;
                    tilePixels[y * tileW + x] = pixels[srcY * width + srcX];
                }
            }
            
            // Generate palette for this tile
            std::vector<uint32_t> palette;
            ColorReducer::medianCut(tilePixels, 255, palette, true);
            
            // Create color cache (like Near Infinity does)
            std::map<uint32_t, uint8_t> colorCache;
            
            // Create tile palette (256 entries)
            std::vector<PaletteEntry> tilePalette(256);
            // First palette entry denotes transparency (exactly like Java)
            tilePalette[0] = PaletteEntry(0, 255, 0, 0); // B=0, G=255 (transparency flag), R=0, A=0
            for (size_t i = 1; i < palette.size() && i < 256; i++) {
                uint32_t color = palette[i - 1]; // Use i-1 like Java does
                tilePalette[i] = PaletteEntry::fromARGB(color);
                colorCache[color] = static_cast<uint8_t>(i - 1);
            }
            
            // Create tile data (tileW * tileH)
            std::vector<uint8_t> tileData(tileW * tileH);
            for (size_t i = 0; i < tilePixels.size(); i++) {
                uint32_t pixel = tilePixels[i];
                if ((pixel >> 24) == 0) {
                    tileData[i] = 0; // Transparent
                } else {
                    // Check color cache first (like Near Infinity)
                    auto it = colorCache.find(pixel);
                    if (it != colorCache.end()) {
                        tileData[i] = it->second + 1;
                    } else {
                        int index = ColorReducer::getNearestColor(pixel, palette);
                        tileData[i] = index + 1;
                        colorCache[pixel] = static_cast<uint8_t>(index);
                    }
                }
            }
            
            // Add to MOS file structure
            mosFile.tilePalettes.push_back(tilePalette);
            mosFile.tileData.push_back(tileData);
        }
    }
    
    // Calculate tile offsets
    size_t currentOffset = 0;
    for (const auto& tileData : mosFile.tileData) {
        mosFile.tileEntries.emplace_back(currentOffset);
        currentOffset += tileData.size();
    }
    
    // Serialize to binary data
    std::vector<uint8_t> mosData = mosFile.serialize();
    
    // Optionally compress to MOSC
    Log(DEBUG, "MOS", "Compression flag: {}", compressed);
    if (compressed) {
        Log(DEBUG, "MOS", "Compressing to MOSC format...");
        
        // Create MOSC header
        MOSCHeader moscHeader;
        moscHeader.setUncompressedSize(mosData.size());
        
        // Compress data
        std::vector<uint8_t> compressedData = compressZlib(mosData);
        if (compressedData.empty()) {
            Log(ERROR, "MOS", "Failed to compress data");
            return false;
        }
        
        // Write MOSC file
        std::ofstream outFile(outputFile, std::ios::binary);
        if (!outFile) {
            Log(ERROR, "MOS", "Cannot write to {}", outputFile);
            return false;
        }
        
        // Write MOSC header
        outFile.write(reinterpret_cast<const char*>(&moscHeader), sizeof(MOSCHeader));
        
        // Write compressed data
        outFile.write(reinterpret_cast<const char*>(compressedData.data()), compressedData.size());
        outFile.close();
        
        Log(MESSAGE, "MOS", "Successfully created compressed MOSC file: {}", outputFile);
    } else {
        // Write uncompressed MOS file
        std::ofstream outFile(outputFile, std::ios::binary);
        if (!outFile) {
            Log(ERROR, "MOS", "Cannot write to {}", outputFile);
            return false;
        }
        
        outFile.write(reinterpret_cast<const char*>(mosData.data()), mosData.size());
        outFile.close();
        
        Log(MESSAGE, "MOS", "Successfully created MOS V1 file: {}", outputFile);
    }
    
    return true;
}

bool MOS::convertPngToMosV2() {
    Log(DEBUG, "MOS", "Converting PNG to MOS V2 format");
    
    // Get the input PNG file path
    std::string inputFile = getUpscaledDir() + "/" + resourceName_ + ".png";
    std::string outputFile = getAssembleDir(true) + "/" + originalFileName;
    
    Log(DEBUG, "MOS", "Converting {} to {}", inputFile, outputFile);
    
    // Load PNG using PluginBase
    std::vector<uint32_t> pixels;
    int width, height;
    if (!loadPNG(inputFile, pixels, width, height)) {
        Log(ERROR, "MOS", "Failed to load PNG file: {}", inputFile);
        return false;
    }
    
    Log(DEBUG, "MOS", "Loaded image: {}x{}", width, height);
    
    // For MOS V2, we'll create a single PVRZ file with the image packed into it
    // Use a simple approach: create one PVRZ file with the full image
    const int pvrzPage = 1; // Use page 1 for simplicity
    
    // Resize image to power of 2 for DXT compression
    int newWidth = PVRZ::nextPowerOfTwo(width);
    int newHeight = PVRZ::nextPowerOfTwo(height);
    
    std::vector<uint32_t> resizedImage(newWidth * newHeight, 0);
    for (int y = 0; y < std::min(height, newHeight); y++) {
        for (int x = 0; x < std::min(width, newWidth); x++) {
            resizedImage[y * newWidth + x] = pixels[y * width + x];
        }
    }
    
    // Convert to ARGB format for PVRZCreator
    std::vector<uint8_t> argbData(newWidth * newHeight * 4);
    for (int i = 0; i < newWidth * newHeight; i++) {
        uint32_t pixel = resizedImage[i];
        argbData[i * 4 + 0] = (pixel >> 24) & 0xFF; // A
        argbData[i * 4 + 1] = (pixel >> 16) & 0xFF; // R
        argbData[i * 4 + 2] = (pixel >> 8) & 0xFF;  // G
        argbData[i * 4 + 3] = pixel & 0xFF;         // B
    }
    
    // Use PVRZCreator to compress to DXT format
    PVRZ pvrzCreator;
    std::vector<uint8_t> dxtData = pvrzCreator.compressToDXT(argbData, newWidth, newHeight, PVRZFormat::AUTO);
    if (dxtData.empty()) {
        Log(ERROR, "MOS", "Failed to compress image to DXT format");
        return false;
    }
    
    // Create PVRZ file with proper naming convention using PluginManager
    std::string pvrzDir = getAssembleDir(true);
            auto [pvrzName, pageNum] = PluginManager::getInstance().generatePVRZName(resourceName_, IE_MOS_CLASS_ID);
    std::string pvrzFilename = pvrzDir + "/" + pvrzName + ".PVRZ";
    
    if (!pvrzCreator.writePVRZFile(pvrzFilename, dxtData, newWidth, newHeight, PVRZFormat::AUTO)) {
        Log(ERROR, "MOS", "Failed to write PVRZ file: {}", pvrzFilename);
        return false;
    }
    
    Log(DEBUG, "MOS", "Created PVRZ file: {}", pvrzFilename);
    
    // Create MOS V2 file structure
    MOSV2File mosFile;
    mosFile.header.setDimensions(width, height, 1); // Single data block
    
    // Add data block referencing the PVRZ file; source coordinates are (0,0)
    MOSV2DataBlock dataBlock(static_cast<uint32_t>(pageNum), 0, 0, width, height, 0, 0);
    mosFile.dataBlocks.push_back(dataBlock);
    
    // Serialize to binary data
    std::vector<uint8_t> mosData = mosFile.serialize();
    
    // Optionally compress to MOSC
    if (compressed) {
        Log(DEBUG, "MOS", "Compressing to MOSC V2 format...");
        
        // Create MOSC header
        MOSCHeader moscHeader;
        moscHeader.setUncompressedSize(mosData.size());
        memcpy(moscHeader.version, "V2 ", 4); // Set version to V2
        
        // Compress data
        std::vector<uint8_t> compressedData = compressZlib(mosData);
        if (compressedData.empty()) {
            Log(ERROR, "MOS", "Failed to compress MOS V2 data");
            return false;
        }
        
        // Write MOSC file
        std::ofstream outFile(outputFile, std::ios::binary);
        if (!outFile) {
            Log(ERROR, "MOS", "Cannot write to {}", outputFile);
            return false;
        }
        
        // Write MOSC header
        outFile.write(reinterpret_cast<const char*>(&moscHeader), sizeof(MOSCHeader));
        outFile.write(reinterpret_cast<const char*>(compressedData.data()), compressedData.size());
        outFile.close();
        
        Log(MESSAGE, "MOS", "Successfully created compressed MOSC V2 file: {}", outputFile);
    } else {
        // Write uncompressed MOS V2 file
        std::ofstream outFile(outputFile, std::ios::binary);
        if (!outFile) {
            Log(ERROR, "MOS", "Cannot write to {}", outputFile);
            return false;
        }
        
        outFile.write(reinterpret_cast<const char*>(mosData.data()), mosData.size());
        outFile.close();
        
        Log(MESSAGE, "MOS", "Successfully created MOS V2 file: {}", outputFile);
    }
    
    return true;
}

bool MOS::convertMosV2ToPng() {
    Log(DEBUG, "MOS", "Converting MOS V2 to PNG format");
    
    // Get the output PNG file path
    std::string outputFile = getExtractDir() + "/" + resourceName_ + ".png";
    
    if (originalFileData.empty()) {
        Log(ERROR, "MOS", "No file data available");
        return false;
    }
    
    // Check if it's a MOSC file (compressed)
    std::vector<uint8_t> mosData;
    
    if (originalFileData.size() >= sizeof(MOSCHeader)) {
        MOSCHeader moscHeader;
        memcpy(&moscHeader, originalFileData.data(), sizeof(MOSCHeader));
        
        if (memcmp(moscHeader.signature, "MOSC", 4) == 0) {
            Log(MESSAGE, "MOS", "Detected compressed MOSC V2 file");
            
            // Decompress data
            std::vector<uint8_t> compressedData(originalFileData.begin() + sizeof(MOSCHeader), originalFileData.end());
            mosData = decompressZlib(compressedData, moscHeader.uncompressedSize);
            if (mosData.empty()) {
                Log(ERROR, "MOS", "Failed to decompress MOSC V2 data");
                return false;
            }
        } else {
            // Check if it's a regular MOS V2 file by looking for "MOS " signature
            if (originalFileData.size() >= 4 && memcmp(originalFileData.data(), "MOS ", 4) == 0) {
                Log(MESSAGE, "MOS", "Detected uncompressed MOS V2 file");
                mosData = originalFileData;
            } else {
                Log(ERROR, "MOS", "Unknown file format");
                return false;
            }
        }
    } else {
        Log(ERROR, "MOS", "File too small to be a valid MOS V2/MOSC V2 file");
        return false;
    }
    
    // Deserialize MOS V2 data
    MOSV2File mosFile;
    if (!mosFile.deserialize(mosData)) {
        Log(ERROR, "MOS", "Failed to deserialize MOS V2 data");
        return false;
    }
    
    Log(DEBUG, "MOS", "MOS V2 file: {}x{} ({} data blocks)", 
        mosFile.header.width, mosFile.header.height, mosFile.header.dataBlockCount);
    
    // Reconstruct image
    std::vector<uint32_t> pixels(mosFile.header.width * mosFile.header.height, 0);
    
    // Load and copy real image data from PVRZ files
    std::map<uint32_t, std::vector<uint32_t>> pvrzCache;
    std::map<uint32_t, int> pvrzWidths;
    std::map<uint32_t, int> pvrzHeights;
    
    for (const auto& block : mosFile.dataBlocks) {
        // Load and cache the PVRZ image if not already loaded
        if (pvrzCache.find(block.pvrzPage) == pvrzCache.end()) {
            std::vector<uint8_t> argbData;
            int pvrzWidth, pvrzHeight;
            
        // Use PluginManager to generate the correct PVRZ resource name
        std::string pvrzName = PluginManager::getInstance().generatePVRZNameInternal(resourceName_, block.pvrzPage, IE_MOS_CLASS_ID);
            
            PVRZ pvrz;
            if (pvrz.loadPVRZResourceAsARGB(pvrzName, argbData, pvrzWidth, pvrzHeight)) {
                // Convert ARGB to uint32_t pixels
                std::vector<uint32_t> pvrzPixels(pvrzWidth * pvrzHeight);
                for (int i = 0; i < pvrzWidth * pvrzHeight; i++) {
                    uint32_t pixel = (argbData[i * 4 + 0] << 24) | // A
                                   (argbData[i * 4 + 1] << 16) | // R
                                   (argbData[i * 4 + 2] << 8) |  // G
                                   argbData[i * 4 + 3];          // B
                    pvrzPixels[i] = pixel;
                }
                pvrzCache[block.pvrzPage] = pvrzPixels;
                pvrzWidths[block.pvrzPage] = pvrzWidth;
                pvrzHeights[block.pvrzPage] = pvrzHeight;
            } else {
                Log(ERROR, "MOS", "Could not load PVRZ page: {}", block.pvrzPage);
                continue;
            }
        }
        
        const std::vector<uint32_t>& atlas = pvrzCache[block.pvrzPage];
        int atlasWidth = pvrzWidths[block.pvrzPage];
        int atlasHeight = pvrzHeights[block.pvrzPage];
        
        // Copy the region from the PVRZ image to the output image
        for (uint32_t y = 0; y < block.height; y++) {
            for (uint32_t x = 0; x < block.width; x++) {
                uint32_t srcX = block.sourceX + x;
                uint32_t srcY = block.sourceY + y;
                uint32_t dstX = block.targetX + x;
                uint32_t dstY = block.targetY + y;
                if (srcX < (uint32_t)atlasWidth && srcY < (uint32_t)atlasHeight &&
                    dstX < (uint32_t)mosFile.header.width && dstY < (uint32_t)mosFile.header.height) {
                    pixels[dstY * mosFile.header.width + dstX] = atlas[srcY * atlasWidth + srcX];
                }
            }
        }
    }
    
    // Save as PNG
    if (!savePNG(outputFile, pixels, mosFile.header.width, mosFile.header.height)) {
        Log(ERROR, "MOS", "Failed to save PNG file: {}", outputFile);
        return false;
    }
    
    Log(MESSAGE, "MOS", "Successfully created PNG file: {}", outputFile);
    return true;
}

// PluginBase interface implementation

// Batch operations (implemented by PluginManager)
bool MOS::extractAll() {
    return PluginManager::getInstance().extractAllResourcesOfType(IE_MOS_CLASS_ID);
}

bool MOS::upscaleAll() {
    return PluginManager::getInstance().upscaleAllResourcesOfType(IE_MOS_CLASS_ID);
}

bool MOS::assembleAll() {
    return PluginManager::getInstance().assembleAllResourcesOfType(IE_MOS_CLASS_ID);
}

// Clean directories before operations - operation-specific
bool MOS::cleanExtractDirectory() {
    Log(DEBUG, "MOS", "Cleaning extract directory for resource: {}", resourceName_);
    return cleanDirectory(getExtractDir(false));
}

bool MOS::cleanUpscaleDirectory() {
    Log(DEBUG, "MOS", "Cleaning upscale directory for resource: {}", resourceName_);
    return cleanDirectory(getUpscaledDir(false));
}

bool MOS::cleanAssembleDirectory() {
    Log(DEBUG, "MOS", "Cleaning assemble directory for resource: {}", resourceName_);
    return cleanDirectory(getAssembleDir(false));
}

bool MOS::cleanDirectory(const std::string& dir) {
    if (std::filesystem::exists(dir)) {
        try {
            std::filesystem::remove_all(dir);
            Log(DEBUG, "MOS", "Cleaned directory: {}", dir);
            return true;
        } catch (const std::filesystem::filesystem_error& e) {
            Log(ERROR, "MOS", "Failed to clean directory {}: {}", dir, e.what());
            return false;
        }
    }
    return true; // Directory doesn't exist, nothing to clean
}

// Path management - MOS-specific paths
std::string MOS::getOutputDir(bool ensureDir) const {
    return constructPath("-mos", ensureDir);
}

std::string MOS::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-mos-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string MOS::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-mos-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string MOS::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-mos-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

void MOS::registerCommands(CommandTable& commandTable) {
    commandTable["mos"] = {
        "MOS file operations",
        {
            {"extract", {
                "Extract MOS resource to PNG image (e.g., mos extract ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: mos extract <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().extractResource(args[0], IE_MOS_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {
                "Upscale MOS frames (e.g., mos upscale ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: mos upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().upscaleResource(args[0], IE_MOS_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {
                "Assemble PNG image into MOS file (e.g., mos assemble ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: mos assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().assembleResource(args[0], IE_MOS_CLASS_ID) ? 0 : 1;
                }
            }},
        }
    };
}

REGISTER_PLUGIN(MOS, IE_MOS_CLASS_ID);

} // namespace ProjectIE4k
