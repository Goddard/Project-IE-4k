#ifndef ARE_H
#define ARE_H

#include "core/SClassID.h"

#include "AREV1.hpp"
#include "plugins/PluginBase.h"

namespace ProjectIE4k {

class ARE : public PluginBase {
public:
    ARE(const std::string& resourceName);
    ~ARE() override;

    // Core operations
    bool extract() override;
    bool assemble() override;
    bool upscale() override;

    // Command registration (static method)
    static void registerCommands(CommandTable& commandTable);

    // Getters
    std::string getResourceName() const override { return resourceName_; }
    bool isValid() const override { return valid_; }
    
    // Batch operations
    bool extractAll() override { return false; }
    bool upscaleAll() override { return false; }
    bool assembleAll() override { return false; }

    bool cleanExtractDirectory() override;
    bool cleanUpscaleDirectory() override;
    bool cleanAssembleDirectory() override;

    // Plugin metadata
    std::string getPluginName() const override { return "ARE"; }
    SClass_ID getResourceType() const override { return IE_ARE_CLASS_ID; };

    // Path management
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;

    // ARE-specific methods
    bool saveToFile(const std::string& filePath) const;

private:
    AREFile areFile;
    bool cleanDirectory(const std::string& dir);
};

} // namespace ProjectIE4k

#endif // ARE_H
