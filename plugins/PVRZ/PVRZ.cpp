#include "PVRZ.h"

#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <zlib.h>
#include <climits>

#include <filesystem>

#include <SDL2/SDL_image.h>

#include "core/Logging/Logging.h"
#include "services/ServiceManager.h"
#include "core/SClassID.h"
#include "services/ResourceService/ResourceCoordinatorService.h"
#include "services/ResourceService/ResourceTypes.h"

namespace ProjectIE4k {

PVRZ::PVRZ() {
}

PVRZ::~PVRZ() {
}

bool PVRZ::createTextureAtlasPVRZ(const std::vector<std::string>& pngFiles,
                                        const std::string& outputPath,
                                        int atlasWidth, int atlasHeight,
                                        PVRZFormat format) {
    if (pngFiles.empty()) {
        Log(ERROR, "PVRZ", "No PNG files provided for texture atlas");
        return false;
    }
    
    // Debug: Log which atlas is being created
    Log(DEBUG, "PVRZ", "=== CREATING ATLAS: {} ===", outputPath);
    Log(DEBUG, "PVRZ", "Atlas contains {} PNG files:", pngFiles.size());
    for (size_t i = 0; i < pngFiles.size(); ++i) {
        Log(DEBUG, "PVRZ", "  [{}] {}", i, pngFiles[i]);
    }
    
    // Ensure atlas dimensions are power of 2
    if (!isValidPowerOfTwo(atlasWidth) || !isValidPowerOfTwo(atlasHeight)) {
        atlasWidth = nextPowerOfTwo(atlasWidth);
        atlasHeight = nextPowerOfTwo(atlasHeight);
        Log(MESSAGE, "PVRZ", "Adjusted atlas dimensions to {}x{}", atlasWidth, atlasHeight);
    }
    
    // Create texture atlas from PNG files
    std::vector<uint8_t> atlasData;
    std::vector<std::pair<int, int>> framePositions;
    
    if (!createAtlasFromPNGs(pngFiles, atlasData, atlasWidth, atlasHeight, framePositions, nullptr)) {
        Log(ERROR, "PVRZ", "Failed to create texture atlas");
        return false;
    }
    
    // Compress atlas to specified format
    std::vector<uint8_t> compressedData = compressToDXT(atlasData, atlasWidth, atlasHeight, format);
    if (compressedData.empty()) {
        Log(ERROR, "PVRZ", "Failed to compress texture atlas");
        return false;
    }
    
    // Create PVR header data in memory first
    std::vector<uint8_t> pvrData;
    
    // PVR header (expects the entire PVR file to be zlib-compressed)
    uint32_t signature = 0x03525650; // 'PVR\x03' little-endian
    uint32_t flags = 0; // ignored
    uint64_t formatField = (format == PVRZFormat::DXT1) ? 7 : 11; // DXT1=7, DXT5=11
    uint32_t colorSpace = 0; // ignored
    uint32_t channelType = 0; // 0: normalized unsigned byte
    uint32_t height = static_cast<uint32_t>(atlasHeight);
    uint32_t width = static_cast<uint32_t>(atlasWidth);
    uint32_t depth = 1;
    uint32_t numFaces = 1;
    uint32_t numSurfaces = 1;
    uint32_t numMipmaps = 1;
    uint32_t metaDataSize = 0;
    
    // Build complete PVR file in memory (header + DXT data)
    pvrData.resize(52 + compressedData.size()); // 52 bytes header + DXT data
    size_t offset = 0;
    
    std::memcpy(&pvrData[offset], &signature, 4); offset += 4;
    std::memcpy(&pvrData[offset], &flags, 4); offset += 4;
    std::memcpy(&pvrData[offset], &formatField, 8); offset += 8;
    std::memcpy(&pvrData[offset], &colorSpace, 4); offset += 4;
    std::memcpy(&pvrData[offset], &channelType, 4); offset += 4;
    std::memcpy(&pvrData[offset], &height, 4); offset += 4;
    std::memcpy(&pvrData[offset], &width, 4); offset += 4;
    std::memcpy(&pvrData[offset], &depth, 4); offset += 4;
    std::memcpy(&pvrData[offset], &numFaces, 4); offset += 4;
    std::memcpy(&pvrData[offset], &numSurfaces, 4); offset += 4;
    std::memcpy(&pvrData[offset], &numMipmaps, 4); offset += 4;
    std::memcpy(&pvrData[offset], &metaDataSize, 4); offset += 4;
    std::memcpy(&pvrData[offset], compressedData.data(), compressedData.size());
    
    // Now zlib-compress the entire PVR file (header + DXT data)
    std::vector<uint8_t> zlibData = compressWithZlib(pvrData);
    
    // Write the zlib-compressed PVR file directly
    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "PVRZ", "Failed to create output file: {}", outputPath);
        return false;
    }
    file.write(reinterpret_cast<const char*>(zlibData.data()), zlibData.size());
    file.close();
    
    // Log file size
    std::ifstream checkFile(outputPath, std::ios::binary | std::ios::ate);
    if (checkFile.is_open()) {
        std::streamsize fileSize = checkFile.tellg();
        Log(DEBUG, "PVRZ", "Wrote {} bytes zlib-compressed PVR file (original PVR: {} bytes), file size on disk: {} bytes", zlibData.size(), pvrData.size(), fileSize);
        checkFile.close();
    } else {
        Log(ERROR, "PVRZ", "Could not open {} to check file size after writing", outputPath);
    }
    
    return true;
}

std::vector<uint8_t> PVRZ::compressWithZlib(const std::vector<uint8_t>& data) {
    uLongf compressedSize = compressBound(data.size());
    // Per the reference code, the final file needs a 4-byte header with the uncompressed size
    std::vector<uint8_t> compressedData(compressedSize + 4); 

    // Write the 4-byte uncompressed size header (little-endian)
    uint32_t uncompressedSize = static_cast<uint32_t>(data.size());
    compressedData[0] = uncompressedSize & 0xFF;
    compressedData[1] = (uncompressedSize >> 8) & 0xFF;
    compressedData[2] = (uncompressedSize >> 16) & 0xFF;
    compressedData[3] = (uncompressedSize >> 24) & 0xFF;

    // Compress the data into the buffer *after* the 4-byte header
    // Use default compression level (6) to match original file format
    int result = compress2(compressedData.data() + 4, &compressedSize,
                          data.data(), data.size(), Z_DEFAULT_COMPRESSION);
    
    if (result != Z_OK) {
        Log(ERROR, "PVRZ", "Zlib compression failed: {}", result);
        return std::vector<uint8_t>();
    }
    
    // Resize the vector to the final size (4-byte header + compressed data size)
    compressedData.resize(compressedSize + 4);
    Log(DEBUG, "PVRZ", "Zlib compression: {} -> {} bytes (including 4-byte header)", data.size(), compressedData.size());
    return compressedData;
}

