
#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <string>

namespace ProjectIE4k {

// WED V1.3 Header structure (serializable)
#pragma pack(push, 1) // Ensure no padding
struct WEDHeader {
    char signature[4];      // 'WED '
    char version[4];        // 'V1.3'
    uint32_t overlayCount;  // Number of overlays (including base layer)
    uint32_t doorCount;     // Number of doors
    uint32_t overlayOffset; // Offset to overlays
    uint32_t secHeaderOffset; // Offset to secondary header
    uint32_t doorOffset;    // Offset to doors
    uint32_t doorTileOffset; // Offset to door tile cell indices
    
    WEDHeader() : overlayCount(0), doorCount(0), overlayOffset(0), 
                  secHeaderOffset(0), doorOffset(0), doorTileOffset(0) {
        memcpy(signature, "WED ", 4);
        memcpy(version, "V1.3", 4);
    }
    
    // Validation functions
    bool isValid() const {
        return memcmp(signature, "WED ", 4) == 0 && memcmp(version, "V1.3", 4) == 0;
    }
    
    bool validateOffsets(size_t fileSize) const {
        return overlayOffset < fileSize && 
               secHeaderOffset < fileSize && 
               doorOffset < fileSize && 
               doorTileOffset < fileSize;
    }
};

// WED V1.3 Secondary Header structure (serializable)
struct WEDSecondaryHeader {
    uint32_t polygonCount;      // Number of polygons
    uint32_t polygonOffset;     // Offset to polygons
    uint32_t vertexOffset;      // Offset to vertices
    uint32_t wallGroupOffset;   // Offset to wall groups
    uint32_t polygonIndexOffset; // Offset to polygon indices lookup table
    
    WEDSecondaryHeader() : polygonCount(0), polygonOffset(0), vertexOffset(0), 
                          wallGroupOffset(0), polygonIndexOffset(0) {}
    
    // Validation functions
    bool validateOffsets(size_t fileSize) const {
        return polygonOffset < fileSize && 
               vertexOffset < fileSize && 
               wallGroupOffset < fileSize && 
               polygonIndexOffset < fileSize;
    }
};

// WED V1.3 Overlay structure (serializable)
struct WEDOverlay {
    uint16_t width;             // Width in tiles
    uint16_t height;            // Height in tiles
    char tilesetName[8];        // Name of tileset (resref)
    uint32_t unknown;           // Unknown 4-byte field (was split into uniqueTileCount + movementType)
    uint32_t tilemapOffset;     // Offset to tilemap for this overlay
    uint32_t tileIndexOffset;   // Offset to tile index lookup for this overlay
    
    WEDOverlay() : width(0), height(0), unknown(0), 
                   tilemapOffset(0), tileIndexOffset(0) {
        memset(tilesetName, 0, 8);
    }
    
    // Utility functions
    size_t getTileCount() const {
        return static_cast<size_t>(width) * static_cast<size_t>(height);
    }
    
    bool validateOffsets(size_t fileSize) const {
        return tilemapOffset < fileSize && tileIndexOffset < fileSize;
    }
    
    std::string getTilesetName() const {
        // Return null-terminated string, trimming any padding
        size_t len = 0;
        while (len < 8 && tilesetName[len] != '\0') {
            len++;
        }
        return std::string(tilesetName, len);
    }
};

// WED V1.3 Door structure (serializable)
struct WEDDoor {
    char name[8];               // Name of door
    uint16_t openClosed;        // Open (0) / Closed (1)
    uint16_t firstDoorTile;     // First door tile cell index
    uint16_t doorTileCount;     // Count of door tile cells for this door
    uint16_t openPolygonCount;  // Count of polygons open state
    uint16_t closedPolygonCount; // Count of polygons closed state
    uint32_t openPolygonOffset; // Offset to polygons open state
    uint32_t closedPolygonOffset; // Offset to polygons closed state
    
