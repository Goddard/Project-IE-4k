#pragma once

#include <string>
#include <vector>

#include "plugins/PluginBase.h"
#include "core/SClassID.h"
#include "WMPV1.hpp"

namespace ProjectIE4k {

class WMP : public PluginBase {
public:
    WMP(const std::string& resourceName = "");
    ~WMP() override;

    // Core operations
    bool extract() override;
    bool upscale() override;   // currently no-op (pass-through)
    bool assemble() override;  // pass-through copy

    // Batch operations via PluginManager
    bool extractAll() override;
    bool upscaleAll() override;
    bool assembleAll() override;

    // Cleaning
    bool cleanExtractDirectory() override;
    bool cleanUpscaleDirectory() override;
    bool cleanAssembleDirectory() override;

    // Metadata
    std::string getPluginName() const override { return "WMP"; }
    SClass_ID getResourceType() const override { return IE_WMP_CLASS_ID; }
    std::string getResourceName() const override { return resourceName_; }
    bool isValid() const override { return valid_; }

    // Paths
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;

    // Commands
    static void registerCommands(CommandTable& commandTable);

    // Data access
    const WMPV1File& getParsed() const { return wmp_; }

private:
    bool detectFormat();
    bool loadFromData();
    bool cleanDirectory(const std::string& dir);

    WMPV1File wmp_;
};

} // namespace ProjectIE4k