// Check if we need alpha interpolation (DXT5) or can use DXT1
bool PVRZ::needsAlphaInterpolation(const std::vector<uint8_t>& argbData, int width, int height) {
    for (int i = 0; i < width * height; i++) {
        int srcIdx = i * 4;
        uint8_t alpha = argbData[srcIdx]; // Alpha is first byte in ARGB format
        // Check for intermediate alpha values (between 0x20 and 0xE0)
        // Full transparency (0x00) and full opacity (0xFF) can be handled by DXT1
        if (alpha > 0x20 && alpha < 0xE0) {
            return true;
        }
    }
    return false;
}

std::vector<uint8_t> PVRZ::compressToDXT(const std::vector<uint8_t>& argbData, 
                                               int width, int height, 
                                               PVRZFormat format) {
    // Auto-detect format if AUTO is specified
    if (format == PVRZFormat::AUTO) {
        bool useDXT5 = needsAlphaInterpolation(argbData, width, height);
        format = useDXT5 ? PVRZFormat::DXT5 : PVRZFormat::DXT1;
        Log(DEBUG, "PVRZ", "Auto-detected format: {} (alpha interpolation: {})", 
            format == PVRZFormat::DXT5 ? "DXT5" : "DXT1", useDXT5);
    }
    
    Log(DEBUG, "PVRZ", "Compressing {}x{} image to {}", width, height, 
        (format == PVRZFormat::DXT1) ? "DXT1" : "DXT5");
    
    int blockWidth = (width + 3) / 4;
    int blockHeight = (height + 3) / 4;
    
    if (format == PVRZFormat::DXT1) {
        std::vector<uint8_t> compressedData(blockWidth * blockHeight * 8);
        
        for (int by = 0; by < blockHeight; ++by) {
            for (int bx = 0; bx < blockWidth; ++bx) {
                uint32_t block[16];
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        int srcX = bx * 4 + x;
                        int srcY = by * 4 + y;
                        if (srcX < width && srcY < height) {
                            int srcIdx = (srcY * width + srcX) * 4;
                            const uint8_t* p = &argbData[srcIdx];
                            // Read ARGB format (converted from SDL BGRA)
                            block[y * 4 + x] = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
                        } else {
                            block[y * 4 + x] = 0;
                        }
                    }
                }
                
                int blockIndex = (by * blockWidth + bx) * 8;
                encodeDXT1Block(&block[0], &compressedData[blockIndex]);
            }
        }
        return compressedData;
    } else if (format == PVRZFormat::DXT5) {
        std::vector<uint8_t> compressedData(blockWidth * blockHeight * 16);

        for (int by = 0; by < blockHeight; ++by) {
            for (int bx = 0; bx < blockWidth; ++bx) {
                uint32_t block[16];
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        int srcX = bx * 4 + x;
                        int srcY = by * 4 + y;
                        if (srcX < width && srcY < height) {
                            int srcIdx = (srcY * width + srcX) * 4;
                            const uint8_t* p = &argbData[srcIdx];
                            // Read ARGB format (converted from SDL BGRA)
                            block[y * 4 + x] = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
                        } else {
                            block[y * 4 + x] = 0;
                        }
                    }
                }
                
                int blockIndex = (by * blockWidth + bx) * 16;
                encodeDXT5Block(&block[0], &compressedData[blockIndex]);
            }
        }
        return compressedData;
    }
    
    return std::vector<uint8_t>();
}

void PVRZ::encodeDXT5Block(const uint32_t* pixels, uint8_t* output) {
    uint8_t alphas[16];
    for (int i = 0; i < 16; i++) {
        alphas[i] = (pixels[i] >> 24) & 0xFF;
    }
    
    uint8_t minAlpha = 255, maxAlpha = 0;
    for (int i = 0; i < 16; i++) {
        minAlpha = std::min(minAlpha, alphas[i]);
        maxAlpha = std::max(maxAlpha, alphas[i]);
    }
    
    output[0] = maxAlpha;
    output[1] = minAlpha;
    
    uint8_t alphaPalette[8];
    alphaPalette[0] = maxAlpha;
    alphaPalette[1] = minAlpha;
    
    if (maxAlpha > minAlpha) {
        for (int i = 1; i < 7; i++) {
            alphaPalette[i + 1] = ((7 - i) * maxAlpha + i * minAlpha) / 7;
        }
    } else {
        for (int i = 2; i < 8; i++) {
            alphaPalette[i] = 0;
        }
    }
    
    uint64_t alphaIndices = 0;
    for (int i = 0; i < 16; i++) {
        int bestIndex = 0;
        int bestDistance = INT_MAX;
        
        for (int j = 0; j < 8; j++) {
            int distance = abs(alphas[i] - alphaPalette[j]);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = j;
            }
        }
        
        alphaIndices |= ((uint64_t)bestIndex << (i * 3));
    }
    
    for (int i = 0; i < 6; i++) {
        output[2 + i] = (alphaIndices >> (i * 8)) & 0xFF;
    }
    
    encodeDXT1Block(pixels, output + 8);
}

uint16_t PVRZ::packRGB565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void PVRZ::unpackRGB565(uint16_t color, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = ((color >> 11) & 0x1F) << 3;
    g = ((color >> 5) & 0x3F) << 2;
    b = (color & 0x1F) << 3;
}

