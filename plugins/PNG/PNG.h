#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "core/SClassID.h"
#include "plugins/PluginBase.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

class PNG : public PluginBase {
public:
    PNG(const std::string& resourceName);
    ~PNG() override;

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
    std::string getPluginName() const override { return "PNG"; }
    SClass_ID getResourceType() const override { return IE_PNG_CLASS_ID; }

    // Path management - PNG-specific paths
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;

    // Register plugin commands
    static void registerCommands(CommandTable& commandTable);

private:
    bool valid_ = false;
    
    bool detectFormat();
    bool convertPngToPng();
    
    // Palette file handling
    bool isPaletteFile() const;
    
    // Directory cleaning helper
    bool cleanDirectory(const std::string& dir);
};

} // namespace ProjectIE4k
