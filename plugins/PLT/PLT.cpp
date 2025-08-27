#include "PLT.h"

#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/Logging/Logging.h"
#include "plugins/CommandRegistry.h"
#include <opencv2/imgcodecs.hpp>

namespace fs = std::filesystem;

namespace ProjectIE4k {

// Auto-register the PLT plugin
REGISTER_PLUGIN(PLT, IE_PLT_CLASS_ID);

PLT::PLT(const std::string &resourceName_)
    : PluginBase(resourceName_, IE_PLT_CLASS_ID) {
  if (originalFileData.empty()) {
    Log(DEBUG, "PLT", "No data loaded for PLT resource: {}", resourceName_);
    return;
  }

  try {
    pltFile.deserialize(originalFileData);
    valid_ = true;
  } catch (const std::exception &e) {
    Log(ERROR, "PLT", "Failed to parse PLT: {}", e.what());
    valid_ = false;
  }
}

PLT::~PLT() {
  // nothing special
}

bool PLT::extract() {
  Log(MESSAGE, "PLT", "Starting PLT extraction for resource: {}",
      resourceName_);
  return convertPltToPng();
}

bool PLT::assemble() {
  Log(MESSAGE, "PLT", "Starting PLT assembly for resource: {}", resourceName_);

  // For now, simply re-serialize the in-memory pltFile (round-trip when paired
  // with a PNG-to-PLT later)
  try {
    std::vector<uint8_t> data = pltFile.serialize();
    std::string outPath =
        getAssembleDir(true) + "/" + originalFileName;
    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) {
      Log(ERROR, "PLT", "Could not create file: {}", outPath);
      return false;
    }
    out.write(reinterpret_cast<const char *>(data.data()), data.size());
    return true;
  } catch (const std::exception &e) {
    Log(ERROR, "PLT", "Serialize failed: {}", e.what());
    return false;
  }
}

// Paths
std::string PLT::getOutputDir(bool ensureDir) const {
  return constructPath("-plt", ensureDir);
}
std::string PLT::getExtractDir(bool ensureDir) const {
  std::string path =
      getOutputDir(ensureDir) + "/" + extractBaseName() + "-plt-extracted";
  if (ensureDir)
    ensureDirectoryExists(path);
  return path;
}
std::string PLT::getUpscaledDir(bool ensureDir) const {
  std::string path =
      getOutputDir(ensureDir) + "/" + extractBaseName() + "-plt-upscaled";
  if (ensureDir)
    ensureDirectoryExists(path);
  return path;
}
std::string PLT::getAssembleDir(bool ensureDir) const {
  std::string path =
      getOutputDir(ensureDir) + "/" + extractBaseName() + "-plt-assembled";
  if (ensureDir)
    ensureDirectoryExists(path);
  return path;
}

// Batch operations are handled by PluginManager; keep simple stubs here
bool PLT::extractAll() { return false; }
bool PLT::upscaleAll() { return false; }
bool PLT::assembleAll() { return false; }

// Cleaning
bool PLT::cleanDirectory(const std::string &dir) {
  try {
    if (fs::exists(dir)) {
      for (auto &p : fs::directory_iterator(dir)) {
        fs::remove_all(p.path());
      }
    }
    ensureDirectoryExists(dir);
    return true;
  } catch (const std::exception &e) {
    Log(ERROR, "PLT", "Failed to clean directory {}: {}", dir, e.what());
    return false;
  }
}

bool PLT::cleanExtractDirectory() {
  return cleanDirectory(getExtractDir(true));
}
bool PLT::cleanUpscaleDirectory() {
  return cleanDirectory(getUpscaledDir(true));
}
bool PLT::cleanAssembleDirectory() {
  return cleanDirectory(getAssembleDir(true));
}