void PVRZ::encodeDXT1Block(const uint32_t* pixels, uint8_t* output) {
    // Extract colors from 4x4 block (following reference implementation)
    uint8_t colors[16][4]; // RGBA
    for (int i = 0; i < 16; i++) {
        uint32_t pixel = pixels[i];
        colors[i][0] = (pixel >> 16) & 0xFF; // R
        colors[i][1] = (pixel >> 8) & 0xFF;  // G
        colors[i][2] = pixel & 0xFF;         // B
        colors[i][3] = (pixel >> 24) & 0xFF; // A
    }
    
    // Find min/max colors (simplified - should use proper clustering)
    uint8_t minR = 255, maxR = 0, minG = 255, maxG = 0, minB = 255, maxB = 0;
    for (int i = 0; i < 16; i++) {
        minR = std::min(minR, colors[i][0]);
        maxR = std::max(maxR, colors[i][0]);
        minG = std::min(minG, colors[i][1]);
        maxG = std::max(maxG, colors[i][1]);
        minB = std::min(minB, colors[i][2]);
        maxB = std::max(maxB, colors[i][2]);
    }
    
    // Pack endpoints
    uint16_t color0 = packRGB565(maxR, maxG, maxB);
    uint16_t color1 = packRGB565(minR, minG, minB);
    
    // Make sure color0 > color1 for 4-color mode
    if (color0 < color1) {
        std::swap(color0, color1);
    }
    
    // Write color endpoints
    output[0] = color0 & 0xFF;
    output[1] = (color0 >> 8) & 0xFF;
    output[2] = color1 & 0xFF;
    output[3] = (color1 >> 8) & 0xFF;
    
    // Generate palette
    uint8_t palette[4][3];
    unpackRGB565(color0, palette[0][0], palette[0][1], palette[0][2]);
    unpackRGB565(color1, palette[1][0], palette[1][1], palette[1][2]);
    
    // Interpolated colors
    palette[2][0] = (2 * palette[0][0] + palette[1][0]) / 3;
    palette[2][1] = (2 * palette[0][1] + palette[1][1]) / 3;
    palette[2][2] = (2 * palette[0][2] + palette[1][2]) / 3;
    
    palette[3][0] = (palette[0][0] + 2 * palette[1][0]) / 3;
    palette[3][1] = (palette[0][1] + 2 * palette[1][1]) / 3;
    palette[3][2] = (palette[0][2] + 2 * palette[1][2]) / 3;
    
    // Generate indices (simplified nearest neighbor)
    uint32_t indices = 0;
    for (int i = 0; i < 16; i++) {
        int bestIndex = 0;
        int bestDistance = INT_MAX;
        
        for (int j = 0; j < 4; j++) {
            int dr = colors[i][0] - palette[j][0];
            int dg = colors[i][1] - palette[j][1];
            int db = colors[i][2] - palette[j][2];
            int distance = dr * dr + dg * dg + db * db;
            
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = j;
            }
        }
        
        indices |= (bestIndex << (i * 2));
    }
    
    // Write indices
    output[4] = indices & 0xFF;
    output[5] = (indices >> 8) & 0xFF;
    output[6] = (indices >> 16) & 0xFF;
    output[7] = (indices >> 24) & 0xFF;
}

bool PVRZ::createAtlasFromPNGs(const std::vector<std::string>& pngFiles,
                                     std::vector<uint8_t>& atlasData,
                                     int& atlasWidth, int& atlasHeight,
                                     std::vector<std::pair<int, int>>& framePositions,
                                     std::vector<uint8_t>* firstPngARGB) {
    Log(DEBUG, "PVRZ", "Creating texture atlas from {} PNG files", pngFiles.size());

    // === Special-case: single full-page PNG (typical for TIS V2 pages) ===
    // In this scenario the input PNG already represents the complete 1024×1024 page.
    if (pngFiles.size() == 1) {
        // Load PNG
        SDL_Surface* surface = IMG_Load(pngFiles[0].c_str());
        if (!surface) {
            Log(ERROR, "PVRZ", "Failed to load PNG: {} ({})", pngFiles[0], IMG_GetError());
            return false;
        }
        // Convert to ARGB8888 – internal pixel order expected by the encoder
        SDL_Surface* argbSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(surface);
        if (!argbSurface) {
            Log(ERROR, "PVRZ", "Failed to convert surface to ARGB8888 for {}", pngFiles[0]);
            return false;
        }

        atlasWidth  = argbSurface->w;
        atlasHeight = argbSurface->h;
        const size_t bufferSize = static_cast<size_t>(atlasWidth) * atlasHeight * 4;
        atlasData.assign(bufferSize, 0);

        SDL_LockSurface(argbSurface);
        uint8_t* pixels = static_cast<uint8_t*>(argbSurface->pixels);
        const int pitch = argbSurface->pitch; // bytes per row

        // Copy the whole surface into atlasData, converting from BGRA to ARGB
        for (int y = 0; y < atlasHeight; ++y) {
            for (int x = 0; x < atlasWidth; ++x) {
                int srcIdx = y * pitch + x * 4;
                int dstIdx = (y * atlasWidth + x) * 4;
                
                // SDL stores as BGRA in memory, convert to ARGB
                uint8_t b = pixels[srcIdx + 0];  // Blue
                uint8_t g = pixels[srcIdx + 1];  // Green  
                uint8_t r = pixels[srcIdx + 2];  // Red
                uint8_t a = pixels[srcIdx + 3];  // Alpha
                
                // Store as ARGB
                atlasData[dstIdx + 0] = a;  // Alpha
                atlasData[dstIdx + 1] = r;  // Red
                atlasData[dstIdx + 2] = g;  // Green
                atlasData[dstIdx + 3] = b;  // Blue
            }
        }
        SDL_UnlockSurface(argbSurface);
        SDL_FreeSurface(argbSurface);

        framePositions.clear();
        framePositions.emplace_back(0, 0);

        Log(DEBUG, "PVRZ", "Texture atlas created: {}x{} pixels (single page)", atlasWidth, atlasHeight);
        return true;
    }

    // === Generic multi-frame atlas (original 3×3 grid logic) ===
    // Calculate frame size dynamically from first PNG
    int frameSize = 256; // Default fallback
    if (!pngFiles.empty()) {
        SDL_Surface* testSurface = IMG_Load(pngFiles[0].c_str());
        if (testSurface) {
            frameSize = testSurface->w; // Use actual frame width
            SDL_FreeSurface(testSurface);
        }
    }
    
    const int grid = 3;
    const int gap = 1;
    const int offsets[3] = {gap, gap + frameSize + gap, gap + 2 * (frameSize + gap)};
    
    // Calculate required atlas size for 3x3 grid of frames
    atlasWidth = gap + 3 * frameSize + 2 * gap;  // 3 frames + 2 gaps + 1-pixel margins
    atlasHeight = gap + 3 * frameSize + 2 * gap;
    // Clear and resize the atlas buffer for this specific atlas
    atlasData.clear();
    atlasData.resize(atlasWidth * atlasHeight * 4, 0);
    Log(DEBUG, "PVRZ", "Initialized atlas buffer: {}x{} = {} bytes", atlasWidth, atlasHeight, atlasData.size());
    framePositions.clear();
    framePositions.reserve(pngFiles.size());
    for (int i = 0; i < (int)pngFiles.size(); ++i) {
        int gridX = (i % 9) % grid;
        int gridY = (i % 9) / grid;
        int atlasX = offsets[gridX];
        int atlasY = offsets[gridY];
        
        framePositions.emplace_back(atlasX, atlasY);
        // Load PNG and place in atlas
        std::string absPath;
        try {
            absPath = std::filesystem::canonical(pngFiles[i]).string();
        } catch (const std::exception&) {
            absPath = std::filesystem::absolute(pngFiles[i]).string();
        }
        SDL_Surface* surface = IMG_Load(pngFiles[i].c_str());
        if (!surface) {
            const char* imgError = IMG_GetError();
            Log(ERROR, "PVRZ", "Failed to load PNG: {} (abs: {}) (SDL_image Error: {})", pngFiles[i], absPath, imgError ? imgError : "Unknown error");
            continue;
        }
        Log(DEBUG, "PVRZ", "Loaded PNG: {} (abs: {}), w={}, h={}, format=0x{:x}", pngFiles[i], absPath, surface->w, surface->h, surface->format->format);
        SDL_Surface* argbSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(surface);
        if (!argbSurface) {
            Log(ERROR, "PVRZ", "Failed to convert surface to ARGB8888 for {}", pngFiles[i]);
            continue;
        }
        SDL_LockSurface(argbSurface);
        uint8_t* pixels = static_cast<uint8_t*>(argbSurface->pixels);
        
        // Copy pixels into atlasData at (atlasX, atlasY)
        // Convert from SDL BGRA format to ARGB uint32 format
        const int pitch = argbSurface->pitch; // bytes per row
        for (int y = 0; y < frameSize; ++y) {
            for (int x = 0; x < frameSize; ++x) {
                int srcIdx = y * pitch + x * 4;
                int dstIdx = ((atlasY + y) * atlasWidth + atlasX + x) * 4;
                
                // SDL stores as BGRA in memory, convert to ARGB
                uint8_t b = pixels[srcIdx + 0];  // Blue
                uint8_t g = pixels[srcIdx + 1];  // Green  
                uint8_t r = pixels[srcIdx + 2];  // Red
                uint8_t a = pixels[srcIdx + 3];  // Alpha
                
                // Store as ARGB
                atlasData[dstIdx + 0] = a;  // Alpha
                atlasData[dstIdx + 1] = r;  // Red
                atlasData[dstIdx + 2] = g;  // Green
                atlasData[dstIdx + 3] = b;  // Blue
            }
        }
        
        SDL_UnlockSurface(argbSurface);
        SDL_FreeSurface(argbSurface);
    }
    Log(DEBUG, "PVRZ", "Texture atlas created: {}x{} pixels, {} frames positioned", atlasWidth, atlasHeight, framePositions.size());
    return true;
}