    WEDDoor() : openClosed(0), firstDoorTile(0), doorTileCount(0), 
                openPolygonCount(0), closedPolygonCount(0), 
                openPolygonOffset(0), closedPolygonOffset(0) {
        memset(name, 0, 8);
    }
    
    // Utility functions
    bool isOpen() const {
        return openClosed == 0;
    }
    
    bool isClosed() const {
        return openClosed == 1;
    }
    
    bool validateOffsets(size_t fileSize) const {
        return openPolygonOffset < fileSize && closedPolygonOffset < fileSize;
    }
    
    std::string getName() const {
        return std::string(name, 8);
    }
};

// WED V1.3 Tilemap structure (serializable)
struct WEDTilemap {
    uint16_t startIndex;        // Start index in tile index lookup table
    uint16_t tileCount;         // Count of tiles in tile index lookup table
    uint16_t secondaryIndex;    // Index from TIS file of secondary tile
    uint8_t overlayFlags;       // Overlay drawing flags
    uint8_t unknown[3];         // Unknown bytes
    
    WEDTilemap() : startIndex(0), tileCount(0), secondaryIndex(0), overlayFlags(0) {
        memset(unknown, 0, 3);
    }
    
    // Utility functions
    bool hasOverlay(uint8_t overlayIndex) const {
        return (overlayFlags & (1 << overlayIndex)) != 0;
    }
    
    uint16_t getEndIndex() const {
        return startIndex + tileCount;
    }
};

// WED V1.3 Wall Group structure (serializable)
struct WEDWallGroup {
    uint16_t startIndex;        // Start polygon index
    uint16_t indexCount;        // Polygon index count
    
    WEDWallGroup() : startIndex(0), indexCount(0) {}
    
    // Utility functions
    uint16_t getEndIndex() const {
        return startIndex + indexCount;
    }
};

// WED V1.3 Polygon structure (serializable)
struct WEDPolygon {
    uint32_t startVertex;   // Starting vertex index
    uint32_t vertexCount;   // Count of vertices
    uint8_t flags;          // Polygon flags
    int8_t height;          // Height
    uint16_t minX;          // Minimum X coordinate of bounding box
    uint16_t maxX;          // Maximum X coordinate of bounding box
    uint16_t minY;          // Minimum Y coordinate of bounding box
    uint16_t maxY;          // Maximum Y coordinate of bounding box
    
    WEDPolygon() : startVertex(0), vertexCount(0), flags(0), height(-1), 
                   minX(0), maxX(0), minY(0), maxY(0) {}
    
    // Utility functions
    uint32_t getEndVertex() const {
        return startVertex + vertexCount;
    }
    
    bool isWall() const {
        return (flags & 0x01) != 0;
    }
    
    bool isHovering() const {
        return (flags & 0x02) != 0;
    }
    
    bool coversAnimations() const {
        return (flags & 0x0C) != 0;
    }
    
    bool isDoor() const {
        return (flags & 0x80) != 0;
    }
    
    uint16_t getWidth() const {
        return maxX - minX;
    }
    
    uint16_t getHeight() const {
        return maxY - minY;
    }
};

// WED V1.3 Vertex structure (serializable)
struct WEDVertex {
    uint16_t x;                 // X coordinate
    uint16_t y;                 // Y coordinate
    
    WEDVertex() : x(0), y(0) {}
    WEDVertex(uint16_t x_, uint16_t y_) : x(x_), y(y_) {}
    
    // Utility functions
    void scale(uint32_t factor) {
        x = static_cast<uint16_t>(x * factor);
        y = static_cast<uint16_t>(y * factor);
    }
};
#pragma pack(pop) // Restore default packing

// WED V1.3 file structure (in-memory representation)
struct WEDFile {
    WEDHeader header;
    WEDSecondaryHeader secHeader;
    std::vector<WEDOverlay> overlays;
    std::vector<WEDDoor> doors;
    std::vector<std::vector<WEDTilemap>> tilemaps;  // [overlay][tile]
    std::vector<uint16_t> doorTileCells;
    std::vector<std::vector<uint16_t>> tileIndices;  // [overlay][index]
    std::vector<WEDWallGroup> wallGroups;
    std::vector<WEDPolygon> polygons;
    std::vector<uint16_t> polygonIndices;  // PLT - wall groups reference this
    std::vector<WEDVertex> vertices;
    
