#include "BAM.h"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <zlib.h>

#include "core/SClassID.h"
#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include "plugins/ColorReducer.h"
#include "plugins/PVRZ/PVRZ.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

BAM::BAM(const std::string& resourceName_) 
    : PluginBase(resourceName_, IE_BAM_CLASS_ID), bamFormat(BAMFormat::UNKNOWN), wasOriginallyCompressed(false), valid_(false) {
    if (originalFileData.empty()) {
        Log(ERROR, "BAM", "No data loaded for BAM resource: {}", resourceName_);
        return;
    }
    
    // Detect format
    if (!detectFormat()) {
        Log(ERROR, "BAM", "Failed to detect BAM format");
        return;
    }
    
    // Deserialize original data based on format
    if (isV1Format()) {
        originalBamV1File.deserialize(originalFileData);
        Log(DEBUG, "BAM", "Loaded original V1 data with {} frames", originalBamV1File.frameEntries.size());
    } else if (isV2Format()) {
        originalBamV2File.deserialize(originalFileData);
        Log(DEBUG, "BAM", "Loaded original V2 data with {} frames", originalBamV2File.frameEntries.size());
    }

    // Mark plugin as valid since we successfully loaded the BAM resource
    valid_ = true;
}

BAM::~BAM() {
    // Clean shutdown - ensure large data structures are properly cleared
    if (isV1Format()) {
        bamV1File.frameData.clear();
        bamV1File.frameData.shrink_to_fit();
        originalBamV1File.frameData.clear();
        originalBamV1File.frameData.shrink_to_fit();
    }
    
    // Clear original file data
    originalFileData.clear();
    originalFileData.shrink_to_fit();
}

bool BAM::extract() {
    Log(DEBUG, "BAM", "Starting BAM extraction for resource: {}", resourceName_);
    
    // Convert to PNG
    if (!convertBamToPng()) {
        Log(ERROR, "BAM", "Failed to convert BAM to PNG");
        return false;
    }
    
    Log(DEBUG, "BAM", "Successfully extracted BAM: {}", resourceName_);
    return true;
}

bool BAM::assemble() {
    Log(DEBUG, "BAM", "Starting BAM assembly for resource: {}", resourceName_);
    // Convert PNG to BAM
    if (!convertPngToBam()) {
        Log(ERROR, "BAM", "Failed to convert PNG to BAM");
        return false;
    }
    
    // Save the assembled BAM file
    std::string outputPath = getAssembleDir() + "/" + originalFileName;
    if (!saveToFile(outputPath)) {
        Log(ERROR, "BAM", "Failed to save assembled BAM file: {}", outputPath);
        return false;
    }
    
    Log(DEBUG, "BAM", "Successfully assembled BAM: {}", resourceName_);
    return true;
}

// Path management overrides
std::string BAM::getOutputDir(bool ensureDir) const {
    return constructPath("-bam", ensureDir);
}

std::string BAM::getExtractOutputDir(bool ensureDir) const {
    return constructExtractPath("-bam", ensureDir);
}

std::string BAM::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-bam-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string BAM::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-bam-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string BAM::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-bam-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

bool BAM::saveToFile(const std::string& filePath) const {
    std::vector<uint8_t> data;

    if (isV1Format()) {
        data = bamV1File.serialize();
        
        // Check if original was BAMC V1 and compress if needed
        if (wasOriginallyCompressed) {
            Log(DEBUG, "BAM", "Original was BAMC V1, compressing output");
            data = compressBAMC(data);
        }
    } else if (isV2Format()) {
        data = bamV2File.serialize();
    } else {
        Log(ERROR, "BAM", "Unknown BAM format for saving");
        return false;
    }
    
    // Write file
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "BAM", "Could not create file: {}", filePath);
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    
    if (file.fail()) {
        Log(ERROR, "BAM", "Failed to write file: {}", filePath);
        return false;
    }
    
    return true;
}

