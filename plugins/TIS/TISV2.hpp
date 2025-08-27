#ifndef TISV2_HPP
#define TISV2_HPP

namespace ProjectIE4k {

// TIS V2 file structure (PVRZ-based)
struct TISV2File {
    TISHeader header;
    std::vector<TISV2Tile> tiles;
    
    TISV2File() = default;
    
    // Calculate file size
    size_t calculateFileSize() const {
        size_t size = sizeof(TISHeader);
        size += tiles.size() * sizeof(TISV2Tile);
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
            data.insert(data.end(), tilePtr, tilePtr + sizeof(TISV2Tile));
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
        
        // Check if this is a PVRZ-based TIS (V2)
        if (!header.isPvrzBased()) {
            return false; // This is a palette-based TIS (V1)
        }
        
        size_t offset = sizeof(TISHeader);
        
        // Read tiles (each tile is 12 bytes for PVRZ)
        tiles.clear();
        tiles.reserve(header.tileCount);
        for (uint32_t i = 0; i < header.tileCount; i++) {
            if (offset + sizeof(TISV2Tile) > data.size()) {
                return false;
            }
            TISV2Tile tile;
            memcpy(&tile, data.data() + offset, sizeof(TISV2Tile));
            tiles.push_back(tile);
            offset += sizeof(TISV2Tile);
        }
        
        return true;
    }
};

} // namespace ProjectIE4k

#endif // TISV2_HPP