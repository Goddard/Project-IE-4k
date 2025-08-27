#ifndef BAMV1_HPP
#define BAMV1_HPP

#include <cstdint>
#include <cstring>
#include <vector>

namespace ProjectIE4k {

// BAM V1 file format structures (serializable)
#pragma pack(push, 1) // Ensure no padding

struct BAMV1Header {
    char signature[4];        // 'BAM '
    char version[4];          // 'V1 '
    uint16_t frameCount;      // Count of frame entries
    uint8_t cycleCount;       // Count of cycles
    uint8_t compressedColor;  // The compressed colour index for RLE encoded bams
    uint32_t frameEntriesOffset;  // Offset to frame entries
    uint32_t paletteOffset;       // Offset to palette
    uint32_t frameLookupTableOffset; // Offset to frame lookup table
    
    void setCounts(uint32_t frames, uint32_t cycles) {
        frameCount = static_cast<uint16_t>(frames);
        cycleCount = static_cast<uint8_t>(cycles);
    }
};

struct BAMV1FrameEntry {
    uint16_t width;           // Frame width
    uint16_t height;          // Frame height
    int16_t centerX;          // Frame center X coordinate
    int16_t centerY;          // Frame center Y coordinate
    uint32_t dataOffset;      // bits 30-0: Offset to frame data, bit 31: 0=Compressed (RLE), 1=Uncompressed
    
    bool isRLE() const {
        return (dataOffset & 0x80000000) == 0;
    }
    
    uint32_t getDataOffset() const {
        return dataOffset & 0x7FFFFFFF;
    }
};

struct BAMV1CycleEntry {
    uint16_t frameCount;      // Count of frame indices in this cycle
    uint16_t firstFrame;      // Index into frame lookup table of first frame index in this cycle
    
    void setCycle(uint16_t count, uint16_t first) {
        frameCount = count;
        firstFrame = first;
    }
};

struct BAMV1PaletteEntry {
    uint8_t b, g, r, a;
    
    BAMV1PaletteEntry() : b(0), g(0), r(0), a(0) {}
    BAMV1PaletteEntry(uint8_t blue, uint8_t green, uint8_t red, uint8_t alpha) 
        : b(blue), g(green), r(red), a(alpha) {}
    
    // Convert from ARGB32
    static BAMV1PaletteEntry fromARGB(uint32_t argb) {
        return BAMV1PaletteEntry(
            (argb >> 0) & 0xFF,  // B
            (argb >> 8) & 0xFF,  // G
            (argb >> 16) & 0xFF, // R
            (argb >> 24) & 0xFF  // A
        );
    }
    
    // Convert to ARGB32
    uint32_t toARGB() const {
        // BAM V1 format: if alpha is 0, make it fully opaque
        uint8_t finalAlpha = (a == 0) ? 0xFF : a;
        return (finalAlpha << 24) | (r << 16) | (g << 8) | b;
    }
};

#pragma pack(pop) // Restore default packing

// BAM V1 file structure (in-memory representation)
struct BAMV1File {
    BAMV1Header header;
    std::vector<BAMV1FrameEntry> frameEntries;
    std::vector<BAMV1CycleEntry> cycleEntries;
    std::vector<BAMV1PaletteEntry> palette;
    std::vector<uint16_t> frameLookupTable;  // Array of frame indices
    std::vector<std::vector<uint8_t>> frameData;  // Frame data for each frame entry
    
    BAMV1File() = default;
    
    // Calculate file size
    size_t calculateFileSize() const {
        size_t size = sizeof(BAMV1Header);
        size += frameEntries.size() * sizeof(BAMV1FrameEntry);
        size += cycleEntries.size() * sizeof(BAMV1CycleEntry);
        size += palette.size() * sizeof(BAMV1PaletteEntry);
        size += frameLookupTable.size() * sizeof(uint16_t);
        
        // Add frame data sizes
        for (const auto& data : frameData) {
            size += data.size();
        }
        
        return size;
    }
    
    // Serialize to binary data
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        data.reserve(calculateFileSize());
        
        // Create a copy of the header for offset calculation
        BAMV1Header serializedHeader = header;
        