bool BAM::detectFormat() {
    // Check if we have enough data for a BAM header (minimum 8 bytes for signature + version)
    if (originalFileData.size() < 8) {
        Log(ERROR, "BAM", "File too small for BAM format: {} bytes", originalFileData.size());
        return false;
    }
    
    // Log the first 8 bytes for debugging
    std::string headerHex;
    for (size_t i = 0; i < std::min(size_t(8), originalFileData.size()); ++i) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", originalFileData[i]);
        headerHex += hex;
        if (i < 7) headerHex += " ";
    }
    Log(DEBUG, "BAM", "BAM header bytes: {}", headerHex);
    
    // Check for compressed BAMC format first
    if (memcmp(originalFileData.data(), "BAMC", 4) == 0) {
        Log(DEBUG, "BAM", "Detected compressed BAMC format");
        wasOriginallyCompressed = true;
        
        // Decompress and then detect the underlying format
        if (!decompressBAMC()) {
            Log(ERROR, "BAM", "Failed to decompress BAMC data");
            return false;
        }
        // Continue with normal format detection on decompressed data
    }
    
    // Check for BAM signature first (bytes 0-3)
    std::string signatureHex;
    for (size_t i = 0; i < 4; ++i) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", originalFileData[i]);
        signatureHex += hex;
        if (i < 3) signatureHex += " ";
    }
    Log(DEBUG, "BAM", "Signature bytes (0-3): {}", signatureHex);
    
    if (memcmp(originalFileData.data(), "BAM ", 4) != 0) {
        Log(ERROR, "BAM", "Invalid BAM signature, expected 'BAM ', got: '{:c}{:c}{:c}{:c}'", 
            originalFileData[0], originalFileData[1], originalFileData[2], originalFileData[3]);
        return false;
    }
    
    // Check version (bytes 4-7)
    std::string versionHex;
    for (size_t i = 4; i < 8; ++i) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", originalFileData[i]);
        versionHex += hex;
        if (i < 7) versionHex += " ";
    }
    Log(DEBUG, "BAM", "Version bytes (4-7): {}", versionHex);
    
    if (memcmp(originalFileData.data() + 4, "V1  ", 4) == 0) {
        Log(DEBUG, "BAM", "Detected BAM V1 format");
        bamFormat = BAMFormat::V1;
        Log(DEBUG, "BAM", "Attempting to deserialize BAM V1 data...");
        Log(DEBUG, "BAM", "Data size: {} bytes", originalFileData.size());
        bool deserializeResult = bamV1File.deserialize(originalFileData);

        if (!deserializeResult) {
            Log(ERROR, "BAM", "BAM V1 deserialization failed");
            Log(DEBUG, "BAM", "BAM V1 file structure - frames: {}, cycles: {}, palette: {}, flt: {}", 
                bamV1File.frameEntries.size(), bamV1File.cycleEntries.size(), 
                bamV1File.palette.size(), bamV1File.frameLookupTable.size());
            
            // Check if we have frame entries to debug the issue
            if (!bamV1File.frameEntries.empty()) {
                const auto& firstFrame = bamV1File.frameEntries[0];
                Log(DEBUG, "BAM", "First frame - width: {}, height: {}, centerX: {}, centerY: {}, dataOffset: 0x{:08x}, isRLE: {}", 
                    firstFrame.width, firstFrame.height, firstFrame.centerX, firstFrame.centerY, 
                    firstFrame.dataOffset, firstFrame.isRLE());
                
                // Check header offsets
                Log(DEBUG, "BAM", "Header offsets - frameEntries: 0x{:08x}, palette: 0x{:08x}, flt: 0x{:08x}", 
                    bamV1File.header.frameEntriesOffset, bamV1File.header.paletteOffset, bamV1File.header.frameLookupTableOffset);
            }
            

        } else {
            Log(DEBUG, "BAM", "BAM V1 deserialization successful - frames: {}, cycles: {}, palette: {}, flt: {}", 
                bamV1File.frameEntries.size(), bamV1File.cycleEntries.size(), 
                bamV1File.palette.size(), bamV1File.frameLookupTable.size());
        }
        return deserializeResult;
    } else if (memcmp(originalFileData.data() + 4, "V2  ", 4) == 0) {
        Log(DEBUG, "BAM", "Detected BAM V2 format");
        bamFormat = BAMFormat::V2;
        Log(DEBUG, "BAM", "Attempting to deserialize BAM V2 data...");
        bool deserializeResult = bamV2File.deserialize(originalFileData);
        if (!deserializeResult) {
            Log(ERROR, "BAM", "BAM V2 deserialization failed");
        }
        return deserializeResult;
    }

    Log(ERROR, "BAM", "Unknown BAM version, expected 'V1  ' or 'V2  ', got: '{:c}{:c}{:c}{:c}'", 
        originalFileData[4], originalFileData[5], originalFileData[6], originalFileData[7]);
    return false;
}

bool BAM::convertBamToPng() {
    // Get extract directory
    std::string extractPath = getExtractDir();
    
    if (isV1Format()) {
        // For V1, we need to extract each frame as a separate PNG
        Log(DEBUG, "BAM", "Extracting BAM V1: {} frames, {} cycles", 
            bamV1File.frameEntries.size(), bamV1File.cycleEntries.size());
        
        // Extract every frame index exactly once, independent of cycles
        for (uint16_t frameIndex = 0; frameIndex < bamV1File.frameEntries.size(); ++frameIndex) {
            const auto& frameEntry = bamV1File.frameEntries[frameIndex];

            // Create frame filename with frame index: frame_X.png
            std::string frameFilename = extractPath + "/frame_" + std::to_string(frameIndex) + ".png";

            // Extract frame data and convert to PNG
            if (!extractBAMV1Frame(frameEntry, frameFilename)) {
                Log(ERROR, "BAM", "Failed to extract frame {} for {} type {}",
                    frameIndex, resourceName_, getPluginName());
                continue;
            }
        }
        
    } else if (isV2Format()) {
        // For V2, we need to extract PVRZ references and frame information
        Log(DEBUG, "BAM", "Extracting BAM V2: {} frames, {} cycles, {} data blocks", 
            bamV2File.frameEntries.size(), bamV2File.cycleEntries.size(), bamV2File.dataBlocks.size());

        // Extract every frame index exactly once, independent of cycles
        for (size_t frameIndex = 0; frameIndex < bamV2File.frameEntries.size(); ++frameIndex) {
            const auto &frameEntry = bamV2File.frameEntries[frameIndex];

            // Create frame filename using absolute frame index for V2
            std::string frameFilename = extractPath + "/frame_" + std::to_string(frameIndex) + ".png";

            // Extract frame from PVRZ texture atlas
            if (!extractBAMV2Frame(frameEntry, frameFilename)) {
                Log(ERROR, "BAM", "Failed to extract frame {}", frameIndex);
                continue;
            }
        }
    }
    
    Log(MESSAGE, "BAM", "Successfully extracted {} frames from BAM", 
        isV1Format() ? bamV1File.frameEntries.size() : bamV2File.frameEntries.size());
    
    return true;
}

