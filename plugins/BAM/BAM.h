#ifndef BAM_H
#define BAM_H

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/SClassID.h"
#include "plugins/CommandRegistry.h"
#include "plugins/PluginBase.h"
#include "BAMV1.hpp"
#include "BAMV2.hpp"

namespace ProjectIE4k {
/**
 * @class BAM
 * Main class for BAM file operations (extract and assemble)
 * Follows the same pattern as TIS and MOS classes for consistency
 */
class BAM : public PluginBase {
public:
    BAM(const std::string& resourceName);
    ~BAM() override;
    
    // Core operations (required)
    bool extract() override;
    // bool upscale() override;
    bool assemble() override;
    
    // PluginBase interface implementation
    std::string getResourceName() const override { return resourceName_; }
    bool isValid() const override { return valid_; }
    
    // Batch operations (implemented by PluginManager)
    bool extractAll() override;
    bool upscaleAll() override;
    bool assembleAll() override;
    
    // Clean directories before operations - operation-specific
    bool cleanExtractDirectory() override;
    bool cleanUpscaleDirectory() override;
    bool cleanAssembleDirectory() override;
    
    // Plugin metadata
    std::string getPluginName() const override { return "BAM"; }
    SClass_ID getResourceType() const override { return IE_BAM_CLASS_ID; }
    
    // Path management - BAM-specific paths
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractOutputDir(bool ensureDir) const;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;
    
    // Format detection
    bool isV1Format() const { return bamFormat == BAMFormat::V1; }
    bool isV2Format() const { return bamFormat == BAMFormat::V2; }
    
    // File access
    bool saveToFile(const std::string& filePath) const;
    
    // Register plugin commands (static method)
    static void registerCommands(CommandTable& commandTable);
    
private:
    enum class BAMFormat {
        UNKNOWN,
        V1,
        V2
    };
    
    // BAM format detection
    BAMFormat bamFormat;
    bool wasOriginallyCompressed;  // Track if original was BAMC compressed
    bool valid_ = false;
    
    // BAM file objects
    BAMV1File bamV1File;
    BAMV2File bamV2File;

    // Original File Structures
    BAMV1File originalBamV1File;
    BAMV2File originalBamV2File;
    
    // Helper methods
    bool loadData();
    bool detectFormat();
    bool convertBamToPng();
    bool convertPngToBam();
    bool convertPngToBamV1();
    bool convertPngToBamV2();
    
    // BAM V2 extraction methods
    bool extractBAMV2Frame(const BAMV2FrameEntry& frameEntry, const std::string& outputPath);

    // BAM V1 extraction methods
    bool extractBAMV1Frame(const BAMV1FrameEntry& frameEntry, const std::string& outputPath);
    std::vector<uint8_t> decodeRLEFrame(const std::vector<uint8_t>& frameData, uint16_t width, uint16_t height, uint8_t compressedColor);
    std::vector<uint32_t> convertPaletteToARGB(const std::vector<BAMV1PaletteEntry>& palette, int transparentIndex = -1);
    
    // Helper functions for assembly
    uint32_t colorDistance(uint8_t gray, const BAMV1PaletteEntry& paletteEntry);
    std::vector<uint8_t> compressFrameRLE(const std::vector<uint8_t>& framePixels, uint8_t compressedColor);
    
    // Compression handling
    bool decompressBAMC();
    std::vector<uint8_t> compressBAMC(const std::vector<uint8_t>& data) const;
    
    // Directory cleaning helper
    bool cleanDirectory(const std::string& dir);
};

} // namespace ProjectIE4k

#endif // BAM_H

