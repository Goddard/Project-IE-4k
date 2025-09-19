#ifndef BMP_H
#define BMP_H

#include <string>

#include "core/SClassID.h"
#include "plugins/PluginBase.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

class BMP : public PluginBase {
public:
    BMP(const std::string& resourceName);
    ~BMP() override;

    // Core operations (required by PluginBase)
    bool extract() override;
    bool upscale() override;  // Override for area map handling
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
    std::string getPluginName() const override { return "BMP"; }
    SClass_ID getResourceType() const override { return IE_BMP_CLASS_ID; }

    // Path management - BMP-specific paths
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;

    // Register plugin commands
    static void registerCommands(CommandTable& commandTable);

private:
    bool valid_ = false;

    // Helper methods
    bool detectFormat();
    bool convertBmpToPng();
    bool convertPngToBmp();
    
    // Area map handling
    bool isAreaMapFile() const;
    bool extractAreaMapBmp();
    bool upscaleAreaMapBmp();
    bool assembleAreaMapBmp();
    bool skipUpscaling();
    bool skipPngToBmp();
    
    // Multi-resolution handling (L/M/S pattern)
    bool isMultiResolutionSet() const;
    std::string getBaseName() const;
    std::string getResolutionSuffix() const;
    
    // Directory cleaning helper
    bool cleanDirectory(const std::string& dir);
};

} // namespace ProjectIE4k

#endif // BMP_H