bool PVRZ::createAtlasFromPNGsWithPositions(const std::vector<std::string>& pngFiles,
                                                  const std::vector<std::pair<int, int>>& framePositions,
                                                  std::vector<uint8_t>& atlasData,
                                                  int& atlasWidth, int& atlasHeight) {
    Log(DEBUG, "PVRZ", "Creating texture atlas from {} PNG files with bin-packing positions", pngFiles.size());

    if (pngFiles.size() != framePositions.size()) {
        Log(ERROR, "PVRZ", "Mismatch between PNG files ({}) and frame positions ({})", pngFiles.size(), framePositions.size());
        return false;
    }

    // === Special-case: single full-page PNG ===
    if (pngFiles.size() == 1) {
        // Load PNG
        SDL_Surface* surface = IMG_Load(pngFiles[0].c_str());
        if (!surface) {
            Log(ERROR, "PVRZ", "Failed to load PNG: {} ({})", pngFiles[0], IMG_GetError());
            return false;
        }
        // Convert to ARGB8888
        SDL_Surface* argbSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(surface);
        if (!argbSurface) {
            Log(ERROR, "PVRZ", "Failed to convert surface to ARGB8888 for {}", pngFiles[0]);
            return false;
        }

        atlasWidth = argbSurface->w;
        atlasHeight = argbSurface->h;
        const size_t bufferSize = static_cast<size_t>(atlasWidth) * atlasHeight * 4;
        atlasData.assign(bufferSize, 0);

        SDL_LockSurface(argbSurface);
        uint8_t* pixels = static_cast<uint8_t*>(argbSurface->pixels);
        const int pitch = argbSurface->pitch;

        // Copy the whole surface into atlasData, converting from BGRA to ARGB
        for (int y = 0; y < atlasHeight; ++y) {
            for (int x = 0; x < atlasWidth; ++x) {
                int srcIdx = y * pitch + x * 4;
                int dstIdx = (y * atlasWidth + x) * 4;
                
                // SDL stores as BGRA in memory, convert to ARGB
                uint8_t b = pixels[srcIdx + 0];  // Blue
                uint8_t g = pixels[srcIdx + 1];  // Green  
                uint8_t r = pixels[srcIdx + 2];  // Red
                uint8_t a = pixels[srcIdx + 3];  // Alpha
                
                // Store as ARGB
                atlasData[dstIdx + 0] = a;  // Alpha
                atlasData[dstIdx + 1] = r;  // Red
                atlasData[dstIdx + 2] = g;  // Green
                atlasData[dstIdx + 3] = b;  // Blue
            }
        }
        SDL_UnlockSurface(argbSurface);
        SDL_FreeSurface(argbSurface);

        Log(DEBUG, "PVRZ", "Texture atlas created: {}x{} pixels (single page)", atlasWidth, atlasHeight);
        return true;
    }

    // === Multi-frame atlas with bin-packing positions ===
    // Calculate required atlas size from frame positions
    int maxX = 0, maxY = 0;
    for (size_t i = 0; i < pngFiles.size(); ++i) {
        SDL_Surface* surface = IMG_Load(pngFiles[i].c_str());
        if (surface) {
            maxX = std::max(maxX, framePositions[i].first + surface->w);
            maxY = std::max(maxY, framePositions[i].second + surface->h);
            SDL_FreeSurface(surface);
        }
    }
    
    atlasWidth = maxX;
    atlasHeight = maxY;
    
    // Clear and resize the atlas buffer
    atlasData.clear();
    atlasData.resize(atlasWidth * atlasHeight * 4, 0);
    Log(DEBUG, "PVRZ", "Initialized atlas buffer: {}x{} = {} bytes", atlasWidth, atlasHeight, atlasData.size());

    // Load and position each PNG
    for (size_t i = 0; i < pngFiles.size(); ++i) {
        std::string absPath;
        try {
            absPath = std::filesystem::canonical(pngFiles[i]).string();
        } catch (const std::exception&) {
            absPath = std::filesystem::absolute(pngFiles[i]).string();
        }
        SDL_Surface* surface = IMG_Load(pngFiles[i].c_str());
        if (!surface) {
            const char* imgError = IMG_GetError();
            Log(ERROR, "PVRZ", "Failed to load PNG: {} (abs: {}) (SDL_image Error: {})", pngFiles[i], absPath, imgError ? imgError : "Unknown error");
            continue;
        }
        Log(DEBUG, "PVRZ", "Loaded PNG: {} (abs: {}), w={}, h={}, format=0x{:x}", pngFiles[i], absPath, surface->w, surface->h, surface->format->format);
        
        SDL_Surface* argbSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(surface);
        if (!argbSurface) {
            Log(ERROR, "PVRZ", "Failed to convert surface to ARGB8888 for {}", pngFiles[i]);
            continue;
        }
        
        SDL_LockSurface(argbSurface);
        uint8_t* pixels = static_cast<uint8_t*>(argbSurface->pixels);
        const int pitch = argbSurface->pitch;
        
        int frameWidth = argbSurface->w;
        int frameHeight = argbSurface->h;
        int atlasX = framePositions[i].first;
        int atlasY = framePositions[i].second;
        
        // Copy pixels into atlasData at the bin-packing position
        for (int y = 0; y < frameHeight; ++y) {
            for (int x = 0; x < frameWidth; ++x) {
                int srcIdx = y * pitch + x * 4;
                int dstIdx = ((atlasY + y) * atlasWidth + atlasX + x) * 4;
                
                // SDL stores as BGRA in memory, convert to ARGB
                uint8_t b = pixels[srcIdx + 0];  // Blue
                uint8_t g = pixels[srcIdx + 1];  // Green  
                uint8_t r = pixels[srcIdx + 2];  // Red
                uint8_t a = pixels[srcIdx + 3];  // Alpha
                
                // Store as ARGB
                atlasData[dstIdx + 0] = a;  // Alpha
                atlasData[dstIdx + 1] = r;  // Red
                atlasData[dstIdx + 2] = g;  // Green
                atlasData[dstIdx + 3] = b;  // Blue
            }
        }
        
        SDL_UnlockSurface(argbSurface);
        SDL_FreeSurface(argbSurface);
    }
    
    Log(DEBUG, "PVRZ", "Texture atlas created: {}x{} pixels, {} frames positioned", atlasWidth, atlasHeight, pngFiles.size());
    return true;
}