bool BAM::convertPngToBam() {
    // Use the detected format to determine which assembly method to use
    if (isV1Format()) {
        return convertPngToBamV1();
    } else if (isV2Format()) {
        return convertPngToBamV2();
    } else {
        Log(ERROR, "BAM", "Unknown BAM format for assembly");
        return false;
    }
}

bool BAM::convertPngToBamV1() {
    std::string upscaledPath = getUpscaledDir();

    // Scan for PNG files (frame_X.png format)
    std::vector<std::string> pngFiles;
    std::map<uint16_t, std::string>
        frameIndexToPng; // actualFrameIndex -> PNG file path

    for (const auto& entry : std::filesystem::directory_iterator(upscaledPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".png") {
            std::string filename = entry.path().filename().string();

            // Parse filename: frame_X.png
            if (filename.find("frame_") == 0) {
              size_t dotPos = filename.find('.', 6);
              if (dotPos != std::string::npos) {
                uint16_t frameIndex = std::stoi(filename.substr(6, dotPos - 6));
                frameIndexToPng[frameIndex] = entry.path().string();
                pngFiles.push_back(entry.path().string());
              }
            }
        }
    }

    if (pngFiles.empty()) {
      Log(ERROR, "BAM", "No valid frame files found in {}", upscaledPath);
      return false;
    }

    Log(DEBUG, "BAM", "Found {} frame files", pngFiles.size());

    // Preserve original cycle structure and FLT
    std::vector<BAMV1CycleEntry> cycles = originalBamV1File.cycleEntries;
    std::vector<uint16_t> frameLookupTable = originalBamV1File.frameLookupTable;

    Log(DEBUG, "BAM",
        "Preserving original cycle structure: {} cycles, {} FLT entries",
        cycles.size(), frameLookupTable.size());

    Log(DEBUG, "BAM", "Converting PNG to BAM V1: {} cycles, {} unique frames",
        cycles.size(), pngFiles.size());

    // Process each PNG file to create frame entries and frame data
    std::vector<BAMV1FrameEntry> frameEntries;
    std::vector<std::vector<uint8_t>> frameData;
    std::vector<uint32_t> allPixels; // For palette creation

    // First pass: collect all pixels for palette creation (sequential)
    for (const auto &pngFile : pngFiles) {
      std::vector<uint32_t> argbPixels;
      int width, height;

      if (!loadPNG(pngFile, argbPixels, width, height)) {
        Log(ERROR, "BAM", "Failed to load PNG: {}", pngFile);
        return false;
      }

      allPixels.insert(allPixels.end(), argbPixels.begin(), argbPixels.end());

      argbPixels.clear();
      argbPixels.shrink_to_fit();
    }

    // Copy original palette and compressed color from source BAM V1
    std::vector<BAMV1PaletteEntry> palette = originalBamV1File.palette;
    if (palette.size() < 256) {
      palette.resize(256);
    }
    uint8_t compressedColor = originalBamV1File.header.compressedColor;
    Log(DEBUG, "BAM", "Using original palette ({} entries) and compressedColor {}",
        palette.size(), compressedColor);

    // Build ARGB palette vector (index 0..255) matching original order; do not force magic green
    std::vector<uint32_t> paletteARGB = convertPaletteToARGB(palette, /*transparentIndex*/ -1);

    // Clear the large pixel collection; no longer needed
    allPixels.clear();
    allPixels.shrink_to_fit();

    // Second pass: create frame entries for ALL original frame indices in-place
    // This preserves original indexing so the FLT remains valid
    size_t originalFrameCount = originalBamV1File.frameEntries.size();
    frameEntries.resize(originalFrameCount);
    frameData.resize(originalFrameCount);
    Log(DEBUG, "BAM", "Creating frame entries to match original count: {}",
        originalFrameCount);

    for (uint16_t frameIndex = 0; frameIndex < originalFrameCount; ++frameIndex) {
      // Look up the PNG file for this frame index
      auto pngIt = frameIndexToPng.find(frameIndex);
      if (pngIt == frameIndexToPng.end()) {
        Log(WARNING, "BAM", "No PNG file found for frame {}", frameIndex);
        // Create empty frame
        BAMV1FrameEntry frameEntry = {};
        frameEntry.width = 1;
        frameEntry.height = 1;
        // Preserve original center coordinates (scaled) even for placeholder frames
        if (frameIndex < originalBamV1File.frameEntries.size()) {
          const auto &originalFrame = originalBamV1File.frameEntries[frameIndex];
          frameEntry.centerX = static_cast<int16_t>(originalFrame.centerX * PIE4K_CFG.UpScaleFactor);
          frameEntry.centerY = static_cast<int16_t>(originalFrame.centerY * PIE4K_CFG.UpScaleFactor);
        } else {
          frameEntry.centerX = 0;
          frameEntry.centerY = 0;
        }
        // dataOffset will be 0 due to {} initialization, let serialize() handle it
        frameEntries[frameIndex] = frameEntry;
        frameData[frameIndex] = std::vector<uint8_t>(1, compressedColor);
        continue;
      }

      const std::string &pngFile = pngIt->second;

      // Load PNG file
      std::vector<uint32_t> argbPixels;
      int width, height;

      if (!loadPNG(pngFile, argbPixels, width, height)) {
        Log(ERROR, "BAM", "Failed to load PNG: {}", pngFile);
        return false;
      }


      // Create frame entry with original center coordinates scaled by upscale factor
      BAMV1FrameEntry frameEntry = {};
      frameEntry.width = static_cast<uint16_t>(width);
      frameEntry.height = static_cast<uint16_t>(height);

      // Use original center coordinates - error if not found (should always exist up to originalFrameCount)
      if (frameIndex < originalBamV1File.frameEntries.size()) {
        const auto &originalFrame = originalBamV1File.frameEntries[frameIndex];
        frameEntry.centerX = static_cast<int16_t>(originalFrame.centerX * PIE4K_CFG.UpScaleFactor);
        frameEntry.centerY = static_cast<int16_t>(originalFrame.centerY * PIE4K_CFG.UpScaleFactor);
        Log(DEBUG, "BAM", "Frame {}: using original center ({}, {}) -> ({}, {})",
            frameIndex, originalFrame.centerX, originalFrame.centerY,
            frameEntry.centerX, frameEntry.centerY);
      } else {
        Log(ERROR, "BAM", "Frame {}: no original center coordinates found", frameIndex);
        return false;
      }

      // dataOffset will be 0 due to {} initialization, let serialize() handle it
      frameEntries[frameIndex] = frameEntry;

      // Convert ARGB to palette indices
      std::vector<uint8_t> framePixels;
      framePixels.reserve(width * height);

      if (!ColorReducer::pixelsToIndicesWithMagicGreen(
              argbPixels, paletteARGB, framePixels)) {
        Log(ERROR, "BAM", "Failed to convert pixels to palette indices");
        return false;
      }

      // Clear the large ARGB vector immediately after conversion
      argbPixels.clear();
      argbPixels.shrink_to_fit();

      frameData[frameIndex] = framePixels;
    }

    // Apply RLE compression to frames if beneficial; use compressedColor from original header
    for (size_t i = 0; i < frameData.size(); i++) {
      std::vector<uint8_t> &framePixels = frameData[i];

      // Apply RLE compression if beneficial
      std::vector<uint8_t> compressedData =
          compressFrameRLE(framePixels, compressedColor);

      if (compressedData.size() < framePixels.size()) {
        frameData[i] = compressedData;
        // Don't set dataOffset here - let serialize() handle it
      } else {
        // Don't set dataOffset here - let serialize() handle it
      }
    }

    // Build the BAM V1 file structure (preserve original frame count)
    bamV1File.header.frameCount = static_cast<uint16_t>(originalFrameCount);
    bamV1File.header.cycleCount = static_cast<uint8_t>(cycles.size());
    bamV1File.header.compressedColor = compressedColor;
    bamV1File.frameEntries = frameEntries;
    bamV1File.cycleEntries = cycles;
    bamV1File.palette = palette;
    bamV1File.frameLookupTable = frameLookupTable;
    bamV1File.frameData = frameData;

    bamFormat = BAMFormat::V1;

    Log(DEBUG, "BAM",
        "Successfully created BAM V1 structure with {} frames, {} cycles",
        frameEntries.size(), cycles.size());

    return true;
}

