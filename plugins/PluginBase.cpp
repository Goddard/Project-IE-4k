#include "PluginBase.h"

#include <algorithm>
#include <filesystem>

#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include "core/SClassID.h"
#include "services/ServiceManager.h"

namespace fs = std::filesystem;

namespace ProjectIE4k {

// Static command registry
CommandTable& PluginBase::getCommandRegistry() {
    static CommandTable commandRegistry;
    return commandRegistry;
}

PluginBase::PluginBase(const std::string& resourceName, SClass_ID resourceType) : resourceName_(resourceName) {
    // Handle empty resource names (during static initialization)
    if (resourceName_.empty()) {
        Log(DEBUG, "PluginBase", "Empty resource name - likely static initialization, skipping resource loading");
        return;
    }
    
    // Extract resource data from the service
    auto* resourceCoordinator = dynamic_cast<ResourceCoordinatorService*>(
        ServiceManager::getService("ResourceCoordinatorService"));
    
    if (!resourceCoordinator) {
        Log(ERROR, "PluginBase", "ResourceCoordinatorService not found");
        return;
    }
    
    ResourceData resourceData = resourceCoordinator->getResourceData(resourceName, resourceType);
    if (resourceData.data.empty()) {
        Log(ERROR, "PluginBase", "Failed to extract resource data for {} (type: {})", resourceName, resourceType);
        return;
        // Resource not found - output to console and exit
        // std::cout << "Resource '" << resourceName << "' not found, or
        // invalid."
        //   << std::endl;
        // exit(1);
    }

    originalFileName = resourceData.filename;
    originalFileData = resourceData.data;
    originalExtension = getOriginalExtension(resourceData.filename);

    std::string paletteName = "MPAL256";
    SClass_ID paletteType = IE_BMP_CLASS_ID;
    if (PIE4K_CFG.GameType == "demo") {
      paletteName = "pal16";
      paletteType = IE_PNG_CLASS_ID;
    }

    ResourceData paletteData =
        resourceCoordinator->getResourceData(paletteName, paletteType);
    if (paletteData.data.empty()) {
      Log(ERROR, "PluginBase",
          "Failed to extract palette data for {} (type: {})", paletteName,
          paletteType);
      // return;
      // Resource not found - output to console and exit
      std::cout << "Palette '" << paletteName << "' not found, or invalid."
                << std::endl;
      exit(1);
    }

    colorPaletteData = paletteData.data;

    Log(DEBUG, "PluginBase", "Loaded resource from service: {} (type: {}) - {} bytes", resourceName, resourceType, resourceData.data.size());
}

PluginBase::~PluginBase() {
    originalFileData.clear();
    originalFileData.shrink_to_fit();
}

std::vector<uint8_t> PluginBase::loadResourceFromService(const std::string& resourceName, SClass_ID resourceType) {
    Log(DEBUG, "PluginBase", "loadResourceFromService called for: {} (type: {})", resourceName, resourceType);

    // Get the ResourceCoordinatorService from the ServiceManager
    auto* resourceCoordinator = dynamic_cast<ResourceCoordinatorService*>(
        ServiceManager::getService("ResourceCoordinatorService"));
    
    if (!resourceCoordinator) {
        Log(ERROR, "PluginBase", "ResourceCoordinatorService not found");
        return std::vector<uint8_t>();
    }
    
    // Check if resource exists
    if (!resourceCoordinator->hasResource(resourceName, resourceType)) {
        Log(WARNING, "PluginBase", "Resource not found: {} (type: {})", resourceName, resourceType);
        return std::vector<uint8_t>();
    }

    // Extract resource data from the service
    ResourceData resourceData = resourceCoordinator->getResourceData(resourceName, resourceType);
    if (resourceData.data.empty()) {
        Log(ERROR, "PluginBase", "Failed to extract resource data for {} (type: {})", resourceName, resourceType);
        return std::vector<uint8_t>();
    }

    Log(DEBUG, "PluginBase", "Loaded resource from service: {} (type: {}) - {} bytes", resourceName, resourceType, resourceData.data.size());
    
    return resourceData.data;
}

bool PluginBase::validateResourceName() const {
    if (resourceName_.empty()) {
        Log(ERROR, "PluginBase", "Resource name is empty");
        return false;
    }
    return true;
}

void PluginBase::logOperation(const std::string& operation, bool success) const {
    if (success) {
        Log(DEBUG, "PluginBase", "Successfully completed {} for resource: {}", operation, resourceName_);
    } else {
        Log(ERROR, "PluginBase", "Failed to complete {} for resource: {}", operation, resourceName_);
    }
}

std::string PluginBase::constructPath(const std::string& suffix, bool ensureDir) const {    
    std::string path = "output/" + PIE4K_CFG.GameType + "/x" + std::to_string(PIE4K_CFG.UpScaleFactor) + "/" + extractBaseName() + suffix;
    
    if (ensureDir) {
        ensureDirectoryExists(path);
    }
    
    return path;
}

// TODO : move extract outside of upscale dir
// extract is unmodified so no reason to have multiple copies
std::string PluginBase::constructExtractPath(const std::string& suffix, bool ensureDir) const {    
    std::string path = "output/" + PIE4K_CFG.GameType + "/" + extractBaseName() + suffix;
    
    if (ensureDir) {
        ensureDirectoryExists(path);
    }
    
    return path;
}

std::string PluginBase::getOriginalExtension(std::string& filename) {
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos) {
        return filename.substr(dotPos);
    }
    return "";
}

