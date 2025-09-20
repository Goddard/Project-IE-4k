#include <cstring>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace ProjectIE4k {

// Night Light Map (LN) - 8-bit values representing nighttime lighting with palette
// Similar to Light Map (LM) but specifically for night lighting conditions
struct NightLightMap {
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> data; // 8-bit values
    std::vector<uint32_t> palette; // ARGB color palette (256 entries)
    
    NightLightMap() : width(0), height(0) {}
    NightLightMap(uint32_t w, uint32_t h) : width(w), height(h), data(w * h, 0), palette(256, 0) {}
    
    // Get an 8-bit value at position (x, y)
    uint8_t getValue(uint32_t x, uint32_t y) const {
        if (x >= width || y >= height) return 0;
        return data[y * width + x];
    }
    
    // Set an 8-bit value at position (x, y)
    void setValue(uint32_t x, uint32_t y, uint8_t value) {
        if (x >= width || y >= height) return;
        data[y * width + x] = value;
    }
    
    // Get color at position (x, y)
    uint32_t getColor(uint32_t x, uint32_t y) const {
        uint8_t index = getValue(x, y);
        return index < palette.size() ? palette[index] : 0;
    }
    
    // Set color at position (x, y)
    void setColor(uint32_t x, uint32_t y, uint32_t color) {
        // Find or create palette entry
        auto it = std::find(palette.begin(), palette.end(), color);
        uint8_t index;
        if (it != palette.end()) {
            index = static_cast<uint8_t>(std::distance(palette.begin(), it));
        } else {
            // Find first unused palette entry
            for (index = 0; index < palette.size(); index++) {
                if (palette[index] == 0) break;
            }
            if (index < palette.size()) {
                palette[index] = color;
            } else {
                // Palette full, use closest match
                index = findClosestColor(color);
            }
        }
        setValue(x, y, index);
    }
    
