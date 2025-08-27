#ifndef MOS_H
#define MOS_H

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

#include "core/SClassID.h"
#include "plugins/PluginBase.h"
#include "plugins/PVRZ/PVRZ.h"
#include "plugins/CommandRegistry.h"
#include "MOSV1.hpp"
#include "MOSV2.hpp"

namespace ProjectIE4k {

/**
 * @class MOS
 * Main class for MOS file operations (extract and assemble)
 * Follows the same pattern as other plugins for consistency
 */
class MOS : public PluginBase {
public:
    MOS(const std::string& resourceName = "");
    ~MOS() override;

    // Core operations (required by PluginBase)
    bool extract() override;
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
    std::string getPluginName() const override { return "MOS"; }
    SClass_ID getResourceType() const override { return IE_MOS_CLASS_ID; }

    // Path management - MOS-specific paths
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;

    // Register plugin commands
    static void registerCommands(CommandTable& commandTable);

private:
    // --- Private Data Structures & Enums ---

    // Simple structure to hold image data
    struct ImageData {
        std::vector<uint32_t> pixels;
        int width;
        int height;
        bool hasAlpha;
    };

    // --- Private Helper Methods ---

    // Zlib Compression
    std::vector<uint8_t> compressZlib(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decompressZlib(const std::vector<uint8_t>& compressedData, uint32_t expectedSize);

    // Conversion Logic
    bool convertPngToMosV1();
    bool convertMosToPng();
    bool convertPngToMosV2();
    bool convertMosV2ToPng();

    // File format detection
    bool readSigAndVer(const std::vector<uint8_t>& fileData);
    bool detectFormat();

    // Member variables
    char referenceSignature[8];
    bool compressed = false;
    bool isV2 = false;
    std::vector<uint8_t> originalMosFileData;
    PVRZ pvrzCreator;
    
    // Directory cleaning helper
    bool cleanDirectory(const std::string& dir);
};

} // namespace ProjectIE4k

#endif // MOS_H