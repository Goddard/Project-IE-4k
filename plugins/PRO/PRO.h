#ifndef PRO_H
#define PRO_H

#include <string>

#include "core/SClassID.h"
#include "plugins/PluginBase.h"
#include "plugins/CommandRegistry.h"
#include "PROV1.hpp"

namespace ProjectIE4k {

class PRO : public PluginBase {
public:
    explicit PRO(const std::string& resourceName);
    ~PRO() override;

    // Core operations (required by PluginBase)
    bool extract() override;
    bool upscale() override;   // Projectiles typically do not need “AI upscaling”; treat as transform/no-op
    bool assemble() override;

    // PluginBase interface implementation
    std::string getResourceName() const override { return resourceName_; }
    bool isValid() const override { return valid_; }

    // Batch operations (implemented by PluginManager)
    bool extractAll() override { return PluginManager::getInstance().extractAllResourcesOfType(IE_PRO_CLASS_ID); }
    bool upscaleAll() override { return PluginManager::getInstance().upscaleAllResourcesOfType(IE_PRO_CLASS_ID); }
    bool assembleAll() override { return PluginManager::getInstance().assembleAllResourcesOfType(IE_PRO_CLASS_ID); }

    // Clean directories before operations - operation-specific
    bool cleanExtractDirectory() override;
    bool cleanUpscaleDirectory() override;
    bool cleanAssembleDirectory() override;

    // Plugin metadata
    std::string getPluginName() const override { return "PRO"; }
    SClass_ID getResourceType() const override { return IE_PRO_CLASS_ID; }

    // Path management - PRO-specific paths
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;

    // Register plugin commands
    static void registerCommands(CommandTable& commandTable);

private:
    bool valid_ = false;

    // Parsed data
    PROV1File proFile;

    // Helper methods
    bool detectFormat();
    bool cleanDirectory(const std::string& dir);
};

} // namespace ProjectIE4k

#endif // PRO_H