bool BAM::convertPngToBamV2() {
    std::string upscaledPath = getUpscaledDir();
    
    // Preserve original cycle structure (we only need absolute frame indices for V2)
    std::vector<BAMV1CycleEntry> cycles = originalBamV2File.cycleEntries;

    // Scan for PNG files (frame_X.png format only)
    std::vector<std::string> pngFiles;
    std::map<uint16_t, std::string> frameIndexToPng; // actualFrameIndex -> PNG file path
    
    for (const auto& entry : std::filesystem::directory_iterator(upscaledPath)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".png") {
            continue;
        }
        std::string filename = entry.path().filename().string();

        // Parse filename: frame_X.png
        if (filename.rfind("frame_", 0) == 0) {
            size_t dotPos = filename.find('.', 6);
            if (dotPos != std::string::npos) {
                uint16_t frameIndex = static_cast<uint16_t>(std::stoi(filename.substr(6, dotPos - 6)));
                frameIndexToPng[frameIndex] = entry.path().string();
                pngFiles.push_back(entry.path().string());
            }
        }
    }
    
    if (pngFiles.empty()) {
        Log(ERROR, "BAM", "No valid frame files found in {}", upscaledPath);
        return false;
    }
    
    Log(DEBUG, "BAM", "Found {} frame files", pngFiles.size());
    
    Log(DEBUG, "BAM", "Preserving original cycle structure: {} cycles", cycles.size());
    
    Log(DEBUG, "BAM", "Converting PNG to BAM V2: {} cycles, {} unique frames", cycles.size(), pngFiles.size());
    
    // Create PVRZ texture atlases from PNG files
    PVRZ pvrz;
    std::vector<std::string> pvrzFiles;
    std::vector<BAMV2DataBlock> dataBlocks;
    std::vector<BAMV2FrameEntry> frameEntries;
    
    // Group frames into texture atlases - one frame per PVRZ file for large frames
    size_t pvrzPageCounter = 0; // Separate counter for PVRZ pages
    
    std::string pvrzOriginalExtension = SClass::getExtensionWithDot(IE_PVRZ_CLASS_ID);
    Log(DEBUG, "BAM", "Using PVRZ extension: '{}'", pvrzOriginalExtension);

    // Process frames in the order they appear in the original frame entries
    for (size_t frameIndex = 0; frameIndex < originalBamV2File.frameEntries.size(); frameIndex++) {
        
        // Look up the PNG file for this frame index
        auto pngIt = frameIndexToPng.find(frameIndex);
        if (pngIt == frameIndexToPng.end()) {
            Log(WARNING, "BAM", "No PNG file found for frame {}", frameIndex);
            // Create empty frame entry
            BAMV2FrameEntry frameEntry;
            frameEntry.width = 1;
            frameEntry.height = 1;
            frameEntry.dataBlockStartIndex = static_cast<uint16_t>(dataBlocks.size());
            frameEntry.dataBlockCount = 0; // No data blocks for empty frame
            frameEntries.push_back(frameEntry);
            continue;
        }
        
        const std::string& pngFile = pngIt->second;
        
        // Load frame to get dimensions
        std::vector<uint32_t> argbPixels;
        int width, height;
        if (!loadPNG(pngFile, argbPixels, width, height)) {
            Log(ERROR, "BAM", "Failed to load PNG for frame dimensions: {}", pngFile);
            return false;
        }
        
        // Use proper PVRZ naming convention: MOS{page_number}
        // Each frame gets its own PVRZ page number, starting from 0
        auto [pvrzResourceName, pageNum] = PluginManager::getInstance().generatePVRZName(resourceName_, IE_BAM_CLASS_ID);
        uint32_t pvrzPage = static_cast<uint32_t>(pageNum);
        std::string pvrzPath = getAssembleDir() + "/" + pvrzResourceName + pvrzOriginalExtension;
        
        // Create single-frame PVRZ (one frame per PVRZ file)
        std::vector<std::string> singleFrame;
        singleFrame.push_back(pngFile);
        if (!pvrz.createTextureAtlasPVRZ(singleFrame, pvrzPath, width, height, PVRZFormat::DXT5)) {
            Log(ERROR, "BAM", "Failed to create PVRZ for frame {}", frameIndex);
            return false;
        }
        
        pvrzFiles.push_back(pvrzPath);
        
        // Create data block for this frame
        BAMV2DataBlock dataBlock;
        dataBlock.pvrzPage = pvrzPage;
        dataBlock.sourceX = 0; // Frame fills entire PVRZ
        dataBlock.sourceY = 0;
        dataBlock.width = static_cast<uint32_t>(width);
        dataBlock.height = static_cast<uint32_t>(height);
        dataBlock.targetX = 0;
        dataBlock.targetY = 0;
        dataBlocks.push_back(dataBlock);

        // Create frame entry
        BAMV2FrameEntry frameEntry;
        frameEntry.width = static_cast<uint32_t>(width);
        frameEntry.height = static_cast<uint32_t>(height);
        frameEntry.dataBlockStartIndex = static_cast<uint16_t>(dataBlocks.size() - 1);
        frameEntry.dataBlockCount = 1; // One data block per frame

        // Preserve and scale original centers
        if (frameIndex < originalBamV2File.frameEntries.size()) {
            const auto &orig = originalBamV2File.frameEntries[frameIndex];
            frameEntry.centerX = static_cast<int16_t>(orig.centerX * PIE4K_CFG.UpScaleFactor);
            frameEntry.centerY = static_cast<int16_t>(orig.centerY * PIE4K_CFG.UpScaleFactor);
            Log(DEBUG, "BAM", "V2 Frame {} center ({}, {}) -> ({}, {})", frameIndex,
                orig.centerX, orig.centerY, frameEntry.centerX, frameEntry.centerY);
        }
        frameEntries.push_back(frameEntry);
        
        pvrzPageCounter++;
        
        // Clear the temporary vector to free memory immediately
        argbPixels.clear();
        argbPixels.shrink_to_fit();
    }
    
    Log(DEBUG, "BAM", "Created {} frame entries and {} data blocks", frameEntries.size(), dataBlocks.size());
    
    // Build the BAM V2 file structure
    bamV2File.header.setCounts(static_cast<uint32_t>(frameEntries.size()), static_cast<uint32_t>(cycles.size()), static_cast<uint32_t>(dataBlocks.size()));
    bamV2File.frameEntries = frameEntries;
    bamV2File.cycleEntries = cycles;
    bamV2File.dataBlocks = dataBlocks;
    
    bamFormat = BAMFormat::V2;
    
    Log(MESSAGE, "BAM", "Successfully created BAM V2 structure with {} frames, {} cycles, {} data blocks", 
        frameEntries.size(), cycles.size(), dataBlocks.size());
    
    return true;
}

