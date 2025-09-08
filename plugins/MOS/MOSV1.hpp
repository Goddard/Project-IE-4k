#ifndef MOSV1_H
#define MOSV1_H

#include <cstdint>
#include <cstring>
#include <vector>

namespace ProjectIE4k {

// MOS V1 file format structures (serializable)
#pragma pack(push, 1) // Ensure no padding

struct MOSHeader {
    char signature[4];    // "MOS "
    char version[4];      // "V1  "
    uint16_t width;       // Image width
    uint16_t height;      // Image height
    uint16_t cols;        // Number of columns (tiles)
    uint16_t rows;        // Number of rows (tiles)
    uint32_t tileSize;    // Tile size (64)
    uint32_t paletteOffset; // Offset to palette data

    MOSHeader() : width(0), height(0), cols(0), rows(0), tileSize(64), paletteOffset(24) {
        memcpy(signature, "MOS ", 4);
        memcpy(version, "V1  ", 4);
    }
    
    void setDimensions(uint16_t w, uint16_t h, uint16_t c, uint16_t r) {
        width = w;
        height = h;
        cols = c;
        rows = r;
    }
};

struct MOSTileEntry {
    uint32_t dataOffset;  // Offset to tile data
    
    MOSTileEntry() : dataOffset(0) {}
    MOSTileEntry(uint32_t offset) : dataOffset(offset) {}
};

struct PaletteEntry {
    uint8_t b, g, r, a;
    
    PaletteEntry() : b(0), g(0), r(0), a(0) {}
    PaletteEntry(uint8_t blue, uint8_t green, uint8_t red, uint8_t alpha) 
        : b(blue), g(green), r(red), a(alpha) {}
    
    // Convert from ARGB32
    static PaletteEntry fromARGB(uint32_t argb) {
        return PaletteEntry(
            (argb >> 0) & 0xFF,  // B
            (argb >> 8) & 0xFF,  // G
            (argb >> 16) & 0xFF, // R
            (argb >> 24) & 0xFF  // A
        );
    }
    
    // Convert to ARGB32
    uint32_t toARGB() const {
        // In MOS V1 format, if green channel is 255, it indicates transparency
        if (g == 255) {
            return 0; // Fully transparent
        }
        // Ensure we have proper alpha - if alpha is very low, make it fully opaque
        uint8_t finalAlpha = (a < 128) ? 0xFF : a;
        return (finalAlpha << 24) | (r << 16) | (g << 8) | b;
    }
};

// MOSC V1 header structure
struct MOSCHeader {
    char signature[4];    // "MOSC"
    char version[4];      // "V1  "
    uint32_t uncompressedSize;
    
    MOSCHeader() : uncompressedSize(0) {
        memcpy(signature, "MOSC", 4);
        memcpy(version, "V1  ", 4);
    }
    
    void setUncompressedSize(uint32_t size) {
        uncompressedSize = size;
    }
};

#pragma pack(pop) // Restore default packing

// MOS V1 file structure (in-memory representation)
struct MOSV1File {
    MOSHeader header;
    std::vector<std::vector<PaletteEntry>> tilePalettes; // 256 entries per tile
    std::vector<MOSTileEntry> tileEntries;
    std::vector<std::vector<uint8_t>> tileData; // Pixel indices per tile
    
    MOSV1File() = default;
    
    // Calculate file size
    size_t calculateFileSize() const {
        size_t size = sizeof(MOSHeader);
        size += tilePalettes.size() * 256 * sizeof(PaletteEntry); // All palettes
        size += tileEntries.size() * sizeof(MOSTileEntry); // Tile entries
        for (const auto& data : tileData) {
            size += data.size(); // Tile data
        }
        return size;
    }
    
    // Serialize to binary data
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        data.reserve(calculateFileSize());
        
        // Write header
        const uint8_t* headerPtr = reinterpret_cast<const uint8_t*>(&header);
        data.insert(data.end(), headerPtr, headerPtr + sizeof(MOSHeader));
        
        // Write palettes
        for (const auto& palette : tilePalettes) {
            for (const auto& entry : palette) {
                const uint8_t* entryPtr = reinterpret_cast<const uint8_t*>(&entry);
                data.insert(data.end(), entryPtr, entryPtr + sizeof(PaletteEntry));
            }
        }
        
        // Write tile entries
        for (const auto& entry : tileEntries) {
            const uint8_t* entryPtr = reinterpret_cast<const uint8_t*>(&entry);
            data.insert(data.end(), entryPtr, entryPtr + sizeof(MOSTileEntry));
        }
        
        // Write tile data
        for (const auto& tileData : tileData) {
            data.insert(data.end(), tileData.begin(), tileData.end());
        }
        
        return data;
    }
    
    // Deserialize from binary data
    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(MOSHeader)) {
            return false;
        }
        
        // Read header
        memcpy(&header, data.data(), sizeof(MOSHeader));
        
        // Validate signature and version
        if (memcmp(header.signature, "MOS ", 4) != 0 ||
            memcmp(header.version, "V1  ", 4) != 0) {
            return false;
        }
        
        size_t offset = sizeof(MOSHeader);
        int tileCount = header.cols * header.rows;
        
        // Read palettes
        tilePalettes.clear();
        tilePalettes.reserve(tileCount);
        for (int i = 0; i < tileCount; i++) {
            std::vector<PaletteEntry> palette;
            palette.reserve(256);
            for (int j = 0; j < 256; j++) {
                if (offset + sizeof(PaletteEntry) > data.size()) {
                    return false;
                }
                PaletteEntry entry;
                memcpy(&entry, data.data() + offset, sizeof(PaletteEntry));
                palette.push_back(entry);
                offset += sizeof(PaletteEntry);
            }
            tilePalettes.push_back(palette);
        }
        
        // Read tile entries
        tileEntries.clear();
        tileEntries.reserve(tileCount);
        for (int i = 0; i < tileCount; i++) {
            if (offset + sizeof(MOSTileEntry) > data.size()) {
                return false;
            }
            MOSTileEntry entry;
            memcpy(&entry, data.data() + offset, sizeof(MOSTileEntry));
            tileEntries.push_back(entry);
            offset += sizeof(MOSTileEntry);
        }
        
        // Read tile data
        tileData.clear();
        tileData.reserve(tileCount);
        for (int i = 0; i < tileCount; i++) {
            if (i < tileCount - 1) {
                // Calculate tile data size from next offset
                size_t tileSize = tileEntries[i + 1].dataOffset - tileEntries[i].dataOffset;
                if (offset + tileSize > data.size()) {
                    return false;
                }
                std::vector<uint8_t> tile(tileSize);
                memcpy(tile.data(), data.data() + offset, tileSize);
                tileData.push_back(tile);
                offset += tileSize;
            } else {
                // Last tile - read to end
                size_t tileSize = data.size() - offset;
                std::vector<uint8_t> tile(tileSize);
                memcpy(tile.data(), data.data() + offset, tileSize);
                tileData.push_back(tile);
            }
        }
        
        return true;
    }
};

} // namespace ProjectIE4k

#endif // MOSV1_HPP