        // Calculate offsets
        size_t currentOffset = sizeof(BAMV1Header);
        serializedHeader.frameEntriesOffset = static_cast<uint32_t>(currentOffset);
        
        currentOffset += frameEntries.size() * sizeof(BAMV1FrameEntry);
        currentOffset += cycleEntries.size() * sizeof(BAMV1CycleEntry);
        serializedHeader.paletteOffset = static_cast<uint32_t>(currentOffset);
        
        currentOffset += palette.size() * sizeof(BAMV1PaletteEntry);
        serializedHeader.frameLookupTableOffset = static_cast<uint32_t>(currentOffset);
        
        currentOffset += frameLookupTable.size() * sizeof(uint16_t);
        size_t frameDataStart = currentOffset;
        
        // Write header
        const uint8_t* headerPtr = reinterpret_cast<const uint8_t*>(&serializedHeader);
        data.insert(data.end(), headerPtr, headerPtr + sizeof(BAMV1Header));
        
        // Write frame entries with updated offsets
        for (size_t i = 0; i < frameEntries.size(); i++) {
            BAMV1FrameEntry entry = frameEntries[i];
            
            // Set the data offset
            if (entry.dataOffset == 0) {
                // RLE compressed
                entry.dataOffset = static_cast<uint32_t>(frameDataStart);
                frameDataStart += frameData[i].size();
            } else {
                // Uncompressed
                entry.dataOffset = static_cast<uint32_t>(frameDataStart) | 0x80000000;
                frameDataStart += frameData[i].size();
            }
            
            const uint8_t* entryPtr = reinterpret_cast<const uint8_t*>(&entry);
            data.insert(data.end(), entryPtr, entryPtr + sizeof(BAMV1FrameEntry));
        }
        
        // Write cycle entries
        for (const auto& entry : cycleEntries) {
            const uint8_t* entryPtr = reinterpret_cast<const uint8_t*>(&entry);
            data.insert(data.end(), entryPtr, entryPtr + sizeof(BAMV1CycleEntry));
        }
        
        // Write palette
        for (const auto& entry : palette) {
            const uint8_t* entryPtr = reinterpret_cast<const uint8_t*>(&entry);
            data.insert(data.end(), entryPtr, entryPtr + sizeof(BAMV1PaletteEntry));
        }
        
        // Write Frame Lookup Table (FLT)
        for (const auto& fltEntry : frameLookupTable) {
            const uint8_t* entryPtr = reinterpret_cast<const uint8_t*>(&fltEntry);
            data.insert(data.end(), entryPtr, entryPtr + sizeof(uint16_t));
        }
        
        // Write frame data
        for (const auto& frameData : frameData) {
            data.insert(data.end(), frameData.begin(), frameData.end());
        }
        
        return data;
    }
    
