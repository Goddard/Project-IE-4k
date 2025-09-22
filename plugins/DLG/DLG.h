#ifndef DLG_H
#define DLG_H

#include "core/SClassID.h"

#include "DLGV1.hpp"
#include "plugins/PluginBase.h"
#include "plugins/BCS/BcsDecompiler.h"
#include "plugins/BCS/BCSCompiler.h"

namespace ProjectIE4k {

class DLG : public PluginBase {
public:
    DLG(const std::string& resourceName);
    ~DLG() override;

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
    std::string getPluginName() const override { return "DLG"; }
    SClass_ID getResourceType() const override { return IE_DLG_CLASS_ID; };

    // Path management
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;
    
    // Shared resource management
    bool initializeSharedResources() override;
    void cleanupSharedResources() override;
    bool hasSharedResources() const override { return true; }

    // DLG-specific methods
    bool saveToFile(const std::string& filePath);

private:
    DLGFile dlgFile;
    bool cleanDirectory(const std::string& dir);
    
    // BCS script processing components
    std::unique_ptr<BcsDecompiler> decompiler_;
    std::unique_ptr<BCSCompiler> compiler_;
    bool decompilerInitialized_ = false;
    bool compilerInitialized_ = false;
    
    // BCS script processing methods
    bool ensureDecompilerInitialized();
    bool ensureCompilerInitialized();
    bool hasCoordinates(const std::string& scriptString);
    std::string stripComments(const std::string& scriptString);
    void upscaleScriptString(std::string& scriptString, int upscaleFactor);
};

} // namespace ProjectIE4k

#endif // DLG_H
