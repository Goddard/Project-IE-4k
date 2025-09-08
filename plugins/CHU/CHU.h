#ifndef CHU_H
#define CHU_H

#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "plugins/PluginBase.h"
#include "plugins/CommandRegistry.h"
#include "core/SClassID.h"
#include "CHUV1.hpp"

namespace ProjectIE4k {

/**
 * @class CHU
 * Main class for CHU file operations (extract, upscale, and assemble)
 * Follows the same pattern as other plugins for consistency
 */
class CHU : public PluginBase {
public:
    CHU(const std::string& resourceName = "");
    ~CHU() override;
    
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
    std::string getPluginName() const override { return "CHU"; }
    SClass_ID getResourceType() const override { return IE_CHU_CLASS_ID; }

    // Path management - CHU-specific paths
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;

    // Register plugin commands
    static void registerCommands(CommandTable& commandTable);
    
    // Header access
    CHUHeader& getHeader() { return header; }
    const CHUHeader& getHeader() const { return header; }
    
    // Utility methods
    const std::vector<uint8_t>& getFileData() const { return originalFileData; }

    // Data access
    const std::vector<CHUWindow>& getWindows() const { static const std::vector<CHUWindow> empty; return v1_ ? v1_->windows : empty; }
    const std::vector<CHUControlTableEntry>& getControlTable() const { static const std::vector<CHUControlTableEntry> empty; return v1_ ? v1_->controlTable : empty; }
    // Controls will be handled as a variant/union in implementation

private:
    // Parsed V1 container (preferred over ad-hoc parsing)
    std::unique_ptr<CHUV1File> v1_;
    CHUHeader header; // retained for legacy helpers/compare
    
    // Helper methods
    bool loadFromData();
    bool readHeader();
    bool writeHeader(std::ofstream& file);
    
    // Utility methods
    bool cleanDirectory(const std::string& dir);
};

} // namespace ProjectIE4k

#endif // CHU_H