    // Deserialize from binary data
    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(BAMV1Header)) {
            return false;
        }
        
        // Read header
        memcpy(&header, data.data(), sizeof(BAMV1Header));
        
        // Validate signature
        if (memcmp(header.signature, "BAM ", 4) != 0) {
            return false;
        }
        
        // Validate version
        if (memcmp(header.version, "V1  ", 4) != 0) {
            return false;
        }
        
        // Validate offsets
        if (header.frameEntriesOffset >= data.size() || 
            header.paletteOffset >= data.size() || 
            header.frameLookupTableOffset >= data.size()) {
            return false;
        }
        
        // Read frame entries
        frameEntries.clear();
        frameEntries.reserve(header.frameCount);
        size_t frameOffset = header.frameEntriesOffset;
        for (uint16_t i = 0; i < header.frameCount; i++) {
            if (frameOffset + sizeof(BAMV1FrameEntry) > data.size()) {
                return false;
            }
            BAMV1FrameEntry entry;
            memcpy(&entry, data.data() + frameOffset, sizeof(BAMV1FrameEntry));
            frameEntries.push_back(entry);
            frameOffset += sizeof(BAMV1FrameEntry);
        }
        
        // Read cycle entries (immediately after frame entries)
        cycleEntries.clear();
        cycleEntries.reserve(header.cycleCount);
        for (uint8_t i = 0; i < header.cycleCount; i++) {
            if (frameOffset + sizeof(BAMV1CycleEntry) > data.size()) {
                return false;
            }
            BAMV1CycleEntry entry;
            memcpy(&entry, data.data() + frameOffset, sizeof(BAMV1CycleEntry));
            cycleEntries.push_back(entry);
            frameOffset += sizeof(BAMV1CycleEntry);
        }
        
        // Read palette
        palette.clear();
        palette.reserve(256); // BAM V1 always has 256 palette entries
        size_t paletteOffset = header.paletteOffset;
        for (int i = 0; i < 256; i++) {
            if (paletteOffset + sizeof(BAMV1PaletteEntry) > data.size()) {
                return false;
            }
            BAMV1PaletteEntry entry;
            memcpy(&entry, data.data() + paletteOffset, sizeof(BAMV1PaletteEntry));
            palette.push_back(entry);
            paletteOffset += sizeof(BAMV1PaletteEntry);
        }
        
        // Read Frame Lookup Table (FLT)
        frameLookupTable.clear();
        size_t fltOffset = header.frameLookupTableOffset;
        
        // Calculate FLT size: find the largest value of start+count in cycle entries
        uint16_t maxFltIndex = 0;
        for (const auto& cycle : cycleEntries) {
            // Skip cycles with negative firstFrame (empty cycles)
            if (static_cast<int16_t>(cycle.firstFrame) < 0) {
                continue;
            }
            uint16_t endIndex = cycle.firstFrame + cycle.frameCount;
            if (endIndex > maxFltIndex) {
                maxFltIndex = endIndex;
            }
        }
        
        frameLookupTable.reserve(maxFltIndex);
        for (uint16_t i = 0; i < maxFltIndex; i++) {
            if (fltOffset + sizeof(uint16_t) > data.size()) {
                return false;
            }
            uint16_t fltEntry;
            memcpy(&fltEntry, data.data() + fltOffset, sizeof(uint16_t));
            frameLookupTable.push_back(fltEntry);
            fltOffset += sizeof(uint16_t);
        }
        
        // Read frame data for each frame entry
        frameData.clear();
        frameData.reserve(header.frameCount);
        
        // Debug: Log frame data reading start
        // Note: We can't use Log here since this is a header file, but we can add debug output in the calling code
        
        for (size_t frameIndex = 0; frameIndex < frameEntries.size(); ++frameIndex) {
            const auto& frameEntry = frameEntries[frameIndex];
            uint32_t dataOffset = frameEntry.getDataOffset();
            

            
            if (dataOffset > data.size()) {
                return false;
            }
            
            // If dataOffset == data.size(), this frame has no data (empty frame)
            if (dataOffset == data.size()) {
                frameData.push_back(std::vector<uint8_t>()); // Empty frame data
                continue;
            }
            
            // Calculate frame data size
            size_t frameDataSize = 0;
            if (frameEntry.isRLE()) {
                // For RLE data, we need to decode to determine size
                // For now, read until we find the end or hit the next frame's data
                size_t nextFrameOffset = data.size();
                for (const auto& otherFrame : frameEntries) {
                    uint32_t otherOffset = otherFrame.getDataOffset();
                    if (otherOffset > dataOffset && otherOffset < nextFrameOffset) {
                        nextFrameOffset = otherOffset;
                    }
                }
                frameDataSize = nextFrameOffset - dataOffset;
            } else {
                // Uncompressed data: width * height bytes
                frameDataSize = frameEntry.width * frameEntry.height;
            }
            
            // Ensure we don't read beyond the data bounds
            if (dataOffset >= data.size()) {
                return false;
            }
            
            // Adjust frame data size if it would exceed data bounds
            if (dataOffset + frameDataSize > data.size()) {
                frameDataSize = data.size() - dataOffset;
            }
            
            std::vector<uint8_t> frameBytes(data.begin() + dataOffset, 
                                           data.begin() + dataOffset + frameDataSize);
            frameData.push_back(frameBytes);
        }
        
        return true;
    }
};

} // namespace ProjectIE4k

#endif // BAMV1_HPP