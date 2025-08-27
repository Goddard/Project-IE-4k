#ifndef PVRZ_CREATOR_H
#define PVRZ_CREATOR_H

#include <vector>
#include <string>
#include <cstdint>

namespace ProjectIE4k {

enum class PVRZFormat {
    DXT1,
    DXT5,
    AUTO
};

/**
 * @class PVRZ
 * Utility class for reading and writing PVRZ (compressed PVR) files.
 * Used by multiple file format plugins (BAM, TIS, MOS, etc.) to handle
 * texture atlases in the PVRZ format used by Enhanced Edition games.
 */
class PVRZ {
public:
    PVRZ();
    ~PVRZ();
    
    /**
     * Create a texture atlas PVRZ file from multiple PNG files
     * @param pngFiles List of PNG file paths to include in the atlas
     * @param outputPath Output path for the PVRZ file
     * @param atlasWidth Width of the texture atlas (will be adjusted to power of 2)
     * @param atlasHeight Height of the texture atlas (will be adjusted to power of 2)
     * @param format PVRZ compression format (DXT1 or DXT5)
     * @return true if successful, false otherwise
     */
    bool createTextureAtlasPVRZ(const std::vector<std::string>& pngFiles,
                               const std::string& outputPath,
                               int atlasWidth = 1024, int atlasHeight = 1024,
                               PVRZFormat format = PVRZFormat::DXT5);
    
    /**
     * Create a texture atlas PVRZ file directly from pixel data
     * @param tilePixels Vector of tile pixel data (each tile is 64x64 ARGB pixels)
     * @param framePositions List of (x,y) positions for each frame
     * @param outputPath Output path for the PVRZ file
     * @param atlasWidth Width of the texture atlas (will be adjusted to power of 2)
     * @param atlasHeight Height of the texture atlas (will be adjusted to power of 2)
     * @param format PVRZ compression format (DXT1 or DXT5)
     * @return true if successful, false otherwise
     */
    bool createTextureAtlasPVRZFromPixels(const std::vector<std::vector<uint32_t>>& tilePixels,
                                         const std::vector<std::pair<int, int>>& framePositions,
                                         const std::string& outputPath,
                                         int atlasWidth = 1024, int atlasHeight = 1024,
                                         PVRZFormat format = PVRZFormat::DXT5);
    
    /**
     * Create atlas data from PNG files with bin-packing positions
     * @param pngFiles List of PNG file paths
     * @param framePositions List of (x,y) positions for each frame
     * @param atlasData Output atlas pixel data
     * @param atlasWidth Output atlas width
     * @param atlasHeight Output atlas height
     * @return true if successful, false otherwise
     */
    bool createAtlasFromPNGsWithPositions(const std::vector<std::string>& pngFiles,
                                         const std::vector<std::pair<int, int>>& framePositions,
                                         std::vector<uint8_t>& atlasData,
                                         int& atlasWidth, int& atlasHeight);
    
    /**
     * Create atlas data directly from pixel data with positions
     * @param tilePixels Vector of tile pixel data (each tile is 64x64 ARGB pixels)
     * @param framePositions List of (x,y) positions for each frame
     * @param atlasData Output atlas pixel data
     * @param atlasWidth Output atlas width
     * @param atlasHeight Output atlas height
     * @return true if successful, false otherwise
     */
    bool createAtlasFromPixelsWithPositions(const std::vector<std::vector<uint32_t>>& tilePixels,
                                           const std::vector<std::pair<int, int>>& framePositions,
                                           std::vector<uint8_t>& atlasData,
                                           int& atlasWidth, int& atlasHeight);
    
    /**
     * Compress ARGB data to DXT format
     * @param argbData Input ARGB pixel data
     * @param width Image width
     * @param height Image height
     * @param format Compression format
     * @return Compressed DXT data
     */
    std::vector<uint8_t> compressToDXT(const std::vector<uint8_t>& argbData, 
                                      int width, int height, 
                                      PVRZFormat format);
    
    /**
     * Write PVRZ file from compressed data
     * @param outputPath Output file path
     * @param compressedData DXT compressed data
     * @param width Image width
     * @param height Image height
     * @param format Compression format
     * @return true if successful, false otherwise
     */
    bool writePVRZFile(const std::string& outputPath,
                      const std::vector<uint8_t>& compressedData,
                      int width, int height,
                      PVRZFormat format);
    
    /**
     * Read PVRZ file and extract DXT data
     * @param filePath Path to PVRZ file
     * @param dxtData Output DXT compressed data
     * @param width Output image width
     * @param height Output image height
     * @param format Output compression format
     * @return true if successful, false otherwise
     */
    bool readPVRZFile(const std::string& filePath,
                     std::vector<uint8_t>& dxtData,
                     int& width, int& height,
                     PVRZFormat& format);
    
    /**
     * Load PVRZ file from resource system
     * @param resourceName Resource name (e.g., "MOS0001")
     * @param dxtData Output DXT compressed data
     * @param width Output image width
     * @param height Output image height
     * @param format Output compression format
     * @return true if successful, false otherwise
     */
    bool loadPVRZResource(const std::string& resourceName,
                         std::vector<uint8_t>& dxtData,
                         int& width, int& height,
                         PVRZFormat& format);
    
    /**
     * Load PVRZ file from resource system and decode to ARGB
     * @param resourceName Resource name (e.g., "MOS0001")
     * @param argbData Output ARGB pixel data (4 bytes per pixel: A,R,G,B)
     * @param width Output image width
     * @param height Output image height
     * @return true if successful, false otherwise
     */
    bool loadPVRZResourceAsARGB(const std::string& resourceName,
                               std::vector<uint8_t>& argbData,
                               int& width, int& height);
    
    // Public utility methods
    static bool isValidPowerOfTwo(int value);
    static int nextPowerOfTwo(int value);
    static void unpackRGB565(uint16_t color, uint8_t& r, uint8_t& g, uint8_t& b);
    
    // DXT decoding methods
    static void decodeDXT1Block(const uint8_t* block, uint32_t* pixels);
    static void decodeDXT5Block(const uint8_t* block, uint32_t* pixels);
    
    // Binary reading utilities
    static uint32_t readLE32(const uint8_t* data);
    static uint64_t readLE64(const uint8_t* data);
    
private:
    // Compression and encoding methods
    std::vector<uint8_t> compressWithZlib(const std::vector<uint8_t>& data);
    bool needsAlphaInterpolation(const std::vector<uint8_t>& argbData, int width, int height);
    
    // DXT encoding methods
    void encodeDXT5Block(const uint32_t* pixels, uint8_t* output);
    void encodeDXT1Block(const uint32_t* pixels, uint8_t* output);
    uint16_t packRGB565(uint8_t r, uint8_t g, uint8_t b);

    // Atlas creation methods
    bool createAtlasFromPNGs(const std::vector<std::string>& pngFiles,
                            std::vector<uint8_t>& atlasData,
                            int& atlasWidth, int& atlasHeight,
                            std::vector<std::pair<int, int>>& framePositions,
                            std::vector<uint8_t>* firstPngARGB = nullptr);
    
    // Private utility methods
    std::vector<uint8_t> padToPowerOfTwo(const std::vector<uint8_t>& data, 
                                        int width, int height, 
                                        int& newWidth, int& newHeight);
};

} // namespace ProjectIE4k

#endif // PVRZ_CREATOR_H 