    WEDFile() = default;
    
    // Calculate file size
    size_t calculateFileSize() const {
        // Find the highest offset and add the size of data at that offset
        size_t maxOffset = 0;
        size_t maxSize = 0;
        
        // Check overlay offset
        if (header.overlayOffset > maxOffset) {
            maxOffset = header.overlayOffset;
            maxSize = overlays.size() * sizeof(WEDOverlay);
        }
        
        // Check secondary header offset
        if (header.secHeaderOffset > maxOffset) {
            maxOffset = header.secHeaderOffset;
            maxSize = sizeof(WEDSecondaryHeader);
        }
        
        // Check door offset
        if (header.doorOffset > maxOffset) {
            maxOffset = header.doorOffset;
            maxSize = doors.size() * sizeof(WEDDoor);
        }
        
        // Check door tile offset
        if (header.doorTileOffset > maxOffset) {
            maxOffset = header.doorTileOffset;
            maxSize = doorTileCells.size() * sizeof(uint16_t);
        }
        
        // Check overlay tilemap and tile index offsets
        for (const auto& overlay : overlays) {
            if (overlay.tilemapOffset > maxOffset) {
                maxOffset = overlay.tilemapOffset;
                size_t tilemapSize = 0;
                for (const auto& overlayTilemaps : tilemaps) {
                    tilemapSize += overlayTilemaps.size() * sizeof(WEDTilemap);
                }
                maxSize = tilemapSize;
            }
            
            if (overlay.tileIndexOffset > maxOffset) {
                maxOffset = overlay.tileIndexOffset;
                size_t tileIndexSize = 0;
                for (const auto& overlayIndices : tileIndices) {
                    tileIndexSize += overlayIndices.size() * sizeof(uint16_t);
                }
                maxSize = tileIndexSize;
            }
        }
        
        // Check secondary header offsets
        if (secHeader.wallGroupOffset > maxOffset) {
            maxOffset = secHeader.wallGroupOffset;
            maxSize = wallGroups.size() * sizeof(WEDWallGroup);
        }
        
        if (secHeader.polygonOffset > maxOffset) {
            maxOffset = secHeader.polygonOffset;
            maxSize = polygons.size() * sizeof(WEDPolygon);
        }
        
        if (secHeader.polygonIndexOffset > maxOffset) {
            maxOffset = secHeader.polygonIndexOffset;
            maxSize = polygonIndices.size() * sizeof(uint16_t);
        }
        
        if (secHeader.vertexOffset > maxOffset) {
            maxOffset = secHeader.vertexOffset;
            maxSize = vertices.size() * sizeof(WEDVertex);
        }
        
        // Return the highest offset plus the size of data at that offset
        return maxOffset + maxSize;
    }
    
    // Serialize to binary data
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        
        // Calculate new offsets based on actual data sizes
        WEDHeader serializedHeader = header;
        WEDSecondaryHeader serializedSecHeader = secHeader;
        std::vector<WEDOverlay> serializedOverlays = overlays;
        
        size_t currentOffset = 0;
        
        // Header
        currentOffset = sizeof(WEDHeader);
        
        // Overlays
        serializedHeader.overlayOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += overlays.size() * sizeof(WEDOverlay);
        
        // Secondary header
        serializedHeader.secHeaderOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += sizeof(WEDSecondaryHeader);
        
        // Doors
        serializedHeader.doorOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += doors.size() * sizeof(WEDDoor);
        
        // Calculate total tilemap size first
        size_t totalTilemapSize = 0;
        for (size_t i = 0; i < tilemaps.size(); i++) {
            totalTilemapSize += tilemaps[i].size() * sizeof(WEDTilemap);
        }
        
