#ifndef TISV1_HPP
#define TISV1_HPP

#include "plugins/ColorReducer.h"

namespace ProjectIE4k {

// TIS V1 file format structures (serializable)
#pragma pack(push, 1) // Ensure no padding

struct TISHeader {
    char signature[4];    // "TIS "
    char version[4];      // "V1  " (can be misleading for PVRZ files)
    uint32_t tileCount;   // Number of tiles
    uint32_t tileSize;    // Size of each tile entry (5120 for V1, 12 for V2)
    uint32_t headerSize;  // Size of header (24 bytes)
    uint32_t tileDimension; // Tile dimension (64 pixels)
    
    TISHeader() : tileCount(0), tileSize(5120), headerSize(24), tileDimension(64) {
        memcpy(signature, "TIS ", 4);
        memcpy(version, "V1  ", 4);
    }
    
    void setTileCount(uint32_t count) {
        tileCount = count;
    }
    
    // Determine if this is a PVRZ-based TIS (V2) or palette-based TIS (V1)
    bool isPvrzBased() const {
        return tileSize == 12; // PVRZ tiles are 12 bytes each
    }
    
    bool isPaletteBased() const {
        return tileSize == 5120; // Palette tiles are 5120 bytes each (1024 + 64*64)
    }
};

// TIS V1 tile structure (5120 bytes total per tile)
struct TISV1Tile {
    uint8_t palette[256][4];  // 256 colors, 4 bytes each (BGRA order as per spec)
    uint8_t pixels[64][64];   // 64x64 pixel indices
    
    TISV1Tile() {
        memset(palette, 0, sizeof(palette));
        memset(pixels, 0, sizeof(pixels));
    }
    
    // Set color in BGRA order (as per TIS spec)
    void setColor(uint8_t index, uint8_t b, uint8_t g, uint8_t r, uint8_t a = 255) {
        palette[index][0] = b;  // Blue
        palette[index][1] = g;  // Green  
        palette[index][2] = r;  // Red
        palette[index][3] = a;  // Alpha
    }
    
    // Get color as ARGB (for our internal use)
    // Note: TIS format doesn't use palette alpha values, so we ignore them
    uint32_t getColor(uint8_t index) const {
        return ColorReducer::bgraToARGB(palette[index], index);
    }
    
    void setPixel(uint8_t x, uint8_t y, uint8_t index) {
        if (x < 64 && y < 64) {
            pixels[y][x] = index;
        }
    }
    
    uint8_t getPixel(uint8_t x, uint8_t y) const {
        if (x < 64 && y < 64) {
            return pixels[y][x];
        }
        return 0;
    }
};

#pragma pack(pop) // Restore default packing

// TIS V2 tile structure (12 bytes per tile for PVRZ-based)
struct TISV2Tile {
    uint32_t page;        // PVRZ page number
    uint32_t x;           // X coordinate in PVRZ
    uint32_t y;           // Y coordinate in PVRZ
    
    TISV2Tile() : page(0), x(0), y(0) {}
};

// TIS V1 file structure (in-memory representation)
struct TISV1File {
    TISHeader header;
    std::vector<TISV1Tile> tiles;
    
    TISV1File() = default;
    
    // Calculate file size
    size_t calculateFileSize() const {
        size_t size = sizeof(TISHeader);
        size += tiles.size() * sizeof(TISV1Tile);
        return size;
    }
    
    // Serialize to binary data
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        data.reserve(calculateFileSize());
        
        // Write header
        const uint8_t* headerPtr = reinterpret_cast<const uint8_t*>(&header);
        data.insert(data.end(), headerPtr, headerPtr + sizeof(TISHeader));
        
        // Write tiles
        for (const auto& tile : tiles) {
            const uint8_t* tilePtr = reinterpret_cast<const uint8_t*>(&tile);
            data.insert(data.end(), tilePtr, tilePtr + sizeof(TISV1Tile));
        }
        
        return data;
    }
    
    // Deserialize from binary data
    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(TISHeader)) {
            return false;
        }
        
        // Read header
        memcpy(&header, data.data(), sizeof(TISHeader));
        
        // Validate signature
        if (memcmp(header.signature, "TIS ", 4) != 0) {
            return false;
        }
        
        // Check if this is a palette-based TIS (V1)
        if (!header.isPaletteBased()) {
            return false; // This is a PVRZ-based TIS (V2)
        }
        
        size_t offset = sizeof(TISHeader);
        
        // Read tiles (each tile is 5120 bytes as per spec)
        tiles.clear();
        tiles.reserve(header.tileCount);
        for (uint32_t i = 0; i < header.tileCount; i++) {
            if (offset + sizeof(TISV1Tile) > data.size()) {
                return false;
            }
            TISV1Tile tile;
            memcpy(&tile, data.data() + offset, sizeof(TISV1Tile));
            tiles.push_back(tile);
            offset += sizeof(TISV1Tile);
        }
        
        return true;
    }
};

} // namespace ProjectIE4k

#endif // TISV1_HPP