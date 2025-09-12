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
    
    // Helpers for packed 4-bit (BMP stores left pixel in HIGH nibble, right pixel in LOW nibble)
    inline uint8_t readNibble(size_t index) const {
        size_t byteIndex = index / 2;
        bool left = (index % 2 == 0); // even index -> left pixel -> high nibble
        uint8_t byte = data[byteIndex];
        return left ? ((byte >> 4) & 0x0F) : (byte & 0x0F);
    }
    inline void writeNibble(std::vector<uint8_t>& buf, size_t index, uint8_t value) const {
        value &= 0x0F;
        size_t byteIndex = index / 2;
        bool left = (index % 2 == 0);
        uint8_t byte = buf[byteIndex];
        if (left) {
            byte = (uint8_t)((byte & 0x0F) | (value << 4));
        } else {
            byte = (uint8_t)((byte & 0xF0) | value);
        }
        buf[byteIndex] = byte;
    }
    
    // Get a 4-bit value at position (x, y)
    uint8_t getValue(uint32_t x, uint32_t y) const {
        if (x >= width || y >= height) return 0;
        size_t index = (size_t)y * width + (size_t)x;
        return readNibble(index);
    }
    
    // Set a 4-bit value at position (x, y)
    void setValue(uint32_t x, uint32_t y, uint8_t value) {
        if (x >= width || y >= height) return;
        size_t index = (size_t)y * width + (size_t)x;
        writeNibble(data, index, value);
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
                size_t index = (size_t)y * newWidth + (size_t)x;
                writeNibble(newData, index, value);
            }
        }
        
        width = newWidth;
        height = newHeight;
        data = std::move(newData);
    }

    // Upscale with thin-lane preservation
    // 1) Nearest-neighbor upscale
    // 2) Dilate passable mask by a small radius to avoid pinching narrow corridors
    // blockedValue: value considered non-walkable (default 0x0F per common IE convention)
    // openFillValue: value to assign for newly opened pixels (default 0x00 fully walkable)
    void upscalePreserveLanes(uint32_t factor, uint8_t blockedValue = 0x0F, uint8_t openFillValue = 0x00, int dilationIters = 1) {
        if (factor <= 1) return;

        // Step 1: regular upscale
        uint32_t newWidth = width * factor;
        uint32_t newHeight = height * factor;
        std::vector<uint8_t> upscaled((newWidth * newHeight + 1) / 2, 0);

        for (uint32_t y = 0; y < newHeight; y++) {
            for (uint32_t x = 0; x < newWidth; x++) {
                uint32_t srcX = x / factor;
                uint32_t srcY = y / factor;
                uint8_t value = getValue(srcX, srcY);
                size_t index = (size_t)y * newWidth + (size_t)x;
                writeNibble(upscaled, index, value);
            }
        }

        // Step 2: build passable mask and dilate to preserve thin lanes
        std::vector<uint8_t> mask(newWidth * newHeight, 0);
        for (uint32_t y = 0; y < newHeight; ++y) {
            for (uint32_t x = 0; x < newWidth; ++x) {
                size_t idx = (size_t)y * newWidth + (size_t)x;
                uint8_t v = readNibble(idx);
                mask[idx] = (v != blockedValue) ? 1 : 0;
            }
        }

        auto inBounds = [&](int xx, int yy){ return xx >= 0 && yy >= 0 && (uint32_t)xx < newWidth && (uint32_t)yy < newHeight; };

        // Simple 8-connected dilation for passable cells
        for (int it = 0; it < dilationIters; ++it) {
            std::vector<uint8_t> next = mask;
            for (int yy = 0; yy < (int)newHeight; ++yy) {
                for (int xx = 0; xx < (int)newWidth; ++xx) {
                    size_t idx = (size_t)yy * newWidth + (size_t)xx;
                    if (mask[idx]) { next[idx] = 1; continue; }
                    // If any neighbor is passable, make this passable
                    bool neigh = false;
                    for (int dy = -1; dy <= 1 && !neigh; ++dy) {
                        for (int dx = -1; dx <= 1 && !neigh; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            int nx = xx + dx, ny = yy + dy;
                            if (inBounds(nx, ny)) {
                                if (mask[(size_t)ny * newWidth + (size_t)nx]) neigh = true;
                            }
                        }
                    }
                    if (neigh) next[idx] = 1;
                }
            }
            mask.swap(next);
        }

        // Step 3: write back. Preserve existing values; for newly opened cells, assign openFillValue
        for (uint32_t y = 0; y < newHeight; ++y) {
            for (uint32_t x = 0; x < newWidth; ++x) {
                size_t idx = (size_t)y * newWidth + (size_t)x;
                uint8_t cur = readNibble(idx);
                if (mask[idx] && cur == blockedValue) {
                    uint8_t nv = openFillValue & 0x0F;
                    writeNibble(upscaled, idx, nv);
                }
            }
        }

        width = newWidth;
        height = newHeight;
        data = std::move(upscaled);
    }

    // Upscale with conservative neighborhood sampling: for each destination pixel,
    // map to source (sx, sy) and assign the MIN value in a (2*r+1)^2 neighborhood around (sx, sy).
    // This biases toward keeping walkable values when there is any thin connection nearby.
    void upscaleConservative(uint32_t factor, int neighborhoodRadius = 1) {
        if (factor <= 1) return;
        if (neighborhoodRadius < 0) neighborhoodRadius = 0;

        const uint32_t newWidth = width * factor;
        const uint32_t newHeight = height * factor;
        std::vector<uint8_t> newData((newWidth * newHeight + 1) / 2, 0);

        auto sampleMinInNeighborhood = [&](uint32_t sx, uint32_t sy) -> uint8_t {
            uint8_t best = 0x0F;
            int r = neighborhoodRadius;
            for (int dy = -r; dy <= r; ++dy) {
                int y = (int)sy + dy;
                if (y < 0 || (uint32_t)y >= height) continue;
                for (int dx = -r; dx <= r; ++dx) {
                    int x = (int)sx + dx;
                    if (x < 0 || (uint32_t)x >= width) continue;
                    uint8_t v = getValue((uint32_t)x, (uint32_t)y) & 0x0F;
                    if (v < best) best = v;
                    if (best == 0) return best; // early exit
                }
            }
            return best;
        };

        for (uint32_t y = 0; y < newHeight; ++y) {
            for (uint32_t x = 0; x < newWidth; ++x) {
                uint32_t sx = x / factor;
                uint32_t sy = y / factor;
                uint8_t value = sampleMinInNeighborhood(sx, sy);

                size_t index = (size_t)y * newWidth + (size_t)x;
                writeNibble(newData, index, value & 0x0F);
            }
        }

        width = newWidth;
        height = newHeight;
        data = std::move(newData);
    }
};

} // namespace ProjectIE4k