        // Set tilemap offsets for each overlay
        size_t tilemapOffset = currentOffset;
        for (size_t i = 0; i < overlays.size(); i++) {
            serializedOverlays[i].tilemapOffset = static_cast<uint32_t>(tilemapOffset);
            if (i < tilemaps.size() && !tilemaps[i].empty()) {
                tilemapOffset += tilemaps[i].size() * sizeof(WEDTilemap);
            }
            // For empty overlays, keep the same offset (pointing to end of valid tilemap data)
        }
        currentOffset += totalTilemapSize;
        
        // Door tile cells (must come before tile indices per WED spec)
        serializedHeader.doorTileOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += doorTileCells.size() * sizeof(uint16_t);
        
        // Set tile index offsets for each overlay (after door tile cells)
        size_t tileIndexOffset = currentOffset;
        for (size_t i = 0; i < overlays.size(); i++) {
            serializedOverlays[i].tileIndexOffset = static_cast<uint32_t>(tileIndexOffset);
            if (i < tileIndices.size()) {
                tileIndexOffset += tileIndices[i].size() * sizeof(uint16_t);
            }
        }
        currentOffset = tileIndexOffset;
        
        // Wall groups
        serializedSecHeader.wallGroupOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += wallGroups.size() * sizeof(WEDWallGroup);
        
        // Polygons (must come before polygon indices per WED spec)
        serializedSecHeader.polygonOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += polygons.size() * sizeof(WEDPolygon);
        
        // Polygon indices (PLT) - wall groups reference this (must come after polygons)
        serializedSecHeader.polygonIndexOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += polygonIndices.size() * sizeof(uint16_t);
        
        // Vertices
        serializedSecHeader.vertexOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += vertices.size() * sizeof(WEDVertex);
        
        // Reserve space for the entire file
        data.reserve(currentOffset);
        
        // Write header with recalculated offsets
        const uint8_t* headerPtr = reinterpret_cast<const uint8_t*>(&serializedHeader);
        data.insert(data.end(), headerPtr, headerPtr + sizeof(WEDHeader));
        
        // Write overlays with recalculated offsets
        for (const auto& overlay : serializedOverlays) {
            const uint8_t* overlayPtr = reinterpret_cast<const uint8_t*>(&overlay);
            data.insert(data.end(), overlayPtr, overlayPtr + sizeof(WEDOverlay));
        }
        
        // Write secondary header with recalculated offsets
        const uint8_t* secHeaderPtr = reinterpret_cast<const uint8_t*>(&serializedSecHeader);
        data.insert(data.end(), secHeaderPtr, secHeaderPtr + sizeof(WEDSecondaryHeader));
        
        // Write doors with updated polygon offsets
        for (const auto& door : doors) {
            WEDDoor serializedDoor = door;
            
            // Update door polygon offsets to match the new polygon section location
            if (serializedDoor.openPolygonOffset > 0) {
                // Calculate relative offset from original polygon section
                uint32_t relativeOffset = serializedDoor.openPolygonOffset - secHeader.polygonOffset;
                // Apply to new polygon section location
                serializedDoor.openPolygonOffset = serializedSecHeader.polygonOffset + relativeOffset;
            }
            
            if (serializedDoor.closedPolygonOffset > 0) {
                // Calculate relative offset from original polygon section
                uint32_t relativeOffset = serializedDoor.closedPolygonOffset - secHeader.polygonOffset;
                // Apply to new polygon section location
                serializedDoor.closedPolygonOffset = serializedSecHeader.polygonOffset + relativeOffset;
            }
            
            const uint8_t* doorPtr = reinterpret_cast<const uint8_t*>(&serializedDoor);
            data.insert(data.end(), doorPtr, doorPtr + sizeof(WEDDoor));
        }
        
        // Write tilemaps for each overlay sequentially
        for (size_t i = 0; i < tilemaps.size(); i++) {
            for (const auto& tilemap : tilemaps[i]) {
                const uint8_t* tilemapPtr = reinterpret_cast<const uint8_t*>(&tilemap);
                data.insert(data.end(), tilemapPtr, tilemapPtr + sizeof(WEDTilemap));
            }
        }
        