bool BAM::extractBAMV1Frame(const BAMV1FrameEntry& frameEntry, const std::string& outputPath) {
    // Find the frame data for this frame entry
    size_t frameIndex = &frameEntry - &bamV1File.frameEntries[0];
    if (frameIndex >= bamV1File.frameData.size()) {
        Log(ERROR, "BAM", "Frame index {} out of bounds for frame data", frameIndex);
        return false;
    }
    
    const std::vector<uint8_t>& frameData = bamV1File.frameData[frameIndex];
    
    Log(DEBUG, "BAM", "Frame {}: {}x{}, RLE={}, data size={}, compressed color={}", 
        frameIndex, frameEntry.width, frameEntry.height, frameEntry.isRLE(), 
        frameData.size(), bamV1File.header.compressedColor);
    
    // Decode frame data (RLE or raw)
    std::vector<uint8_t> decodedPixels;
    if (frameEntry.isRLE()) {
        // RLE compressed data - decode it
        decodedPixels = decodeRLEFrame(frameData, frameEntry.width, frameEntry.height, bamV1File.header.compressedColor);
    } else {
        // Raw data - copy directly
        decodedPixels = frameData;
    }
    
    if (decodedPixels.size() != frameEntry.width * frameEntry.height) {
        Log(ERROR, "BAM", "Decoded frame size mismatch: expected {}, got {}", 
            frameEntry.width * frameEntry.height, decodedPixels.size());
        return false;
    }
    
    // Convert palette to ARGB colors, honoring the file's transparent index
    std::vector<uint32_t> argbPalette = convertPaletteToARGB(bamV1File.palette, bamV1File.header.compressedColor);
    
    // Convert frame pixels to ARGB
    std::vector<uint32_t> argbPixels;
    argbPixels.reserve(decodedPixels.size());
    
    for (uint8_t pixelIndex : decodedPixels) {
        if (pixelIndex < argbPalette.size()) {
            argbPixels.push_back(argbPalette[pixelIndex]);
        } else {
            // Invalid palette index - use transparent
            argbPixels.push_back(0x00000000);
        }
    }
    
    // Save as PNG
    if (!savePNG(outputPath, argbPixels, frameEntry.width, frameEntry.height)) {
      Log(ERROR, "BAM", "Failed to save PNG: {}", outputPath);
      return false;
    }
    
    Log(DEBUG, "BAM", "Saved frame {}x{} to {}", frameEntry.width, frameEntry.height, outputPath);
    return true;
}

