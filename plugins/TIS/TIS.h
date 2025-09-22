#ifndef TIS_H
#define TIS_H

#include <cstdint>
#include <string>
#include <vector>

#include "TISV1.hpp"
#include "TISV2.hpp"
#include "core/SClassID.h"
#include "plugins/CommandRegistry.h"
#include "plugins/PluginBase.h"

namespace ProjectIE4k {

/**
 * @class TIS
 * Main class for TIS file operations (extract and assemble)
 * Follows the same pattern as MOS class for consistency
 */
class TIS : public PluginBase {
public:
    TIS(const std::string& resourceName = "");
    ~TIS() override;

    // Core operations (required by PluginBase)
    bool extract() override;
    bool assemble() override;

    // PluginBase interface implementation
    std::string getResourceName() const override { return resourceName_; }
    bool isValid() const override { return valid_; }
    
    // Batch operations (delegated to PluginManager)
    bool extractAll() override;
    bool upscaleAll() override;
    bool assembleAll() override;

    // Clean directories before operations - operation-specific
    bool cleanExtractDirectory() override;
    bool cleanUpscaleDirectory() override;
    bool cleanAssembleDirectory() override;

    // Plugin metadata
    std::string getPluginName() const override { return "TIS"; }
    SClass_ID getResourceType() const override { return IE_TIS_CLASS_ID; }

    // Path management - TIS-specific paths
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;

    // Register plugin commands
    static void registerCommands(CommandTable& commandTable);

    // Data access
    const std::vector<uint8_t>& getFileData() const { return originalFileData; }
    
private:
    // Original File data
    std::vector<uint8_t> originalTisFileData;
    std::vector<uint8_t> originalWedFileData;

    // File extensions
    std::string tisOriginalExtension;
    std::string pvrzOriginalExtension;
    std::string wedOriginalExtension;
    
    // File format detection
    bool isPvrzBased = false;
    
    // V1 (palette-based) data
    TISV1File tisV1File;
    
    // V2 (PVRZ-based) data  
    TISV2File tisV2File;
    
    // WED layout information
    int tilesPerRow;
    int tilesPerColumn;
    
    // Additional PVRZ files referenced in WED
    std::vector<std::string> additionalPvrzResources;
    bool saveSeconaryAsPng = false;
    
    // Helper methods
    bool detectFormat();
    bool convertTisToPng();
    bool convertPngToTis();
    bool convertPngToTisV1Palette();
    bool createTile(const std::vector<uint32_t>& tilePixels, TISV1Tile& tile);
    bool processTileBatch(int batchStart, int batchEnd, int totalTilesPerRow,
                          const std::vector<uint32_t> &pixels, int imageWidth,
                          TISV1File &tisFile);
    std::vector<uint32_t> extractTilePixels(int tileX, int tileY, int tileWidth, int tileHeight);
    void calculateGridFromTileCount();
    void extractSeconaryTilesAsPngs(TISV2File &tisV2File, uint16_t &maxTileIndex);
    
    // WED parsing methods
    bool parseWEDFile();
    bool extractAdditionalPVRZFiles();
    
    // Utility methods
    bool cleanDirectory(const std::string& dir);
};

} // namespace ProjectIE4k

#endif // TIS_H