std::string PluginBase::extractBaseName() const {
    fs::path path(resourceName_);
    std::string filename = path.stem().string();
    
    // Get all supported extensions from SClassID.h
    const auto& resourceMap = SClass::getResourceTypeMap();
    std::string lowerFilename = filename;
    std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);
    
    for (const auto& [id, info] : resourceMap) {
        std::string ext = info.extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (lowerFilename.length() > ext.length() && 
            lowerFilename.substr(lowerFilename.length() - ext.length()) == ext) {
            // Remove the extension
            filename = filename.substr(0, filename.length() - ext.length());
            break;
        }
    }
    
    return filename;
}

void PluginBase::ensureDirectoryExists(const std::string& path) const {
    if (!(fs::exists(path) && fs::is_directory(path))) {
        if (!createDirectory(path)) {
            Log(ERROR, "PluginBase", "Failed to create directory: {}", path);
        } else {
            Log(DEBUG, "PluginBase", "Created directory: {}", path);
        }
    }
}

bool PluginBase::createDirectory(const std::string& path) const {
    try {
        return fs::create_directories(path);
    } catch (const std::exception& e) {
        Log(ERROR, "PluginBase", "Failed to create directory {}: {}", path, e.what());
        return false;
    }
}

bool PluginBase::upscale() {
    // Get the UpscalerService from ServiceManager
    auto* upscalerService = ServiceManager::getService("UpscalerService");
    if (!upscalerService) {
        Log(ERROR, "PluginBase", "UpscalerService not available for resource: {}", resourceName_);
        return false;
    }
    
    try {
        // Get the upscale directories
        std::string inputDir = getExtractDir(false);  // Read from extracted frames
        std::string outputDir = getUpscaledDir(true);  // Write to upscaled directory
        
        Log(DEBUG, "PluginBase", "Upscaling resource {} from {} to {}", resourceName_, inputDir, outputDir);
        
        // Initialize the service for this resource type
        upscalerService->initializeForResourceType(getResourceType());
        
        // Use the service interface properly - no need to cast or access underlying implementation
        return upscalerService->upscaleDirectory(inputDir, outputDir);
        
    } catch (const std::exception& e) {
        Log(ERROR, "PluginBase", "Exception during upscaling of {}: {}", resourceName_, e.what());
        return false;
    }
}

void PluginBase::registerPluginFactory(SClass_ID resourceType, 
                                      std::function<std::unique_ptr<PluginBase>(const std::string&)> factory) {
    Log(DEBUG, "PluginBase", "Registering plugin factory for resource type: {}", resourceType);
    PluginManager::getInstance().registerPlugin(resourceType, factory);
}

bool PluginBase::loadPNG(const std::string& filename, std::vector<uint32_t>& pixels, int& width, int& height) const {
    FILE* file = fopen(filename.c_str(), "rb");
    if (!file) {
        Log(ERROR, "PluginBase", "Cannot open file: {}", filename);
        return false;
    }
    
    // Check PNG signature
    uint8_t header[8];
    if (fread(header, 1, 8, file) != 8) {
        Log(ERROR, "PluginBase", "Cannot read PNG header from: {}", filename);
        fclose(file);
        return false;
    }
    
    if (png_sig_cmp(header, 0, 8)) {
        Log(ERROR, "PluginBase", "File is not a PNG file: {}", filename);
        fclose(file);
        return false;
    }
    
    // Create PNG read struct
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        fclose(file);
        return false;
    }
    
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        fclose(file);
        return false;
    }
    
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        fclose(file);
        return false;
    }
    
    png_init_io(png_ptr, file);
    png_set_sig_bytes(png_ptr, 8);
    png_read_info(png_ptr, info_ptr);
    
    // Get image info
    width = png_get_image_width(png_ptr, info_ptr);
    height = png_get_image_height(png_ptr, info_ptr);
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    
    // Convert to RGBA 8-bit
    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }
    
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }
    
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }
    
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
    }
    
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    }
    
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }
    
    png_read_update_info(png_ptr, info_ptr);
    
    // Read image data
    std::vector<png_bytep> row_pointers(height);
    std::vector<uint8_t> image_data(width * height * 4);
    
    for (int y = 0; y < height; y++) {
        row_pointers[y] = &image_data[y * width * 4];
    }
    
    png_read_image(png_ptr, row_pointers.data());
    png_read_end(png_ptr, nullptr);
    
    // Convert to ARGB format
    pixels.resize(width * height);
    for (int i = 0; i < width * height; i++) {
        uint8_t r = image_data[i * 4 + 0];
        uint8_t g = image_data[i * 4 + 1];
        uint8_t b = image_data[i * 4 + 2];
        uint8_t a = image_data[i * 4 + 3];
        pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    
    // Clean up PNG structures
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    fclose(file);
    
    // Clear the large temporary image data immediately
    image_data.clear();
    image_data.shrink_to_fit();
    
    return true;
}