bool PVRZ::createAtlasFromPixelsWithPositions(const std::vector<std::vector<uint32_t>>& tilePixels,
                                             const std::vector<std::pair<int, int>>& framePositions,
                                             std::vector<uint8_t>& atlasData,
                                             int& atlasWidth, int& atlasHeight) {
    Log(DEBUG, "PVRZ", "Creating texture atlas from {} tile pixel arrays with positions", tilePixels.size());

    if (tilePixels.size() != framePositions.size()) {
        Log(ERROR, "PVRZ", "Mismatch between tile pixels ({}) and frame positions ({})", tilePixels.size(), framePositions.size());
        return false;
    }

    // Calculate required atlas size from frame positions
    int maxX = 0, maxY = 0;
    for (size_t i = 0; i < tilePixels.size(); ++i) {
        // Each tile is 64x64 pixels
        maxX = std::max(maxX, framePositions[i].first + 64);
        maxY = std::max(maxY, framePositions[i].second + 64);
    }
    
    atlasWidth = maxX;
    atlasHeight = maxY;
    
    // Clear and resize the atlas buffer
    atlasData.clear();
    atlasData.resize(atlasWidth * atlasHeight * 4, 0);
    Log(DEBUG, "PVRZ", "Initialized atlas buffer: {}x{} = {} bytes", atlasWidth, atlasHeight, atlasData.size());

    // Copy each tile's pixels into the atlas at the specified position
    for (size_t i = 0; i < tilePixels.size(); ++i) {
        const std::vector<uint32_t>& tileData = tilePixels[i];
        int atlasX = framePositions[i].first;
        int atlasY = framePositions[i].second;
        
        // Copy 64x64 tile pixels into atlasData
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                int tileIdx = y * 64 + x;
                int atlasIdx = ((atlasY + y) * atlasWidth + atlasX + x) * 4;
                
                if (tileIdx < tileData.size()) {
                    uint32_t pixel = tileData[tileIdx];
                    
                    // Convert ARGB32 to ARGB8 bytes
                    atlasData[atlasIdx + 0] = (pixel >> 24) & 0xFF;  // Alpha
                    atlasData[atlasIdx + 1] = (pixel >> 16) & 0xFF;  // Red
                    atlasData[atlasIdx + 2] = (pixel >> 8) & 0xFF;   // Green
                    atlasData[atlasIdx + 3] = pixel & 0xFF;          // Blue
                }
            }
        }
    }
    
    Log(DEBUG, "PVRZ", "Texture atlas created: {}x{} pixels, {} tiles positioned", atlasWidth, atlasHeight, tilePixels.size());
    return true;
}