bool BAM::extractBAMV2Frame(const BAMV2FrameEntry& frameEntry, const std::string& outputPath) {
    Log(DEBUG, "BAM", "Extracting BAM V2 frame: {}x{}, data blocks {}-{}", 
        frameEntry.width, frameEntry.height, frameEntry.dataBlockStartIndex, 
        frameEntry.dataBlockStartIndex + frameEntry.dataBlockCount - 1);
    
    // Create a frame buffer with the frame dimensions
    std::vector<uint32_t> framePixels(frameEntry.width * frameEntry.height, 0);
    
    // Process each data block for this frame
    for (uint16_t blockIndex = 0; blockIndex < frameEntry.dataBlockCount; blockIndex++) {
        size_t dataBlockIdx = frameEntry.dataBlockStartIndex + blockIndex;
        if (dataBlockIdx >= bamV2File.dataBlocks.size()) {
            Log(ERROR, "BAM", "Data block index {} out of bounds for data blocks", dataBlockIdx);
            return false;
        }
        
        const BAMV2DataBlock& dataBlock = bamV2File.dataBlocks[dataBlockIdx];
        
        Log(DEBUG, "BAM", "Processing data block {}: PVRZ page {}, source ({},{}), size {}x{}, target ({},{})", 
            dataBlockIdx, dataBlock.pvrzPage, dataBlock.sourceX, dataBlock.sourceY, 
            dataBlock.width, dataBlock.height, dataBlock.targetX, dataBlock.targetY);
        
        // Load the PVRZ texture atlas
        std::string pvrzResourceName = PluginManager::getInstance().generatePVRZNameInternal(resourceName_, dataBlock.pvrzPage, IE_BAM_CLASS_ID);
        
        PVRZ pvrz;
        std::vector<uint8_t> argbData;
        int atlasWidth, atlasHeight;
        
        if (!pvrz.loadPVRZResourceAsARGB(pvrzResourceName, argbData, atlasWidth, atlasHeight)) {
            Log(ERROR, "BAM", "Failed to load PVRZ resource: {}", pvrzResourceName);
            return false;
        }
        
        Log(DEBUG, "BAM", "Loaded PVRZ atlas: {}x{}", atlasWidth, atlasHeight);
        
        // Ensure source rectangle is within atlas bounds
        if (dataBlock.sourceX + dataBlock.width > static_cast<uint32_t>(atlasWidth) || 
            dataBlock.sourceY + dataBlock.height > static_cast<uint32_t>(atlasHeight)) {
            Log(ERROR, "BAM", "Source rectangle {}x{} out of bounds for atlas {}x{}", 
                dataBlock.width, dataBlock.height, atlasWidth, atlasHeight);
            return false;
        }
        
        // Ensure target rectangle is within frame bounds
        if (dataBlock.targetX + dataBlock.width > frameEntry.width || 
            dataBlock.targetY + dataBlock.height > frameEntry.height) {
            Log(ERROR, "BAM", "Target rectangle {}x{} out of bounds for frame {}x{}", 
                dataBlock.width, dataBlock.height, frameEntry.width, frameEntry.height);
            return false;
        }
        
        // Copy pixels from atlas to frame
        for (uint32_t y = 0; y < dataBlock.height; y++) {
            for (uint32_t x = 0; x < dataBlock.width; x++) {
                // Source position in atlas
                size_t srcIdx = ((dataBlock.sourceY + y) * atlasWidth + (dataBlock.sourceX + x)) * 4;
                if (srcIdx + 3 >= argbData.size()) {
                    Log(ERROR, "BAM", "Source pixel index out of bounds: {}", srcIdx);
                    return false;
                }
                
                // Read ARGB from atlas (4 bytes per pixel: A,R,G,B)
                uint32_t argbColor = (argbData[srcIdx] << 24) |     // A
                                   (argbData[srcIdx + 1] << 16) |   // R
                                   (argbData[srcIdx + 2] << 8) |    // G
                                   argbData[srcIdx + 3];            // B
                
                // Target position in frame
                size_t dstIdx = (dataBlock.targetY + y) * frameEntry.width + (dataBlock.targetX + x);
                if (dstIdx >= framePixels.size()) {
                    Log(ERROR, "BAM", "Target pixel index out of bounds: {}", dstIdx);
                    return false;
                }
                
                framePixels[dstIdx] = argbColor;
            }
        }
        
        // Clear the large ARGB data vector immediately after use
        argbData.clear();
        argbData.shrink_to_fit();
    }
    
    // Save as PNG
    if (!savePNG(outputPath, framePixels, frameEntry.width, frameEntry.height)) {
      Log(ERROR, "BAM", "Failed to save PNG: {}", outputPath);
      return false;
    }
    
    Log(DEBUG, "BAM", "Extracted frame {}x{} to {}", frameEntry.width, frameEntry.height, outputPath);
    return true;
}

