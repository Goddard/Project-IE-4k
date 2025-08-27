#ifndef CFG_H
#define CFG_H

#pragma once

#include <string>
#include <vector>

namespace ProjectIE4k {

class CFG {
    public:
    std::string configFilePath;

    std::string GamePath;
    std::string GameType;
    bool Logging;
    int UpScaleFactor;
    bool RetainCache;
    std::string upscalerModel;
    std::string MOSUpscalerModel;
    std::string BAMUpscalerModel;
    std::string BMPUpscalerModel;
    std::string PNGUpscalerModel;
    std::string TISUpscalerModel;
    std::string PLTUpscalerModel;
    std::vector<std::string> ResourceKnownBad;
    double MaxCPU;
    double MaxRAM;
    double MaxGPU;
    double MaxVRAM;
    std::string NCNNModelPath;

    CFG();

    void initialize(const std::string& configFile);
    void setGamePath(const std::string& path) { GamePath = path; }
    const std::string& getGamePath() const { return GamePath; }
    void setLogging(bool enabled) { Logging = enabled; }
    bool getLogging() const { return Logging; }
    std::string getGameType() const;
    std::string getGameOverridePath() const;
    bool isUpScaleFactorValid() const;
    std::string
    getUpscalerModelByResourceType(const std::string &resourceType = "") const;
    bool isResourceKnownBad(const std::string& resourceName) const;
    const std::string& getUpscalerModel() const { return upscalerModel; }
    void setUpscalerModel(const std::string& m) { upscalerModel = m; }
    static std::string findConfigFile();

private:
    CFG(const CFG&) = delete;
    CFG& operator=(const CFG&) = delete;
    
    // Helper methods
    static std::vector<std::string> splitCommaSeparated(const std::string& input);
};

// Global variable declaration always use this
extern CFG PIE4K_CFG;

} // namespace ProjectIE4k

#endif // CFG_H
 