bool PVRZ::writePVRZFile(const std::string& outputPath,
                               const std::vector<uint8_t>& compressedData,
                               int width, int height,
                               PVRZFormat format) {
    // Create PVR header data in memory first
    std::vector<uint8_t> pvrData;
    
    // PVR header (expects the entire PVR file to be zlib-compressed)
    uint32_t signature = 0x03525650; // 'PVR\x03' little-endian
    uint32_t flags = 0; // ignored
    uint64_t formatField = (format == PVRZFormat::DXT1) ? 7 : 11; // DXT1=7, DXT5=11
    uint32_t colorSpace = 0; // ignored
    uint32_t channelType = 0; // 0: normalized unsigned byte
    uint32_t height32 = static_cast<uint32_t>(height);
    uint32_t width32 = static_cast<uint32_t>(width);
    uint32_t depth = 1;
    uint32_t numFaces = 1;
    uint32_t numSurfaces = 1;
    uint32_t numMipmaps = 1;
    uint32_t metaDataSize = 0;
    
    // Build complete PVR file in memory (header + DXT data)
    pvrData.resize(52 + compressedData.size()); // 52 bytes header + DXT data
    size_t offset = 0;
    
    std::memcpy(&pvrData[offset], &signature, 4); offset += 4;
    std::memcpy(&pvrData[offset], &flags, 4); offset += 4;
    std::memcpy(&pvrData[offset], &formatField, 8); offset += 8;
    std::memcpy(&pvrData[offset], &colorSpace, 4); offset += 4;
    std::memcpy(&pvrData[offset], &channelType, 4); offset += 4;
    std::memcpy(&pvrData[offset], &height32, 4); offset += 4;
    std::memcpy(&pvrData[offset], &width32, 4); offset += 4;
    std::memcpy(&pvrData[offset], &depth, 4); offset += 4;
    std::memcpy(&pvrData[offset], &numFaces, 4); offset += 4;
    std::memcpy(&pvrData[offset], &numSurfaces, 4); offset += 4;
    std::memcpy(&pvrData[offset], &numMipmaps, 4); offset += 4;
    std::memcpy(&pvrData[offset], &metaDataSize, 4); offset += 4;
    std::memcpy(&pvrData[offset], compressedData.data(), compressedData.size());
    
    // Now zlib-compress the entire PVR file (header + DXT data)
    std::vector<uint8_t> zlibData = compressWithZlib(pvrData);
    
    // Write the zlib-compressed PVR file directly
    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "PVRZ", "Failed to create output file: {}", outputPath);
        return false;
    }
    file.write(reinterpret_cast<const char*>(zlibData.data()), zlibData.size());
    file.close();
    
    // Log file size
    std::ifstream checkFile(outputPath, std::ios::binary | std::ios::ate);
    if (checkFile.is_open()) {
        std::streamsize fileSize = checkFile.tellg();
        Log(MESSAGE, "PVRZ", "Wrote {} bytes zlib-compressed PVR file (original PVR: {} bytes), file size on disk: {} bytes", zlibData.size(), pvrData.size(), fileSize);
        checkFile.close();
    } else {
        Log(ERROR, "PVRZ", "Could not open {} to check file size after writing", outputPath);
    }
    
    return true;
}

bool PVRZ::isValidPowerOfTwo(int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

int PVRZ::nextPowerOfTwo(int value) {
    if (value <= 0) return 1;
    
    int power = 1;
    while (power < value) {
        power <<= 1;
    }
    return power;
}

std::vector<uint8_t> PVRZ::padToPowerOfTwo(const std::vector<uint8_t>& data, int width, int height, int& newWidth, int& newHeight) {
    newWidth = nextPowerOfTwo(width);
    newHeight = nextPowerOfTwo(height);
    
    std::vector<uint8_t> paddedData(newWidth * newHeight * 4, 0);
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int srcIndex = (y * width + x) * 4;
            int dstIndex = (y * newWidth + x) * 4;
            
            if (srcIndex + 3 < static_cast<int>(data.size()) && 
                dstIndex + 3 < static_cast<int>(paddedData.size())) {
                paddedData[dstIndex] = data[srcIndex];     // A
                paddedData[dstIndex + 1] = data[srcIndex + 1]; // R
                paddedData[dstIndex + 2] = data[srcIndex + 2]; // G
                paddedData[dstIndex + 3] = data[srcIndex + 3]; // B
            }
        }
    }
    
    return paddedData;
}

bool PVRZ::readPVRZFile(const std::string& filePath,
                        std::vector<uint8_t>& dxtData,
                        int& width, int& height,
                        PVRZFormat& format) {
    // Read the PVRZ file
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "PVRZ", "Failed to open PVRZ file: {}", filePath);
        return false;
    }
    
    // Read the entire file
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> pvrzData(fileSize);
    file.read(reinterpret_cast<char*>(pvrzData.data()), fileSize);
    file.close();
    
    // Parse zlib header (first 4 bytes contain uncompressed size)
    if (pvrzData.size() < 4) {
        Log(ERROR, "PVRZ", "PVRZ file too small: {}", filePath);
        return false;
    }
    
    uint32_t uncompressedSize = readLE32(pvrzData.data());
    
    // Decompress the data
    std::vector<uint8_t> pvrData(uncompressedSize);
    uLongf actualSize = uncompressedSize;
    
    int result = uncompress(pvrData.data(), &actualSize,
                           pvrzData.data() + 4, pvrzData.size() - 4);
    
    if (result != Z_OK) {
        Log(ERROR, "PVRZ", "Failed to decompress PVRZ file: {}", filePath);
        return false;
    }
    
    // Parse PVR header
    if (pvrData.size() < 52) {
        Log(ERROR, "PVRZ", "PVR data too small: {}", filePath);
        return false;
    }
    
    uint32_t version = readLE32(pvrData.data() + 0);
    uint64_t pixelFormat = readLE64(pvrData.data() + 8);
    width = readLE32(pvrData.data() + 28);
    height = readLE32(pvrData.data() + 24);
    
    // Determine format
    if (pixelFormat == 7) {
        format = PVRZFormat::DXT1;
    } else if (pixelFormat == 11) {
        format = PVRZFormat::DXT5;
    } else {
        Log(ERROR, "PVRZ", "Unsupported pixel format: {}", pixelFormat);
        return false;
    }
    
    // Extract DXT data (everything after the 52-byte header)
    dxtData.assign(pvrData.begin() + 52, pvrData.end());
    
    Log(DEBUG, "PVRZ", "Successfully read PVRZ file: {}x{} format: {}", 
        width, height, format == PVRZFormat::DXT1 ? "DXT1" : "DXT5");
    
    return true;
}