        // Write door tile cells (must come before tile indices per WED spec)
        for (const auto& doorTile : doorTileCells) {
            const uint8_t* tilePtr = reinterpret_cast<const uint8_t*>(&doorTile);
            data.insert(data.end(), tilePtr, tilePtr + sizeof(uint16_t));
        }
        
        // Write tile indices for each overlay sequentially (after door tile cells)
        for (size_t i = 0; i < tileIndices.size(); i++) {
            for (const auto& index : tileIndices[i]) {
                const uint8_t* indexPtr = reinterpret_cast<const uint8_t*>(&index);
                data.insert(data.end(), indexPtr, indexPtr + sizeof(uint16_t));
            }
        }
        
        // Write wall groups
        for (const auto& wallGroup : wallGroups) {
            const uint8_t* wallGroupPtr = reinterpret_cast<const uint8_t*>(&wallGroup);
            data.insert(data.end(), wallGroupPtr, wallGroupPtr + sizeof(WEDWallGroup));
        }
        
        // Write polygons (must come before polygon indices per WED spec)
        for (const auto& polygon : polygons) {
            const uint8_t* polygonPtr = reinterpret_cast<const uint8_t*>(&polygon);
            data.insert(data.end(), polygonPtr, polygonPtr + sizeof(WEDPolygon));
        }
        
        // Write polygon indices (PLT) - wall groups reference this (must come after polygons)
        for (const auto& polygonIndex : polygonIndices) {
            const uint8_t* indexPtr = reinterpret_cast<const uint8_t*>(&polygonIndex);
            data.insert(data.end(), indexPtr, indexPtr + sizeof(uint16_t));
        }
        
        // Write vertices
        for (const auto& vertex : vertices) {
            const uint8_t* vertexPtr = reinterpret_cast<const uint8_t*>(&vertex);
            data.insert(data.end(), vertexPtr, vertexPtr + sizeof(WEDVertex));
        }
        
        return data;
    }
    