std::vector<uint8_t> BAM::decodeRLEFrame(const std::vector<uint8_t>& frameData, uint16_t width, uint16_t height, uint8_t compressedColor) {
    size_t pixelCount = width * height;
    std::vector<uint8_t> decodedPixels(pixelCount, compressedColor); // Initialize with transparent color
    
    Log(DEBUG, "BAM", "RLE decode: {}x{} pixels, {} bytes data, compressed color {}", 
        width, height, frameData.size(), compressedColor);
    
    size_t transQueue = 0;
    size_t pixelIndex = 0;
    size_t dataIndex = 0;
    
    while (pixelIndex < pixelCount && dataIndex < frameData.size()) {
        if (transQueue) {
            // Fill with transparent pixels from queue
            size_t fillCount = std::min(transQueue, pixelCount - pixelIndex);
            std::fill_n(decodedPixels.begin() + pixelIndex, fillCount, compressedColor);
            pixelIndex += fillCount;
            transQueue -= fillCount;
        } else {
            uint8_t px = frameData[dataIndex++];
            if (px == compressedColor) {
                // RLE run of transparent pixels
                if (dataIndex < frameData.size()) {
                    transQueue = std::min<size_t>(1 + frameData[dataIndex++], pixelCount - pixelIndex);
                }
            } else {
                // Raw pixel data
                decodedPixels[pixelIndex++] = px;
            }
        }
    }
    
    Log(DEBUG, "BAM", "RLE decode complete: {} pixels decoded from {} bytes", pixelIndex, dataIndex);
    return decodedPixels;
}

std::vector<uint32_t> BAM::convertPaletteToARGB(const std::vector<BAMV1PaletteEntry>& palette, int transparentIndex) {
    std::vector<uint32_t> argbPalette;
    argbPalette.reserve(palette.size());
    
    for (size_t i = 0; i < palette.size(); i++) {
        const auto& entry = palette[i];
        if (transparentIndex >= 0 && static_cast<size_t>(transparentIndex) == i) {
            argbPalette.push_back(0x00000000); // fully transparent for file's transparent index
        } else {
            // Keep as opaque ARGB (respecting stored alpha if present)
            uint32_t a = (entry.a == 0) ? 0xFF : entry.a;
            uint32_t color = (a << 24) | (entry.r << 16) | (entry.g << 8) | entry.b;
            argbPalette.push_back(color);
        }
    }
    
    return argbPalette;
}

bool BAM::decompressBAMC() {
    // BAMC V1 format: 4-byte signature + 4-byte version + 4-byte uncompressed size + zlib compressed data
    if (originalFileData.size() < 12) {
        Log(ERROR, "BAM", "BAMC file too small for header");
        return false;
    }
    
    // Read uncompressed size from offset 8-11
    uint32_t uncompressedSize;
    memcpy(&uncompressedSize, originalFileData.data() + 8, 4);

    Log(DEBUG, "BAM", "BAMC uncompressed size: {} bytes", uncompressedSize);
    
    // Extract compressed data (skip 12-byte header)
    std::vector<uint8_t> compressedData(originalFileData.begin() + 12, originalFileData.end());
    
    // Decompress using zlib
    std::vector<uint8_t> decompressedData(uncompressedSize);
    uLongf actualDecompressedSize = uncompressedSize;
    
    int result = uncompress(decompressedData.data(), &actualDecompressedSize,
                           compressedData.data(), compressedData.size());
    
    if (result != Z_OK) {
        Log(ERROR, "BAM", "zlib decompression failed with error: {}", result);
        return false;
    }
    
    if (actualDecompressedSize != uncompressedSize) {
        Log(ERROR, "BAM", "Decompressed size mismatch: expected {}, got {}", 
            uncompressedSize, actualDecompressedSize);
        return false;
    }
    
    // Replace the original data with decompressed data
    originalFileData = decompressedData;
    
    // Log the first 8 bytes of decompressed data for debugging
    if (originalFileData.size() >= 8) {
        std::string headerHex;
        for (size_t i = 0; i < 8; ++i) {
            char hex[3];
            snprintf(hex, sizeof(hex), "%02x", originalFileData[i]);
            headerHex += hex;
            if (i < 7) headerHex += " ";
        }
        Log(DEBUG, "BAM", "Decompressed BAM header bytes: {}", headerHex);
    }
    
    Log(DEBUG, "BAM", "Successfully decompressed BAMC data: {} bytes", originalFileData.size());
    return true;
}