bool PVRZ::loadPVRZResource(const std::string& resourceName,
                           std::vector<uint8_t>& dxtData,
                           int& width, int& height,
                           PVRZFormat& format) {
    // Use our resource service to load the PVRZ file
    auto* resourceCoordinator = dynamic_cast<ResourceCoordinatorService*>(ServiceManager::getService("ResourceCoordinatorService"));
    if (!resourceCoordinator) {
        Log(ERROR, "PVRZ", "Failed to get ResourceCoordinatorService");
        return false;
    }
    ResourceData resourceData = resourceCoordinator->getResourceData(resourceName, IE_PVRZ_CLASS_ID);
    
    if (resourceData.data.empty()) {
        Log(ERROR, "PVRZ", "Failed to load PVRZ resource: {}", resourceName);
        return false;
    }
    
    std::vector<uint8_t> pvrzData = resourceData.data;
    
    // Parse zlib header (first 4 bytes contain uncompressed size)
    if (pvrzData.size() < 4) {
        Log(ERROR, "PVRZ", "PVRZ data too small: {}", resourceName);
        return false;
    }
    
    uint32_t uncompressedSize = readLE32(pvrzData.data());
    
    // Decompress the data
    std::vector<uint8_t> pvrData(uncompressedSize);
    uLongf actualSize = uncompressedSize;
    
    int result = uncompress(pvrData.data(), &actualSize,
                           pvrzData.data() + 4, pvrzData.size() - 4);
    
    if (result != Z_OK) {
        Log(ERROR, "PVRZ", "Failed to decompress PVRZ resource: {}", resourceName);
        return false;
    }
    
    // Parse PVR header
    if (pvrData.size() < 52) {
        Log(ERROR, "PVRZ", "PVR data too small: {}", resourceName);
        return false;
    }
    
    uint32_t version = readLE32(pvrData.data() + 0);
    uint64_t pixelFormat = readLE64(pvrData.data() + 8);
    width = readLE32(pvrData.data() + 28);
    height = readLE32(pvrData.data() + 24);
    
    // Determine format
    if (pixelFormat == 7) {
        format = PVRZFormat::DXT1;
    } else if (pixelFormat == 11) {
        format = PVRZFormat::DXT5;
    } else {
        Log(ERROR, "PVRZ", "Unsupported pixel format: {}", pixelFormat);
        return false;
    }
    
    // Extract DXT data (everything after the 52-byte header)
    dxtData.assign(pvrData.begin() + 52, pvrData.end());
    
    Log(DEBUG, "PVRZ", "Successfully loaded PVRZ resource: {}x{} format: {}", 
        width, height, format == PVRZFormat::DXT1 ? "DXT1" : "DXT5");
    
    return true;
}

uint32_t PVRZ::readLE32(const uint8_t* data) {
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

uint64_t PVRZ::readLE64(const uint8_t* data) {
    return (uint64_t)readLE32(data) | ((uint64_t)readLE32(data + 4) << 32);
}

void PVRZ::decodeDXT1Block(const uint8_t* block, uint32_t* pixels) {
    uint16_t color0 = (block[0] | (block[1] << 8));
    uint16_t color1 = (block[2] | (block[3] << 8));
    uint32_t indices = (block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24));
    
    uint8_t r0, g0, b0, r1, g1, b1;
    unpackRGB565(color0, r0, g0, b0);
    unpackRGB565(color1, r1, g1, b1);
    
    uint32_t colors[4];
    colors[0] = (0xFF << 24) | (r0 << 16) | (g0 << 8) | b0;
    colors[1] = (0xFF << 24) | (r1 << 16) | (g1 << 8) | b1;
    
    if (color0 > color1) {
        colors[2] = (0xFF << 24) | (((2 * r0 + r1) / 3) << 16) | (((2 * g0 + g1) / 3) << 8) | ((2 * b0 + b1) / 3);
        colors[3] = (0xFF << 24) | (((r0 + 2 * r1) / 3) << 16) | (((g0 + 2 * g1) / 3) << 8) | ((b0 + 2 * b1) / 3);
    } else {
        colors[2] = (0xFF << 24) | (((r0 + r1) / 2) << 16) | (((g0 + g1) / 2) << 8) | ((b0 + b1) / 2);
        colors[3] = 0; // Transparent
    }
    
    for (int i = 0; i < 16; ++i) {
        pixels[i] = colors[(indices >> (i * 2)) & 3];
    }
}

void PVRZ::decodeDXT5Block(const uint8_t* block, uint32_t* pixels) {
    // Alpha values (8 bytes)
    uint64_t alpha = (uint64_t(block[0]) | (uint64_t(block[1]) << 8) | (uint64_t(block[2]) << 16) | (uint64_t(block[3]) << 24) |
                     (uint64_t(block[4]) << 32) | (uint64_t(block[5]) << 40) | (uint64_t(block[6]) << 48) | (uint64_t(block[7]) << 56));
    
    uint8_t alpha0 = alpha & 0xFF;
    uint8_t alpha1 = (alpha >> 8) & 0xFF;
    uint64_t alphaIndices = alpha >> 16;
    
    uint8_t alphas[8];
    alphas[0] = alpha0;
    alphas[1] = alpha1;
    
    if (alpha0 > alpha1) {
        alphas[2] = (6 * alpha0 + 1 * alpha1) / 7;
        alphas[3] = (5 * alpha0 + 2 * alpha1) / 7;
        alphas[4] = (4 * alpha0 + 3 * alpha1) / 7;
        alphas[5] = (3 * alpha0 + 4 * alpha1) / 7;
        alphas[6] = (2 * alpha0 + 5 * alpha1) / 7;
        alphas[7] = (1 * alpha0 + 6 * alpha1) / 7;
    } else {
        alphas[2] = (4 * alpha0 + 1 * alpha1) / 5;
        alphas[3] = (3 * alpha0 + 2 * alpha1) / 5;
        alphas[4] = (2 * alpha0 + 3 * alpha1) / 5;
        alphas[5] = (1 * alpha0 + 4 * alpha1) / 5;
        alphas[6] = 0;
        alphas[7] = 255;
    }
    
    // Color values (8 bytes)
    uint16_t color0 = (block[8] | (block[9] << 8));
    uint16_t color1 = (block[10] | (block[11] << 8));
    uint32_t indices = (block[12] | (block[13] << 8) | (block[14] << 16) | (block[15] << 24));
    
    uint8_t r0, g0, b0, r1, g1, b1;
    unpackRGB565(color0, r0, g0, b0);
    unpackRGB565(color1, r1, g1, b1);
    
    uint32_t colors[4];
    colors[0] = (r0 << 16) | (g0 << 8) | b0;
    colors[1] = (r1 << 16) | (g1 << 8) | b1;
    colors[2] = (((2 * r0 + r1) / 3) << 16) | (((2 * g0 + g1) / 3) << 8) | ((2 * b0 + b1) / 3);
    colors[3] = (((r0 + 2 * r1) / 3) << 16) | (((g0 + 2 * g1) / 3) << 8) | ((b0 + 2 * b1) / 3);
    
    for (int i = 0; i < 16; ++i) {
        uint8_t alphaVal = alphas[(alphaIndices >> (i * 3)) & 7];
        uint32_t colorVal = colors[(indices >> (i * 2)) & 3];
        pixels[i] = (alphaVal << 24) | colorVal;
    }
}