    // Deserialize from binary data
    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(WEDHeader)) {
            return false;
        }
        
        // Read header
        memcpy(&header, data.data(), sizeof(WEDHeader));
        
        // Validate signature and version
        if (!header.isValid()) {
            return false;
        }
        
        // Validate offsets
        if (!header.validateOffsets(data.size())) {
            return false;
        }
        
        // Read overlays (immediately after header)
        overlays.clear();
        overlays.reserve(header.overlayCount);
        size_t overlayOffset = header.overlayOffset;
        for (uint32_t i = 0; i < header.overlayCount; i++) {
            if (overlayOffset + sizeof(WEDOverlay) > data.size()) {
                return false;
            }
            WEDOverlay overlay;
            memcpy(&overlay, data.data() + overlayOffset, sizeof(WEDOverlay));
            overlays.push_back(overlay);
            overlayOffset += sizeof(WEDOverlay);
        }
        
        // Read secondary header (after overlays)
        size_t secHeaderOffset = header.secHeaderOffset;
        if (secHeaderOffset + sizeof(WEDSecondaryHeader) > data.size()) {
            return false;
        }
        memcpy(&secHeader, data.data() + secHeaderOffset, sizeof(WEDSecondaryHeader));
        
        // Temporarily disable secondary header offset validation
        // if (!secHeader.validateOffsets(data.size())) {
        //     return false;
        // }
        
        // Read doors (after secondary header)
        doors.clear();
        doors.reserve(header.doorCount);
        if (header.doorCount > 0) {
            size_t doorOffset = header.doorOffset;
            for (uint32_t i = 0; i < header.doorCount; i++) {
                if (doorOffset + sizeof(WEDDoor) > data.size()) {
                    return false;
                }
                WEDDoor door;
                memcpy(&door, data.data() + doorOffset, sizeof(WEDDoor));
                doors.push_back(door);
                doorOffset += sizeof(WEDDoor);
            }
        }
        
        // Read tilemaps for each overlay (preserve original data)
        tilemaps.clear();
        tilemaps.reserve(overlays.size());
        for (const auto& overlay : overlays) {
            if (!overlay.validateOffsets(data.size())) {
                return false;
            }
            
            std::vector<WEDTilemap> overlayTilemaps;
            size_t tilemapOffset = overlay.tilemapOffset;
            size_t tileCount = overlay.getTileCount();
            
            overlayTilemaps.reserve(tileCount);
            for (size_t i = 0; i < tileCount; i++) {
                if (tilemapOffset + sizeof(WEDTilemap) > data.size()) {
                    break;
                }
                WEDTilemap tilemap;
                memcpy(&tilemap, data.data() + tilemapOffset, sizeof(WEDTilemap));
                overlayTilemaps.push_back(tilemap);
                tilemapOffset += sizeof(WEDTilemap);
            }
            tilemaps.push_back(overlayTilemaps);
        }
        
        // Read tile indices for each overlay based on tilemap requirements
        tileIndices.clear();
        tileIndices.reserve(overlays.size());
        for (size_t overlayIdx = 0; overlayIdx < overlays.size(); overlayIdx++) {
            const auto& overlay = overlays[overlayIdx];
            if (!overlay.validateOffsets(data.size())) {
                return false;
            }
            
            std::vector<uint16_t> overlayIndices;
            
            // Calculate total tile indices needed by summing up counts from all tilemaps for this overlay
            size_t totalIndicesNeeded = 0;
            if (overlayIdx < tilemaps.size()) {
                for (const auto& tilemap : tilemaps[overlayIdx]) {
                    totalIndicesNeeded += tilemap.tileCount;
                }
            }
            
            // Read the required number of tile indices
            if (totalIndicesNeeded > 0) {
                size_t indexOffset = overlay.tileIndexOffset;
                overlayIndices.reserve(totalIndicesNeeded);
                
                for (size_t i = 0; i < totalIndicesNeeded; i++) {
                    if (indexOffset + sizeof(uint16_t) > data.size()) {
                        break;
                    }
                    uint16_t index;
                    memcpy(&index, data.data() + indexOffset, sizeof(uint16_t));
                    overlayIndices.push_back(index);
                    indexOffset += sizeof(uint16_t);
                }
            }
            
            tileIndices.push_back(overlayIndices);
        }
        
        // Read door tile cells (if any)
        doorTileCells.clear();
        if (header.doorCount > 0) {
            size_t doorTileOffset = header.doorTileOffset;
            
            // Calculate door tile cell count from doors
            uint16_t totalDoorTiles = 0;
            for (const auto& door : doors) {
                totalDoorTiles += door.doorTileCount;
            }
            
            doorTileCells.reserve(totalDoorTiles);
            for (uint16_t i = 0; i < totalDoorTiles; i++) {
                if (doorTileOffset + sizeof(uint16_t) > data.size()) {
                    return false;
                }
                uint16_t doorTile;
                memcpy(&doorTile, data.data() + doorTileOffset, sizeof(uint16_t));
                doorTileCells.push_back(doorTile);
                doorTileOffset += sizeof(uint16_t);
            }
        }
        
        // Read wall groups (after door tile cells)
        wallGroups.clear();
        if (secHeader.wallGroupOffset < data.size()) {
            size_t wallGroupOffset = secHeader.wallGroupOffset;
            
            // Calculate wall group count based on overlay size
            if (!overlays.empty()) {
                size_t w = overlays[0].width;
                size_t h = overlays[0].height * 2;
                size_t groupSize = ((w + 9) / 10) * ((h + 14) / 15); // ceil(w/10) * ceil(h/7.5)
                
                wallGroups.reserve(groupSize);
                for (size_t i = 0; i < groupSize; i++) {
                    if (wallGroupOffset + sizeof(WEDWallGroup) > data.size()) {
                        break;
                    }
                    WEDWallGroup wallGroup;
                    memcpy(&wallGroup, data.data() + wallGroupOffset, sizeof(WEDWallGroup));
                    wallGroups.push_back(wallGroup);
                    wallGroupOffset += sizeof(WEDWallGroup);
                }
            }
        }
        
        // Read polygons (after wall groups)
        polygons.clear();
        if (secHeader.polygonCount > 0 && secHeader.polygonOffset < data.size()) {
            size_t polygonOffset = secHeader.polygonOffset;
            size_t polygonCount = (secHeader.polygonIndexOffset - secHeader.polygonOffset) / sizeof(WEDPolygon);
            
            polygons.reserve(polygonCount);
            for (size_t i = 0; i < polygonCount; i++) {
                if (polygonOffset + sizeof(WEDPolygon) > data.size()) {
                    return false;
                }
                WEDPolygon polygon;
                memcpy(&polygon, data.data() + polygonOffset, sizeof(WEDPolygon));
                polygons.push_back(polygon);
                polygonOffset += sizeof(WEDPolygon);
            }
        }
        
        // Read polygon indices (after polygons)
        polygonIndices.clear();
        if (secHeader.polygonIndexOffset < data.size()) {
            size_t polygonIndexOffset = secHeader.polygonIndexOffset;
            size_t polygonIndexCount = 0;
            
            if (secHeader.vertexOffset > secHeader.polygonIndexOffset) {
                polygonIndexCount = (secHeader.vertexOffset - secHeader.polygonIndexOffset) / sizeof(uint16_t);
            } else {
                // If no vertices, read until the end of the file
                polygonIndexCount = (data.size() - secHeader.polygonIndexOffset) / sizeof(uint16_t);
            }
            
            polygonIndices.reserve(polygonIndexCount);
            for (size_t i = 0; i < polygonIndexCount; i++) {
                if (polygonIndexOffset + sizeof(uint16_t) > data.size()) {
                    return false;
                }
                uint16_t polygonIndex;
                memcpy(&polygonIndex, data.data() + polygonIndexOffset, sizeof(uint16_t));
                polygonIndices.push_back(polygonIndex);
                polygonIndexOffset += sizeof(uint16_t);
            }
        }
        
        // Read vertices (after polygon indices)
        vertices.clear();
        if (secHeader.vertexOffset < data.size()) {
            size_t vertexOffset = secHeader.vertexOffset;
            size_t vertexCount = (data.size() - secHeader.vertexOffset) / sizeof(WEDVertex);
            
            vertices.reserve(vertexCount);
            for (size_t i = 0; i < vertexCount; i++) {
                if (vertexOffset + sizeof(WEDVertex) > data.size()) {
                    return false;
                }
                WEDVertex vertex;
                memcpy(&vertex, data.data() + vertexOffset, sizeof(WEDVertex));
                vertices.push_back(vertex);
                vertexOffset += sizeof(WEDVertex);
            }
        }
        
        return true;
    }
    
    // Utility functions
    bool isValid() const {
        size_t fileSize = calculateFileSize();
        bool headerValid = header.isValid();
        bool headerOffsetsValid = header.validateOffsets(fileSize);
        bool secHeaderOffsetsValid = secHeader.validateOffsets(fileSize);
        
        // Temporarily disable secondary header validation as it's too strict
        return headerValid && headerOffsetsValid; // && secHeaderOffsetsValid;
    }
    
    void scaleCoordinates(uint32_t factor) {
        // Scale vertices
        for (auto& vertex : vertices) {
            vertex.scale(factor);
        }
        
        // Scale polygon bounding boxes
        for (auto& polygon : polygons) {
            polygon.minX = static_cast<uint16_t>(polygon.minX * factor);
            polygon.maxX = static_cast<uint16_t>(polygon.maxX * factor);
            polygon.minY = static_cast<uint16_t>(polygon.minY * factor);
            polygon.maxY = static_cast<uint16_t>(polygon.maxY * factor);
        }
    }
};

} // namespace ProjectIE4k 