bool PluginBase::savePNG(const std::string& filename, const std::vector<uint32_t>& pixels, int width, int height) const {
    // libpng doesn't handle writing 0x0 files well
    if (width <= 0 || height <= 0) {
        Log(WARNING, "PluginBase", "Invalid image dimensions for saving: {}x{}", width, height);
        return false;
    }
    
    // Check for potential memory overflow
    size_t expectedPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (expectedPixels > 2147483648) { // 2 Gig limit safety measure
        Log(ERROR, "PluginBase", "Image too large for saving: {}x{} ({} pixels)", width, height, expectedPixels);
        return false;
    }
    
    // Verify pixel data size matches dimensions
    if (pixels.size() != expectedPixels) {
        Log(ERROR, "PluginBase", "Pixel data size mismatch: expected {}, got {}", expectedPixels, pixels.size());
        return false;
    }
    
    FILE* file = fopen(filename.c_str(), "wb");
    if (!file) {
        Log(ERROR, "PluginBase", "Cannot create file: {}", filename);
        return false;
    }
    
    // Create PNG write struct
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        fclose(file);
        return false;
    }
    
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, nullptr);
        fclose(file);
        return false;
    }
    
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(file);
        return false;
    }
    
    png_init_io(png_ptr, file);
    
    // Set image info
    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    
    png_write_info(png_ptr, info_ptr);
    
    // Convert from ARGB to RGBA and write
    std::vector<uint8_t> row_data(width * 4);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t pixel = pixels[y * width + x];
            row_data[x * 4 + 0] = (pixel >> 16) & 0xFF; // R
            row_data[x * 4 + 1] = (pixel >> 8) & 0xFF;  // G
            row_data[x * 4 + 2] = pixel & 0xFF;         // B
            row_data[x * 4 + 3] = (pixel >> 24) & 0xFF; // A
        }
        png_write_row(png_ptr, row_data.data());
    }
    
    png_write_end(png_ptr, nullptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(file);
    
    return true;
}

bool PluginBase::loadPNGRows(
    const std::string &filename,
    const std::function<bool(int width, int height, int rowIndex, const std::vector<uint32_t> &argbRow)>
        &onRow) const {
  FILE *file = fopen(filename.c_str(), "rb");
  if (!file) {
    Log(ERROR, "PluginBase", "Cannot open file: {}", filename);
    return false;
  }

  uint8_t header[8];
  if (fread(header, 1, 8, file) != 8 || png_sig_cmp(header, 0, 8)) {
    Log(ERROR, "PluginBase", "Not a PNG: {}", filename);
    fclose(file);
    return false;
  }

  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_ptr) {
    fclose(file);
    return false;
  }
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    fclose(file);
    return false;
  }
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    fclose(file);
    return false;
  }
  png_init_io(png_ptr, file);
  png_set_sig_bytes(png_ptr, 8);
  png_read_info(png_ptr, info_ptr);

  int width = png_get_image_width(png_ptr, info_ptr);
  int height = png_get_image_height(png_ptr, info_ptr);
  png_byte color_type = png_get_color_type(png_ptr, info_ptr);
  png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

  if (bit_depth == 16) {
    png_set_strip_16(png_ptr);
  }
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png_ptr);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand_gray_1_2_4_to_8(png_ptr);
  }
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
    png_set_tRNS_to_alpha(png_ptr);
  }
  if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(png_ptr);
  }

  png_read_update_info(png_ptr, info_ptr);

  std::vector<uint8_t> row_rgba(static_cast<size_t>(width) * 4);
  std::vector<uint32_t> row_argb(static_cast<size_t>(width));

  for (int y = 0; y < height; ++y) {
    png_read_row(png_ptr, row_rgba.data(), nullptr);
    for (int x = 0; x < width; ++x) {
      uint8_t r = row_rgba[x * 4 + 0];
      uint8_t g = row_rgba[x * 4 + 1];
      uint8_t b = row_rgba[x * 4 + 2];
      uint8_t a = row_rgba[x * 4 + 3];
      row_argb[x] = (static_cast<uint32_t>(a) << 24) |
                    (static_cast<uint32_t>(r) << 16) |
                    (static_cast<uint32_t>(g) << 8) |
                    static_cast<uint32_t>(b);
    }
    if (!onRow(width, height, y, row_argb)) {
      png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
      fclose(file);
      return false;
    }
  }

  png_read_end(png_ptr, nullptr);
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  fclose(file);
  return true;
}



} // namespace ProjectIE4k 