bool PVRZ::loadPVRZResourceAsARGB(const std::string& resourceName,
                                  std::vector<uint8_t>& argbData,
                                  int& width, int& height) {
    // Load the PVRZ resource and get DXT data
    std::vector<uint8_t> dxtData;
    PVRZFormat format;
    
    if (!loadPVRZResource(resourceName, dxtData, width, height, format)) {
        return false;
    }
    
    // Decode DXT data to ARGB
    int blockWidth = width / 4;
    int blockHeight = height / 4;
    int blockSize = (format == PVRZFormat::DXT5) ? 16 : 8;
    
    argbData.resize(width * height * 4, 0);
    
    for (int by = 0; by < blockHeight; ++by) {
        for (int bx = 0; bx < blockWidth; ++bx) {
            size_t blockIndex = by * blockWidth + bx;
            size_t blockPos = blockIndex * blockSize;
            
            if (blockPos + blockSize > dxtData.size()) {
                Log(ERROR, "PVRZ", "DXT block out of bounds");
                continue;
            }
            
            uint32_t decoded[16];
            if (format == PVRZFormat::DXT1) {
                decodeDXT1Block(dxtData.data() + blockPos, decoded);
            } else {
                decodeDXT5Block(dxtData.data() + blockPos, decoded);
            }
            
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    int px = bx * 4 + x;
                    int py = by * 4 + y;
                    
                    if (px < width && py < height) {
                        uint32_t pixel = decoded[y * 4 + x];
                        size_t pixelIndex = (py * width + px) * 4;
                        argbData[pixelIndex + 0] = (pixel >> 24) & 0xFF; // A
                        argbData[pixelIndex + 1] = (pixel >> 16) & 0xFF; // R
                        argbData[pixelIndex + 2] = (pixel >> 8) & 0xFF;  // G
                        argbData[pixelIndex + 3] = pixel & 0xFF;         // B
                    }
                }
            }
        }
    }
    
    Log(DEBUG, "PVRZ", "Successfully decoded PVRZ to ARGB: {}x{}", width, height);
    return true;
}

bool PVRZ::createTextureAtlasPVRZFromPixels(const std::vector<std::vector<uint32_t>>& tilePixels,
                                           const std::vector<std::pair<int, int>>& tilePositions,
                                           const std::string& outputPath,
                                           int atlasWidth, int atlasHeight,
                                           PVRZFormat format) {
    if (tilePixels.empty() || tilePixels.size() != tilePositions.size()) {
        Log(ERROR, "PVRZ", "Invalid tile data for texture atlas");
        return false;
    }
    
    // Debug: Log which atlas is being created
    Log(DEBUG, "PVRZ", "=== CREATING ATLAS FROM PIXELS: {} ===", outputPath);
    Log(DEBUG, "PVRZ", "Atlas contains {} tiles:", tilePixels.size());
    
    // Ensure atlas dimensions are power of 2
    if (!isValidPowerOfTwo(atlasWidth) || !isValidPowerOfTwo(atlasHeight)) {
        atlasWidth = nextPowerOfTwo(atlasWidth);
        atlasHeight = nextPowerOfTwo(atlasHeight);
        Log(DEBUG, "PVRZ", "Adjusted atlas dimensions to {}x{}", atlasWidth, atlasHeight);
    }
    
    // Create texture atlas from pixel data
    std::vector<uint8_t> atlasData;
    std::vector<std::pair<int, int>> framePositions;
    
    if (!createAtlasFromPixelsWithPositions(tilePixels, tilePositions, atlasData, atlasWidth, atlasHeight)) {
        Log(ERROR, "PVRZ", "Failed to create texture atlas from pixels");
        return false;
    }
    
    // Compress atlas to specified format
    std::vector<uint8_t> compressedData = compressToDXT(atlasData, atlasWidth, atlasHeight, format);
    if (compressedData.empty()) {
        Log(ERROR, "PVRZ", "Failed to compress texture atlas");
        return false;
    }
    
    // Create PVR header data in memory first
    std::vector<uint8_t> pvrData;
    
    // PVR header (expects the entire PVR file to be zlib-compressed)
    uint32_t signature = 0x03525650; // 'PVR\x03' little-endian
    uint32_t flags = 0; // ignored
    uint64_t formatField = (format == PVRZFormat::DXT1) ? 7 : 11; // DXT1=7, DXT5=11
    uint32_t colorSpace = 0; // ignored
    uint32_t channelType = 0; // 0: normalized unsigned byte
    uint32_t height = static_cast<uint32_t>(atlasHeight);
    uint32_t width = static_cast<uint32_t>(atlasWidth);
    uint32_t depth = 1;
    uint32_t numFaces = 1;
    uint32_t numSurfaces = 1;
    uint32_t numMipmaps = 1;
    uint32_t metaDataSize = 0;
    
    // Build complete PVR file in memory (header + DXT data)
    pvrData.resize(52 + compressedData.size()); // 52 bytes header + DXT data
    size_t offset = 0;
    
    std::memcpy(&pvrData[offset], &signature, 4); offset += 4;
    std::memcpy(&pvrData[offset], &flags, 4); offset += 4;
    std::memcpy(&pvrData[offset], &formatField, 8); offset += 8;
    std::memcpy(&pvrData[offset], &colorSpace, 4); offset += 4;
    std::memcpy(&pvrData[offset], &channelType, 4); offset += 4;
    std::memcpy(&pvrData[offset], &height, 4); offset += 4;
    std::memcpy(&pvrData[offset], &width, 4); offset += 4;
    std::memcpy(&pvrData[offset], &depth, 4); offset += 4;
    std::memcpy(&pvrData[offset], &numFaces, 4); offset += 4;
    std::memcpy(&pvrData[offset], &numSurfaces, 4); offset += 4;
    std::memcpy(&pvrData[offset], &numMipmaps, 4); offset += 4;
    std::memcpy(&pvrData[offset], &metaDataSize, 4); offset += 4;
    std::memcpy(&pvrData[offset], compressedData.data(), compressedData.size());
    
    // Now zlib-compress the entire PVR file (header + DXT data)
    std::vector<uint8_t> zlibData = compressWithZlib(pvrData);
    
    // Write the zlib-compressed PVR file directly
    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "PVRZ", "Failed to create output file: {}", outputPath);
        return false;
    }
    file.write(reinterpret_cast<const char*>(zlibData.data()), zlibData.size());
    file.close();
    
    // Log file size
    std::ifstream checkFile(outputPath, std::ios::binary | std::ios::ate);
    if (checkFile.is_open()) {
        std::streamsize fileSize = checkFile.tellg();
        Log(DEBUG, "PVRZ", "Wrote {} bytes zlib-compressed PVR file (original PVR: {} bytes), file size on disk: {} bytes", zlibData.size(), pvrData.size(), fileSize);
        checkFile.close();
    } else {
        Log(ERROR, "PVRZ", "Could not open {} to check file size after writing", outputPath);
    }
    
    return true;
}

} // namespace ProjectIE4k