uint32_t BAM::colorDistance(uint8_t gray, const BAMV1PaletteEntry& paletteEntry) {
    // Convert palette entry to grayscale for comparison
    uint8_t paletteGray = (paletteEntry.r + paletteEntry.g + paletteEntry.b) / 3;
    
    // Calculate distance (simple absolute difference)
    int diff = static_cast<int>(gray) - static_cast<int>(paletteGray);
    return static_cast<uint32_t>(diff * diff);
}

std::vector<uint8_t> BAM::compressFrameRLE(const std::vector<uint8_t>& framePixels, uint8_t compressedColor) {
    std::vector<uint8_t> compressed;
    compressed.reserve(framePixels.size()); // Worst case: no compression
    
    size_t i = 0;
    while (i < framePixels.size()) {
        if (framePixels[i] == compressedColor) {
            // Count consecutive transparent pixels
            size_t count = 0;
            while (i + count < framePixels.size() && framePixels[i + count] == compressedColor && count < 255) {
                count++;
            }
            
            if (count > 2) {
                // RLE compression is beneficial
                compressed.push_back(compressedColor);
                compressed.push_back(static_cast<uint8_t>(count - 1));
                i += count;
            } else {
                // Not worth compressing
                for (size_t j = 0; j < count; j++) {
                    compressed.push_back(compressedColor);
                }
                i += count;
            }
        } else {
            // Non-transparent pixel
            compressed.push_back(framePixels[i]);
            i++;
        }
    }
    
    return compressed;
}

std::vector<uint8_t> BAM::compressBAMC(const std::vector<uint8_t>& data) const {
    // BAMC V1 format: 4-byte signature + 4-byte version + 4-byte uncompressed size + zlib compressed data
    
    // Compress using zlib
    uLongf compressedSize = compressBound(data.size());
    std::vector<uint8_t> compressedData(compressedSize);
    
    int result = compress(compressedData.data(), &compressedSize,
                         data.data(), data.size());
    
    if (result != Z_OK) {
        Log(ERROR, "BAM", "zlib compression failed with error: {}", result);
        return data; // Return original if compression fails
    }
    
    // Resize to actual compressed size
    compressedData.resize(compressedSize);
    
    // Create BAMC header
    std::vector<uint8_t> bamcData;
    bamcData.reserve(12 + compressedData.size());
    
    // Signature: "BAMC"
    bamcData.insert(bamcData.end(), {'B', 'A', 'M', 'C'});
    
    // Version: "V1 "
    bamcData.insert(bamcData.end(), {'V', '1', ' ', ' '});
    
    // Uncompressed size (4 bytes, little-endian)
    uint32_t uncompressedSize = static_cast<uint32_t>(data.size());
    bamcData.insert(bamcData.end(), 
                   reinterpret_cast<uint8_t*>(&uncompressedSize),
                   reinterpret_cast<uint8_t*>(&uncompressedSize) + 4);
    
    // Compressed data
    bamcData.insert(bamcData.end(), compressedData.begin(), compressedData.end());
    
    Log(DEBUG, "BAM", "Compressed {} bytes to {} bytes (BAMC V1)", 
        data.size(), bamcData.size());
    
    return bamcData;
}

// PluginBase interface implementation
bool BAM::extractAll() {
  return PluginManager::getInstance().extractAllResourcesOfType(
      IE_BAM_CLASS_ID);
}

bool BAM::upscaleAll() {
  return PluginManager::getInstance().upscaleAllResourcesOfType(
      IE_BAM_CLASS_ID);
}

bool BAM::assembleAll() {
  return PluginManager::getInstance().assembleAllResourcesOfType(
      IE_BAM_CLASS_ID);
}

bool BAM::cleanExtractDirectory() {
    Log(DEBUG, "BAM", "Cleaning extract directory for resource: {}", resourceName_);
    return cleanDirectory(getExtractDir(false));
}

bool BAM::cleanUpscaleDirectory() {
    Log(DEBUG, "BAM", "Cleaning upscale directory for resource: {}", resourceName_);
    return cleanDirectory(getUpscaledDir(false));
}

bool BAM::cleanAssembleDirectory() {
    Log(DEBUG, "BAM", "Cleaning assemble directory for resource: {}", resourceName_);
    return cleanDirectory(getAssembleDir(false));
}

bool BAM::cleanDirectory(const std::string& dir) {
    if (std::filesystem::exists(dir)) {
        try {
            std::filesystem::remove_all(dir);
            Log(DEBUG, "BAM", "Cleaned directory: {}", dir);
            return true;
        } catch (const std::filesystem::filesystem_error& e) {
            Log(ERROR, "BAM", "Failed to clean directory {}: {}", dir, e.what());
            return false;
        }
    }
    return true; // Directory doesn't exist, nothing to clean
}

void BAM::registerCommands(CommandTable& commandTable) {
    commandTable["bam"] = {
        "BAM file operations",
        {
            {"extract", {
                "Extract BAM resource to PNG frames (e.g., bam extract btnhor)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: bam extract <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().extractResource(args[0], IE_BAM_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {
                "Upscale BAM frames (e.g., bam upscale btnhor)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: bam upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().upscaleResource(args[0], IE_BAM_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {
                "Assemble PNG frames into BAM file (e.g., bam assemble btnhor)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: bam assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().assembleResource(args[0], IE_BAM_CLASS_ID) ? 0 : 1;
                }
            }}
        }
    };
}

// Auto-register the BAM plugin
REGISTER_PLUGIN(BAM, IE_BAM_CLASS_ID);

} // namespace ProjectIE4k
