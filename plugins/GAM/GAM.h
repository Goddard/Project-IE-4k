#ifndef GAM_H
#define GAM_H

#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "plugins/PluginBase.h"
#include "plugins/CommandRegistry.h"
#include "core/SClassID.h"
#include "GAMV1.1.hpp"
#include "GAMV2.0.hpp"
#include "GAMV2.2.hpp"

namespace ProjectIE4k {

/**
 * @class GAM
 * Main class for GAM file operations (extract, upscale, and assemble)
 */
class GAM : public PluginBase {
public:
    explicit GAM(const std::string& resourceName = "");
    ~GAM() override;

    // Core operations
    bool extract() override;
    bool upscale() override;   // No-op: writes original to upscaled dir for pipeline consistency
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
    std::string getPluginName() const override { return "GAM"; }
    SClass_ID getResourceType() const override { return IE_GAM_CLASS_ID; }

    // Path management - GAM-specific paths
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;

    // Register plugin commands
    static void registerCommands(CommandTable& commandTable);

    // Header access helpers
    const std::string& getDetectedVersion() const { return detectedVersion_; }

private:
    // Minimal parsed header info for diagnostics
    std::string detectedVersion_;

    // Helpers
    bool loadFromData();
    bool readHeader();
    bool writeFile(const std::string& path, const std::vector<uint8_t>& data) const;
    bool cleanDirectory(const std::string& dir);

    // V1.1 handling
    bool parseV11();
    std::vector<uint8_t> serializeV11() const;
    std::unique_ptr<GAMV11File> v11_;

    // V2.0 handling
    bool parseV20();
    std::vector<uint8_t> serializeV20() const;
    std::unique_ptr<GAMV20File> v20_;

    // V2.2 handling
    bool parseV22();
    std::vector<uint8_t> serializeV22() const;
    std::unique_ptr<GAMV22File> v22_;

public:
};

} // namespace ProjectIE4k

#endif // GAM_H


