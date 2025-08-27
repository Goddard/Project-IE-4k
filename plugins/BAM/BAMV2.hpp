#ifndef BAMV2_HPP
#define BAMV2_HPP

namespace ProjectIE4k {

// BAM V2 file format structures (serializable)
#pragma pack(push, 1) // Ensure no padding

struct BAMV2Header {
    char signature[4];    // "BAM "
    char version[4];      // "V2  "
    uint32_t frameCount;
    uint32_t cycleCount;
    uint32_t dataBlockCount;
    uint32_t frameEntriesOffset;
    uint32_t cycleEntriesOffset;
    uint32_t dataBlocksOffset;
    
    BAMV2Header() : frameCount(0), cycleCount(0), dataBlockCount(0), 
                   frameEntriesOffset(0), cycleEntriesOffset(0), dataBlocksOffset(0) {
        memcpy(signature, "BAM ", 4);
        memcpy(version, "V2  ", 4);
    }
    
    void setCounts(uint32_t frames, uint32_t cycles, uint32_t blocks) {
        frameCount = frames;
        cycleCount = cycles;
        dataBlockCount = blocks;
    }
    
    void setOffsets(uint32_t frameOff, uint32_t cycleOff, uint32_t blocksOff) {
        frameEntriesOffset = frameOff;
        cycleEntriesOffset = cycleOff;
        dataBlocksOffset = blocksOff;
    }
};

struct BAMV2FrameEntry {
    uint16_t width;
    uint16_t height;
    int16_t centerX;
    int16_t centerY;
    uint16_t dataBlockStartIndex;
    uint16_t dataBlockCount;
    
    BAMV2FrameEntry() : width(0), height(0), centerX(0), centerY(0), 
                       dataBlockStartIndex(0), dataBlockCount(0) {}
    
    void setDimensions(uint16_t w, uint16_t h, int16_t cx, int16_t cy) {
        width = w;
        height = h;
        centerX = cx;
        centerY = cy;
    }
    
    void setDataBlocks(uint16_t start, uint16_t count) {
        dataBlockStartIndex = start;
        dataBlockCount = count;
    }
};

struct BAMV2DataBlock {
    uint32_t pvrzPage;
    uint32_t sourceX;
    uint32_t sourceY;
    uint32_t width;
    uint32_t height;
    uint32_t targetX;
    uint32_t targetY;
    
    BAMV2DataBlock() : pvrzPage(0), sourceX(0), sourceY(0), width(0), height(0), targetX(0), targetY(0) {}
    
    void setSource(uint32_t page, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h) {
        pvrzPage = page;
        sourceX = sx;
        sourceY = sy;
        width = w;
        height = h;
    }
    
    void setTarget(uint32_t tx, uint32_t ty) {
        targetX = tx;
        targetY = ty;
    }
};

#pragma pack(pop) // Restore default packing

// BAM V2 file structure (in-memory representation)
struct BAMV2File {
    BAMV2Header header;
    std::vector<BAMV2FrameEntry> frameEntries;
    std::vector<BAMV1CycleEntry> cycleEntries; // Same as V1
    std::vector<BAMV2DataBlock> dataBlocks;
    
    BAMV2File() = default;
    
    // Calculate file size
    size_t calculateFileSize() const {
        size_t size = sizeof(BAMV2Header);
        size += frameEntries.size() * sizeof(BAMV2FrameEntry);
        size += cycleEntries.size() * sizeof(BAMV1CycleEntry);
        size += dataBlocks.size() * sizeof(BAMV2DataBlock);
        return size;
    }
    
    // Serialize to binary data
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        data.reserve(calculateFileSize());
        
        // Write header
        const uint8_t* headerPtr = reinterpret_cast<const uint8_t*>(&header);
        data.insert(data.end(), headerPtr, headerPtr + sizeof(BAMV2Header));
        
        // Write frame entries
        for (const auto& entry : frameEntries) {
            const uint8_t* entryPtr = reinterpret_cast<const uint8_t*>(&entry);
            data.insert(data.end(), entryPtr, entryPtr + sizeof(BAMV2FrameEntry));
        }
        
        // Write cycle entries
        for (const auto& entry : cycleEntries) {
            const uint8_t* entryPtr = reinterpret_cast<const uint8_t*>(&entry);
            data.insert(data.end(), entryPtr, entryPtr + sizeof(BAMV1CycleEntry));
        }
        
        // Write data blocks
        for (const auto& block : dataBlocks) {
            const uint8_t* blockPtr = reinterpret_cast<const uint8_t*>(&block);
            data.insert(data.end(), blockPtr, blockPtr + sizeof(BAMV2DataBlock));
        }
        
        return data;
    }
    
    // Deserialize from binary data
    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(BAMV2Header)) {
            return false;
        }
        
        // Read header
        memcpy(&header, data.data(), sizeof(BAMV2Header));
        
        // Validate signature
        if (memcmp(header.signature, "BAM ", 4) != 0) {
            return false;
        }
        
        // Validate version
        if (memcmp(header.version, "V2  ", 4) != 0) {
            return false;
        }
        
        size_t offset = sizeof(BAMV2Header);
        
        // Read frame entries
        frameEntries.clear();
        frameEntries.reserve(header.frameCount);
        for (uint32_t i = 0; i < header.frameCount; i++) {
            if (offset + sizeof(BAMV2FrameEntry) > data.size()) {
                return false;
            }
            BAMV2FrameEntry entry;
            memcpy(&entry, data.data() + offset, sizeof(BAMV2FrameEntry));
            frameEntries.push_back(entry);
            offset += sizeof(BAMV2FrameEntry);
        }
        
        // Read cycle entries
        cycleEntries.clear();
        cycleEntries.reserve(header.cycleCount);
        for (uint32_t i = 0; i < header.cycleCount; i++) {
            if (offset + sizeof(BAMV1CycleEntry) > data.size()) {
                return false;
            }
            BAMV1CycleEntry entry;
            memcpy(&entry, data.data() + offset, sizeof(BAMV1CycleEntry));
            cycleEntries.push_back(entry);
            offset += sizeof(BAMV1CycleEntry);
        }
        
        // Read data blocks
        dataBlocks.clear();
        dataBlocks.reserve(header.dataBlockCount);
        for (uint32_t i = 0; i < header.dataBlockCount; i++) {
            if (offset + sizeof(BAMV2DataBlock) > data.size()) {
                return false;
            }
            BAMV2DataBlock block;
            memcpy(&block, data.data() + offset, sizeof(BAMV2DataBlock));
            dataBlocks.push_back(block);
            offset += sizeof(BAMV2DataBlock);
        }
        
        return true;
    }
};

} // namespace ProjectIE4k

#endif // BAMV2_HPP