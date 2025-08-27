#ifndef MOSV2_HPP
#define MOSV2_HPP

namespace ProjectIE4k {

// MOS V2 file format structures (serializable)
#pragma pack(push, 1) // Ensure no padding

struct MOSV2Header {
    char signature[4];    // "MOS "
    char version[4];      // "V2 "
    uint32_t width;       // Image width
    uint32_t height;      // Image height
    uint32_t dataBlockCount; // Number of data blocks
    uint32_t dataBlockOffset; // Offset to data blocks
    
    MOSV2Header() : width(0), height(0), dataBlockCount(0), dataBlockOffset(24) {
        memcpy(signature, "MOS ", 4);
        memcpy(version, "V2  ", 4); // Two spaces to match actual format
    }
    
    void setDimensions(uint32_t w, uint32_t h, uint32_t count) {
        width = w;
        height = h;
        dataBlockCount = count;
    }
};

struct MOSV2DataBlock {
    uint32_t pvrzPage;    // PVRZ page number
    uint32_t sourceX;     // Source x coordinate
    uint32_t sourceY;     // Source y coordinate
    uint32_t width;       // Width
    uint32_t height;      // Height
    uint32_t targetX;     // Target x coordinate
    uint32_t targetY;     // Target y coordinate
    
    MOSV2DataBlock() : pvrzPage(0), sourceX(0), sourceY(0), width(0), height(0), targetX(0), targetY(0) {}
    
    MOSV2DataBlock(uint32_t page, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t tx, uint32_t ty)
        : pvrzPage(page), sourceX(sx), sourceY(sy), width(w), height(h), targetX(tx), targetY(ty) {}
};

#pragma pack(pop) // Restore default packing

// MOS V2 file structure (in-memory representation)
struct MOSV2File {
    MOSV2Header header;
    std::vector<MOSV2DataBlock> dataBlocks;
    
    MOSV2File() = default;
    
    // Calculate file size
    size_t calculateFileSize() const {
        return sizeof(MOSV2Header) + dataBlocks.size() * sizeof(MOSV2DataBlock);
    }
    
    // Serialize to binary data
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        data.reserve(calculateFileSize());
        
        // Write header
        const uint8_t* headerPtr = reinterpret_cast<const uint8_t*>(&header);
        data.insert(data.end(), headerPtr, headerPtr + sizeof(MOSV2Header));
        
        // Write data blocks
        for (const auto& block : dataBlocks) {
            const uint8_t* blockPtr = reinterpret_cast<const uint8_t*>(&block);
            data.insert(data.end(), blockPtr, blockPtr + sizeof(MOSV2DataBlock));
        }
        
        return data;
    }
    
    // Deserialize from binary data
    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(MOSV2Header)) {
            return false;
        }
        
        // Read header
        memcpy(&header, data.data(), sizeof(MOSV2Header));
        
        // Validate signature
        if (memcmp(header.signature, "MOS ", 4) != 0) {
            return false;
        }
        
        // Validate version (allow for trailing spaces)
        if (memcmp(header.version, "V2", 2) != 0) {
            return false;
        }
        
        // Use the data block offset from header, or default to header size
        size_t offset = (header.dataBlockOffset > 0) ? header.dataBlockOffset : sizeof(MOSV2Header);
        
        // Read data blocks
        dataBlocks.clear();
        dataBlocks.reserve(header.dataBlockCount);
        for (uint32_t i = 0; i < header.dataBlockCount; i++) {
            if (offset + sizeof(MOSV2DataBlock) > data.size()) {
                return false;
            }
            MOSV2DataBlock block;
            memcpy(&block, data.data() + offset, sizeof(MOSV2DataBlock));
            dataBlocks.push_back(block);
            offset += sizeof(MOSV2DataBlock);
        }
        
        return true;
    }
};

} // namespace ProjectIE4k

#endif // MOSV2_HPP