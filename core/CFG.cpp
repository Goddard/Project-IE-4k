#include "CFG.h"

#include <sstream>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <iostream>
#include <cstdlib>

#include "ConfigParser.h"
#include "Logging/Logging.h"

namespace ProjectIE4k {

CFG::CFG()
    : GamePath(""), GameType(""), Logging(true), UpScaleFactor(1),
      RetainCache(false), upscalerModel("upscayl-ultrasharp-v2"),
      MOSUpscalerModel(""), BAMUpscalerModel(""), BMPUpscalerModel(""),
      PNGUpscalerModel(""), TISUpscalerModel(""), PLTUpscalerModel(""),
      MaxCPU(100.0), MaxRAM(95.0), MaxGPU(100.0), MaxVRAM(90.0), NCNNModelPath("models/ncnn/") {
        if(!isUpScaleFactorValid()) {
          UpScaleFactor = 4;
          Log(WARNING, "CFG", "Invalid UpScaleFactor set, using 4 instead");
        }
      }

void CFG::initialize(const std::string& configFile) {
    configFilePath = configFile;
    ConfigParser config;
    
    if (!config.loadFromFile(configFile)) {
        // If config file doesn't exist or can't be loaded, use defaults
        Log(WARNING, "Config", "Could not load config file: {}, using defaults", configFile);
    }
    
    auto upscaleStr = config.get("UpScaleFactor", "1");
    UpScaleFactor = atoi(upscaleStr.c_str());

    auto upscalerModelStr = config.get("UpscalerModel", "");
    upscalerModel = upscalerModelStr.empty() ? "upscayl-ultrasharp-v2" : upscalerModelStr;
    
    // Read resource-specific upscaler models
    auto mosModelStr = config.get("MOSUpscalerModel", "");
    MOSUpscalerModel = mosModelStr.empty() ? upscalerModel : mosModelStr;
    
    auto bamModelStr = config.get("BAMUpscalerModel", "");
    BAMUpscalerModel = bamModelStr.empty() ? upscalerModel : bamModelStr;
    
    auto bmpModelStr = config.get("BMPUpscalerModel", "");
    BMPUpscalerModel = bmpModelStr.empty() ? upscalerModel : bmpModelStr;
    
    auto pngModelStr = config.get("PNGUpscalerModel", "");
    PNGUpscalerModel = pngModelStr.empty() ? upscalerModel : pngModelStr;
    
    auto tisModelStr = config.get("TISUpscalerModel", "");
    TISUpscalerModel = tisModelStr.empty() ? upscalerModel : tisModelStr;

    auto pltModelStr = config.get("PLTUpscalerModel", "");
    PLTUpscalerModel = tisModelStr.empty() ? upscalerModel : pltModelStr;

    // Read logging setting
    auto loggingStr = config.get("Logging", "1");
    Logging = loggingStr == "1";

    // Read retain cache setting
    auto retainCacheStr = config.get("RetainCache", "0");
    RetainCache = retainCacheStr == "1";

    // Read game path and type
    GamePath = config.get("GamePath", "");
    GameType = config.get("GameType", "");

    // Read resource limits
    auto maxCPUStr = config.get("MaxCPU", "100");
    MaxCPU = std::stod(maxCPUStr);
    
    auto maxRAMStr = config.get("MaxRAM", "95");
    MaxRAM = std::stod(maxRAMStr);
    
    auto maxGPUStr = config.get("MaxGPU", "100");
    MaxGPU = std::stod(maxGPUStr);
    
    auto maxVRAMStr = config.get("MaxVRAM", "90");
    MaxVRAM = std::stod(maxVRAMStr);

    // Read usable GPU IDs as a comma-separated list (e.g., "0,1,2")
    UsableGPUIDs.clear();
    auto usableGpuStr = config.get("UsableGPUIDs", "");
    if (!usableGpuStr.empty()) {
        auto parts = splitCommaSeparated(usableGpuStr);
        for (const auto &p : parts) {
            try {
                int id = std::stoi(p);
                if (id >= 0) {
                    UsableGPUIDs.push_back(id);
                } else {
                    Log(WARNING, "Config", "Ignoring negative GPU id '{}' in UsableGPUIDs", p);
                }
            } catch (...) {
                Log(WARNING, "Config", "Invalid GPU id '{}' in UsableGPUIDs; skipping", p);
            }
        }
        Log(DEBUG, "Config", "Loaded {} usable GPU id(s) from config", UsableGPUIDs.size());
    } else {
        UsableGPUIDs.push_back(0);
        Log(DEBUG, "Config", "UsableGPUIDs not specified; defaulting to [0]");
    }
    
    // Read known bad resources list and split into vector
    auto resourceKnownBadStr = config.get("ResourceKnownBad", "");
    ResourceKnownBad = splitCommaSeparated(resourceKnownBadStr);
}

bool CFG::isUpScaleFactorValid() const {
    static const int validFactors[] = {1, 2, 4, 6, 8, 12};
    for (int v : validFactors) {
        if (UpScaleFactor == v) return true;
    }
    return false;
}

std::string
CFG::getUpscalerModelByResourceType(const std::string &resourceType) const {
  std::string modelName;

  // Determine which model to use based on resource type
  if (resourceType == "MOS" && !MOSUpscalerModel.empty()) {
    modelName = MOSUpscalerModel;
  } else if (resourceType == "BAM" && !BAMUpscalerModel.empty()) {
    modelName = BAMUpscalerModel;
  } else if (resourceType == "BMP" && !BMPUpscalerModel.empty()) {
    modelName = BMPUpscalerModel;
  } else if (resourceType == "PNG" && !PNGUpscalerModel.empty()) {
    modelName = PNGUpscalerModel;
  } else if (resourceType == "TIS" && !TISUpscalerModel.empty()) {
    modelName = TISUpscalerModel;
  } else if (resourceType == "PLT" && !PLTUpscalerModel.empty()) {
    modelName = PLTUpscalerModel;
  } else {
    // Default to the main upscaler model
    modelName = upscalerModel.empty() ? "upscayl-ultrasharp-v2" : upscalerModel;
  }

  // Check if the model files exist in the models directory
  std::string modelPath = NCNNModelPath + modelName;
  std::string paramFile = modelPath + ".param";
  std::string binFile = modelPath + ".bin";

  if (!std::filesystem::exists(paramFile)) {
    Log(ERROR, "Config", "Model parameter file not found: {}", paramFile);
    std::exit(1);
  }

  if (!std::filesystem::exists(binFile)) {
    Log(ERROR, "Config", "Model binary file not found: {}", binFile);
    std::exit(1);
  }

  return modelName;
}

std::string CFG::getGameType() const {
    return GameType;
}

std::string CFG::getGameOverridePath() const {
    // Construct override path based on game path
    if (GamePath.empty()) {
        return "";
    }
    
    std::filesystem::path gamePath(GamePath);
    std::filesystem::path overridePath = gamePath / "override";
    
    return overridePath.string();
}

// Helper function to auto-detect config file
std::string CFG::findConfigFile() {
    try {
        std::vector<std::string> configFiles;
        for (const auto &entry : std::filesystem::directory_iterator(".")) {
            if (entry.is_regular_file() && entry.path().extension() == ".cfg") {
                configFiles.push_back(entry.path().string());
            }
        }
        if (!configFiles.empty()) {
            std::sort(configFiles.begin(), configFiles.end());
            return configFiles[0];
        }
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "Error scanning for config files: " << e.what() << std::endl;
    }
    return "";
}

// Helper method to split comma-separated strings
std::vector<std::string> CFG::splitCommaSeparated(const std::string& input) {
    std::vector<std::string> result;
    if (input.empty()) {
        return result;
    }
    
    std::stringstream ss(input);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // Trim whitespace
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

bool CFG::isResourceKnownBad(const std::string& resourceName) const {
    return std::find(ResourceKnownBad.begin(), ResourceKnownBad.end(), resourceName) != ResourceKnownBad.end();
}

// Define the global variable
CFG PIE4K_CFG;

} // namespace ProjectIE4k