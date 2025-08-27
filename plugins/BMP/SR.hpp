#include <cstring>
#include <vector>
#include <cstdint>

namespace ProjectIE4k {
// Search Map (SR) - 4-bit values representing terrain types and walkability
// Values 0-15 correspond to different terrain types as documented in IESDP
struct SearchMap {
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> data; // 4-bit values packed into bytes
    
    SearchMap() : width(0), height(0) {}
    SearchMap(uint32_t w, uint32_t h) : width(w), height(h), data((w * h + 1) / 2, 0) {}
    
    // Get a 4-bit value at position (x, y)
    uint8_t getValue(uint32_t x, uint32_t y) const {
        if (x >= width || y >= height) return 0;
        size_t index = y * width + x;
        size_t byteIndex = index / 2;
        size_t bitOffset = (index % 2) * 4;
        return (data[byteIndex] >> bitOffset) & 0x0F;
    }
    
    // Set a 4-bit value at position (x, y)
    void setValue(uint32_t x, uint32_t y, uint8_t value) {
        if (x >= width || y >= height) return;
        value &= 0x0F; // Ensure 4-bit value
        size_t index = y * width + x;
        size_t byteIndex = index / 2;
        size_t bitOffset = (index % 2) * 4;
        uint8_t mask = 0x0F << bitOffset;
        data[byteIndex] = (data[byteIndex] & ~mask) | (value << bitOffset);
    }
    
    // Serialize to binary data
    std::vector<uint8_t> serialize() const {
        // Create BMP file with 4-bit indexed color format
        std::vector<uint8_t> bmpData;
        
        // Calculate BMP header sizes
        uint32_t rowSize = (width + 1) / 2; // 4-bit packed
        uint32_t paddedRowSize = ((rowSize + 3) / 4) * 4; // Pad to 4-byte boundary
        uint32_t dataSize = paddedRowSize * height; // Total data size with padding
        uint32_t paletteSize = 16 * 4; // 16 colors * 4 bytes each (BGRA)
        uint32_t fileSize = 54 + paletteSize + dataSize; // 54-byte header + palette + data
        
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
        uint16_t bitCount = 4;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&bitCount), reinterpret_cast<const uint8_t*>(&bitCount) + 2);
        uint32_t compression = 0;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&compression), reinterpret_cast<const uint8_t*>(&compression) + 4);
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&dataSize), reinterpret_cast<const uint8_t*>(&dataSize) + 4);
        uint32_t xPixelsPerM = 0;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&xPixelsPerM), reinterpret_cast<const uint8_t*>(&xPixelsPerM) + 4);
        uint32_t yPixelsPerM = 0;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&yPixelsPerM), reinterpret_cast<const uint8_t*>(&yPixelsPerM) + 4);
        uint32_t colorsUsed = 16;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&colorsUsed), reinterpret_cast<const uint8_t*>(&colorsUsed) + 4);
        uint32_t importantColors = 16;
        bmpData.insert(bmpData.end(), reinterpret_cast<const uint8_t*>(&importantColors), reinterpret_cast<const uint8_t*>(&importantColors) + 4);
        
        // Color Palette (16 colors in BGRA format)
        for (int i = 0; i < 16; i++) {
            uint8_t b = i * 17; // Blue component
            uint8_t g = i * 17; // Green component  
            uint8_t r = i * 17; // Red component
            uint8_t a = 255;    // Alpha component
            bmpData.push_back(b);
            bmpData.push_back(g);
            bmpData.push_back(r);
            bmpData.push_back(a);
        }
        
        // Image Data (4-bit packed, bottom-up with row padding)
        // For positive height, write rows from bottom to top
        for (int32_t y = 0; y < height; y++) {
            // Copy row data (y=0 is the bottom row in BMP)
            size_t rowStart = ((height - 1 - y) * width) / 2;
            size_t rowEnd = ((height - y) * width + 1) / 2;
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
        if (bitCount != 4) return false;
        
        // Read data offset
        uint32_t dataOffset;
        memcpy(&dataOffset, bmpData.data() + 10, 4);
        
        // Calculate data size (with padding)
        uint32_t rowSize = (width + 1) / 2; // 4-bit packed
        uint32_t paddedRowSize = ((rowSize + 3) / 4) * 4; // Pad to 4-byte boundary
        uint32_t dataSize = paddedRowSize * height; // Total data size with padding
        
        // Extract image data (handle row padding)
        if (dataOffset + dataSize > bmpData.size()) return false;
        
        data.clear();
        data.reserve((width * height + 1) / 2);
        
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
    
    // Upscale the search map by the given factor using nearest-neighbor
    void upscale(uint32_t factor) {
        if (factor <= 1) return;
        
        uint32_t newWidth = width * factor;
        uint32_t newHeight = height * factor;
        std::vector<uint8_t> newData((newWidth * newHeight + 1) / 2, 0);
        
        for (uint32_t y = 0; y < newHeight; y++) {
            for (uint32_t x = 0; x < newWidth; x++) {
                uint32_t srcX = x / factor;
                uint32_t srcY = y / factor;
                uint8_t value = getValue(srcX, srcY);
                
                // Set value in new data
                size_t index = y * newWidth + x;
                size_t byteIndex = index / 2;
                size_t bitOffset = (index % 2) * 4;
                uint8_t mask = 0x0F << bitOffset;
                newData[byteIndex] = (newData[byteIndex] & ~mask) | (value << bitOffset);
            }
        }
        
        width = newWidth;
        height = newHeight;
        data = std::move(newData);
    }
};

} // namespace ProjectIE4k