// Commands (optional for now)
void PLT::registerCommands(CommandTable &commandTable) {
  commandTable["plt"] = {
      "PLT file operations",
      {
          {"extract",
           {"Extract PLT resource to png (e.g., plt extract paperdoll)",
            [](const std::vector<std::string> &args) -> int {
              if (args.empty()) {
                std::cerr << "Usage: plt extract <resource_name>" << std::endl;
                return 1;
              }
              return ProjectIE4k::PluginManager::getInstance().extractResource(
                         args[0], IE_PLT_CLASS_ID)
                         ? 0
                         : 1;
            }}},
          {"upscale",
           {"Upscale PLT png",
            [](const std::vector<std::string> &args) -> int {
              if (args.empty()) {
                std::cerr << "Usage: plt upscale <resource_name>" << std::endl;
                return 1;
              }
              return ProjectIE4k::PluginManager::getInstance().upscaleResource(
                         args[0], IE_PLT_CLASS_ID)
                         ? 0
                         : 1;
            }}},
          {"assemble",
           {"Assemble PLT from working data (e.g., plt assemble paperdoll)",
            [](const std::vector<std::string> &args) -> int {
              if (args.empty()) {
                std::cerr << "Usage: plt assemble <resource_name>" << std::endl;
                return 1;
              }
              return ProjectIE4k::PluginManager::getInstance().assembleResource(
                         args[0], IE_PLT_CLASS_ID)
                         ? 0
                         : 1;
            }}},
      }};
}

// Convert PLT palette-index pairs to RGBA via MPAL256.bmp and save as PNG
bool PLT::convertPltToPng() {
  if (!valid_) {
    Log(ERROR, "PLT", "PLT file not loaded or invalid");
    return false;
  }

  const uint32_t width = pltFile.header.width;
  const uint32_t height = pltFile.header.height;
  if (width == 0 || height == 0) {
    Log(ERROR, "PLT", "Invalid dimensions: {}x{}", width, height);
    return false;
  }

  // Decode MPAL256.bmp from in-memory resource data provided by PluginBase
  // Expected palette image: 256x256 (columns x rows)
  cv::Mat palBgr = cv::imdecode(colorPaletteData, cv::IMREAD_UNCHANGED);
  if (palBgr.empty()) {
    Log(ERROR, "PLT", "Failed to decode in-memory MPAL256 palette data");
    return false;
  }
  const int palW = palBgr.cols;
  const int palH = palBgr.rows;
  if (palW < 1 || palH < 1) {
    Log(ERROR, "PLT", "Invalid palette image dimensions");
    return false;
  }

  std::vector<uint32_t> argb(width * height, 0u);
  // File stores pixels left->right, bottom->top
  for (uint32_t fileYFromBottom = 0; fileYFromBottom < height;
       ++fileYFromBottom) {
    const uint32_t fileY =
        height - 1 - fileYFromBottom; // convert to top->bottom row for output
    for (uint32_t x = 0; x < width; ++x) {
      const uint64_t idx = static_cast<uint64_t>(fileYFromBottom) *
                               static_cast<uint64_t>(width) +
                           x;
      const PLTV1Pixel p = pltFile.pixels[static_cast<size_t>(idx)];

      const int col = static_cast<int>(p.column);
      const int row = static_cast<int>(p.row);
      if (col < 0 || col >= palW || row < 0 || row >= palH) {
        // Guard against malformed inputs
        continue;
      }
      const cv::Vec4b bgra =
          palBgr.channels() == 4
              ? palBgr.at<cv::Vec4b>(row, col)
              : cv::Vec4b(palBgr.at<cv::Vec3b>(row, col)[0],
                          palBgr.at<cv::Vec3b>(row, col)[1],
                          palBgr.at<cv::Vec3b>(row, col)[2], 255);
      const uint8_t b = bgra[0];
      const uint8_t g = bgra[1];
      const uint8_t r = bgra[2];
      const uint8_t a = bgra[3];

      argb[static_cast<size_t>(fileY) * static_cast<size_t>(width) +
           static_cast<size_t>(x)] =
          (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
          (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    }
  }

  std::string outputDir = getExtractDir(true);
  std::string outputPath = outputDir + "/" + resourceName_ + ".png";
  if (!savePNG(outputPath, argb, static_cast<int>(width),
               static_cast<int>(height))) {
    Log(ERROR, "PLT", "Failed to save PNG file: {}", outputPath);
    return false;
  }
  Log(DEBUG, "PLT", "Saved PNG {}", outputPath);
  return true;
}

} // namespace ProjectIE4k
