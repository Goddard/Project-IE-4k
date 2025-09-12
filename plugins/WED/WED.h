#pragma once

#include <string>

#include "core/SClassID.h"
#include "plugins/PluginBase.h"
#include "plugins/CommandRegistry.h"
#include "WEDV1.3.hpp"
#include "WEDV1.4.hpp"

namespace ProjectIE4k {

/**
 * @class WED
 * Main class for WED file operations (extract, upscale, and assemble)
 * Follows the same pattern as other plugins for consistency
 */
class WED : public PluginBase {
public:
    WED(const std::string& resourceName = "");
    ~WED() override;
    
    // Core operations (required by PluginBase)
    bool extract() override;
    bool upscale() override;
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
    std::string getPluginName() const override { return "WED"; }
    SClass_ID getResourceType() const override { return IE_WED_CLASS_ID; }

    // Path management - WED-specific paths
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;

    // Register plugin commands
    static void registerCommands(CommandTable& commandTable);
    
    // Save to file
    bool saveToFile(const std::string& filePath) const;
    bool saveToFile13(const std::string& filePath) const;

    bool upscale13();
    
    // File data access
    const WEDFile& getWedFile13() const { return wedFile13; }
    WEDFile& getWedFile13() { return wedFile13; }
    // File data access (use V1.4 in-memory; deserializer supports V1.3 and V1.4)
    const WEDFileV14& getWedFile() const { return wedFile; }
    WEDFileV14& getWedFile() { return wedFile; }

private:
    WEDFileV14 wedFile;
    WEDFile wedFile13;

    // Helper methods
    bool upscaleTileMap();
    
    // Loading methods
    bool loadFromData();
    
    // Utility methods
    bool cleanDirectory(const std::string& dir);
};

} // namespace ProjectIE4k