    // Serialize to binary data
    std::vector<uint8_t> serialize() const {
        // Create BMP file with 8-bit indexed color format
        std::vector<uint8_t> bmpData;
        
        // Calculate BMP header sizes
        uint32_t rowSize = width; // 8-bit
        uint32_t paddedRowSize = ((rowSize + 3) / 4) * 4; // Pad to 4-byte boundary
        uint32_t dataSize = paddedRowSize * height; // Total data size with padding
        uint32_t paletteSize = 256 * 4; // 256 colors * 4 bytes each (BGRA)
        uint32_t fileSize = 54 + paletteSize + dataSize;
        
        // BMP File Header (14 bytes)
        bmpData.push_back('B');
        bmpData.push_back('M');
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&fileSize), reinterpret_cast<const uint8_t*>(&fileSize) + 4);
        bmpData.insert(bmpData.end(), 4, 0); // Reserved
        uint32_t dataOffset = 54 + paletteSize;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&dataOffset), reinterpret_cast<const uint8_t*>(&dataOffset) + 4);
        
        // BMP Info Header (40 bytes)
        uint32_t headerSize = 40;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&headerSize), reinterpret_cast<const uint8_t*>(&headerSize) + 4);
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&width), reinterpret_cast<const uint8_t*>(&width) + 4);
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&height), reinterpret_cast<const uint8_t*>(&height) + 4);
        uint16_t planes = 1;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&planes), reinterpret_cast<const uint8_t*>(&planes) + 2);
        uint16_t bitCount = 8;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&bitCount), reinterpret_cast<const uint8_t*>(&bitCount) + 2);
        uint32_t compression = 0;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&compression), reinterpret_cast<const uint8_t*>(&compression) + 4);
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&dataSize), reinterpret_cast<const uint8_t*>(&dataSize) + 4);
        uint32_t xPixelsPerM = 0;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&xPixelsPerM), reinterpret_cast<const uint8_t*>(&xPixelsPerM) + 4);
        uint32_t yPixelsPerM = 0;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&yPixelsPerM), reinterpret_cast<const uint8_t*>(&yPixelsPerM) + 4);
        uint32_t colorsUsed = 256;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&colorsUsed), reinterpret_cast<const uint8_t*>(&colorsUsed) + 4);
        uint32_t importantColors = 256;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&importantColors), reinterpret_cast<const uint8_t*>(&importantColors) + 4);
        
        // Color Palette (256 colors in BGRA format)
        for (size_t i = 0; i < 256; i++) {
            uint32_t color = i < palette.size() ? palette[i] : 0;
            uint8_t a = (color >> 24) & 0xFF; // Alpha component
            uint8_t r = (color >> 16) & 0xFF; // Red component
            uint8_t g = (color >> 8) & 0xFF;  // Green component
            uint8_t b = color & 0xFF;         // Blue component
            bmpData.push_back(b);  // Blue first (BGR order)
            bmpData.push_back(g);  // Green second
            bmpData.push_back(r);  // Red third
            bmpData.push_back(a);  // Alpha last
        }
        
        // Image Data (8-bit, bottom-up with row padding)
        // For positive height, write rows from bottom to top
        for (int32_t y = 0; y < height; y++) {
            // Copy row data (y=0 is the bottom row in BMP)
            size_t rowStart = (height - 1 - y) * width;
            size_t rowEnd = (height - y) * width;
            bmpData.insert(bmpData.end(), data.begin() + rowStart, data.begin() + rowEnd);
            
            // Add padding if needed
            uint32_t padding = paddedRowSize - rowSize;
            for (uint32_t i = 0; i < padding; i++) {
                bmpData.push_back(0);
            }
        }
        
        return bmpData;
    }
    
    // Deserialize from binary data
    bool deserialize(const std::vector<uint8_t>& bmpData) {
        if (bmpData.size() < 54) return false;
        
        // Parse BMP header
        if (bmpData[0] != 'B' || bmpData[1] != 'M') return false;
        
        // Read dimensions
        memcpy(&width, bmpData.data() + 18, 4);
        memcpy(&height, bmpData.data() + 22, 4);
        
        // Read bit depth
        uint16_t bitCount;
        memcpy(&bitCount, bmpData.data() + 28, 2);
        if (bitCount != 8) return false;
        
        // Read data offset
        uint32_t dataOffset;
        memcpy(&dataOffset, bmpData.data() + 10, 4);
        
        // Extract palette
        palette.resize(256);
        for (size_t i = 0; i < 256; i++) {
            size_t paletteOffset = 54 + i * 4;
            if (paletteOffset + 3 >= bmpData.size()) return false;
            uint8_t b = bmpData[paletteOffset];
            uint8_t g = bmpData[paletteOffset + 1];
            uint8_t r = bmpData[paletteOffset + 2];
            uint8_t a = bmpData[paletteOffset + 3];
            palette[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        
        // Extract image data (handle row padding)
        uint32_t rowSize = width; // 8-bit
        uint32_t paddedRowSize = ((rowSize + 3) / 4) * 4; // Pad to 4-byte boundary
        uint32_t dataSize = paddedRowSize * height; // Total data size with padding
        
        if (dataOffset + dataSize > bmpData.size()) return false;
        
        data.clear();
        data.reserve(width * height);
        
        // For positive height, rows are stored bottom-up (last row first)
        for (int32_t y = 0; y < height; y++) {
            size_t rowOffset = dataOffset + (height - 1 - y) * paddedRowSize;
            if (rowOffset + rowSize > bmpData.size()) return false;
            
            data.insert(data.end(), 
                       bmpData.begin() + rowOffset, 
                       bmpData.begin() + rowOffset + rowSize);
        }
        
        return true;
    }
    
    // Upscale the night light map by the given factor using nearest-neighbor
    void upscale(uint32_t factor) {
        if (factor <= 1) return;
        
        uint32_t newWidth = width * factor;
        uint32_t newHeight = height * factor;
        std::vector<uint8_t> newData(newWidth * newHeight, 0);
        
        for (uint32_t y = 0; y < newHeight; y++) {
            for (uint32_t x = 0; x < newWidth; x++) {
                uint32_t srcX = x / factor;
                uint32_t srcY = y / factor;
                uint8_t value = getValue(srcX, srcY);
                newData[y * newWidth + x] = value;
            }
        }
        
        width = newWidth;
        height = newHeight;
        data = std::move(newData);
    }

private:
    uint8_t findClosestColor(uint32_t color) const {
        uint8_t closestIndex = 0;
        uint32_t minDistance = UINT32_MAX;
        
        for (size_t i = 0; i < palette.size(); i++) {
            uint32_t paletteColor = palette[i];
            uint32_t distance = 0;
            
            // Calculate color distance (simple RGB distance)
            for (int shift = 0; shift < 24; shift += 8) {
                int c1 = (color >> shift) & 0xFF;
                int c2 = (paletteColor >> shift) & 0xFF;
                int diff = c1 - c2;
                distance += diff * diff;
            }
            
            if (distance < minDistance) {
                minDistance = distance;
                closestIndex = static_cast<uint8_t>(i);
            }
        }
        
        return closestIndex;
    }
};

} // namespace ProjectIE4k
