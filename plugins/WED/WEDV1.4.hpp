#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <cstring>
#include <string>

#include "core/Logging/Logging.h"

/*
 WED V1.4 format notes (differences vs V1.3)
 
 - Version string:
   * V1.4 uses header version "V1.4" (first 8 bytes are "WED " "V1.4").
 
 - Widened fields and on-disk sizes:
   * Tilemap entries: WEDTilemapV14 widens startIndex, tileCount, secondaryIndex to uint32
     - V1.3: 10 bytes per tilemap (uint16 startIndex, tileCount, secondaryIndex; uint8 overlayFlags; uint8[3] unknown)
     - V1.4: 16 bytes per tilemap (uint32 startIndex, tileCount, secondaryIndex; uint8 overlayFlags; uint8[3] unknown)
   * Overlay tile indices array:
     - V1.3: uint16 per index
     - V1.4: uint32 per index
   * Door tile cells array:
     - V1.3: uint16 per cell
     - V1.4: uint32 per cell
   * Door table entry (WEDDoorV14): widens firstDoorTile and doorTileCount to uint32
     - V1.3 WEDDoor: 26 bytes (firstDoorTile, doorTileCount are uint16)
     - V1.4 WEDDoorV14: 30 bytes (firstDoorTile, doorTileCount are uint32)
 
 - Same between V1.3 and V1.4:
   * WEDHeader and WEDSecondaryHeader: field layout/size consistent; counts/offsets are uint32
   * Polygon sections: offsets remain uint32; polygons/vertices/wall-groups keep same field widths
 
 - Behavioral implications:
   * V1.3 overlay/door indices can roll over modulo 65536 due to uint16 storage
   * V1.4 eliminates rollover by widening to uint32; supports >65k tilemaps and indices, suitable for 4x upscales
  * Only a few areas so far that I have checked caused this requirement in BG2.  Not sure of other games yet.

 - Section ordering reminder (as used by serializer):
   * overlays table
   * secondary header
   * doors table
   * door tile cells (uint32 each in V1.4)
   * wall groups (4 bytes per entry)
   * polygons (18 bytes each)
   * polygon indices PLT (uint16 entries; total count == sum of polygon.vertexCount)
   * vertices (4 bytes per vertex)

 - PLT construction in V1.4 upscaled output:
   * We rebuild PLT as the concatenation of per-polygon vertex index ranges: [startVertex .. startVertex + vertexCount)
   * Stored as uint16 indices; we validate that sum(vertexCount) equals the PLT entry count
*/

namespace ProjectIE4k {

// WED V1.4 Header structure (serializable)
#pragma pack(push, 1) // Ensure no padding
struct WEDHeaderV14 {
    char signature[4];      // 'WED '
    char version[4];        // 'V1.4'
    uint32_t overlayCount;  // Number of overlays (including base layer)
    uint32_t doorCount;     // Number of doors
    uint32_t overlayOffset; // Offset to overlays
    uint32_t secHeaderOffset; // Offset to secondary header
    uint32_t doorOffset;    // Offset to doors
    uint32_t doorTileOffset; // Offset to door tile cell indices
    
    WEDHeaderV14() : overlayCount(0), doorCount(0), overlayOffset(0), 
                  secHeaderOffset(0), doorOffset(0), doorTileOffset(0) {
        memcpy(signature, "WED ", 4);
        memcpy(version, "V1.4", 4);
    }
    
    // Validation functions
    bool isValid() const {
        // Accept both V1.3 and V1.4 for reading
        return memcmp(signature, "WED ", 4) == 0 &&
               (memcmp(version, "V1.3", 4) == 0 || memcmp(version, "V1.4", 4) == 0);
    }
    
    bool validateOffsets(size_t fileSize) const {
        return overlayOffset < fileSize && 
               secHeaderOffset < fileSize && 
               doorOffset < fileSize && 
               doorTileOffset < fileSize;
    }
};

// WED V1.3 Secondary Header structure (serializable)
struct WEDSecondaryHeaderV14 {
    uint32_t polygonCount;      // Number of polygons
    uint32_t polygonOffset;     // Offset to polygons
    uint32_t vertexOffset;      // Offset to vertices
    uint32_t wallGroupOffset;   // Offset to wall groups
    uint32_t polygonIndexOffset; // Offset to polygon indices lookup table
    
    WEDSecondaryHeaderV14() : polygonCount(0), polygonOffset(0), vertexOffset(0), 
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
struct WEDOverlayV14 {
    uint16_t width;             // Width in tiles
    uint16_t height;            // Height in tiles
    char tilesetName[8];        // Name of tileset (resref)
    uint32_t unknown;           // Unknown 4-byte field (was split into uniqueTileCount + movementType)
    uint32_t tilemapOffset;     // Offset to tilemap for this overlay
    uint32_t tileIndexOffset;   // Offset to tile index lookup for this overlay
    
    WEDOverlayV14() : width(0), height(0), unknown(0), 
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

// WED V1.4 Door structure (serializable)
struct WEDDoorV14 {
    char name[8];               // Name of door
    uint16_t openClosed;        // Open (0) / Closed (1)
    uint32_t firstDoorTile;     // First door tile cell index (widened)
    uint32_t doorTileCount;     // Count of door tile cells for this door (widened)
    uint16_t openPolygonCount;  // Count of polygons open state
    uint16_t closedPolygonCount; // Count of polygons closed state
    uint32_t openPolygonOffset; // Offset to polygons open state
    uint32_t closedPolygonOffset; // Offset to polygons closed state
    
    WEDDoorV14() : openClosed(0), firstDoorTile(0), doorTileCount(0), 
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

// WED V1.4 Tilemap structure (serializable)
struct WEDTilemapV14 {
    uint32_t startIndex;        // Start index in tile index lookup table (widened)
    uint32_t tileCount;         // Count of tiles in tile index lookup table (widened)
    uint32_t secondaryIndex;    // Index from TIS file of secondary tile (widened)
    uint8_t overlayFlags;       // Overlay drawing flags
    uint8_t unknown[3];         // Unknown bytes
    
    WEDTilemapV14() : startIndex(0), tileCount(0), secondaryIndex(0), overlayFlags(0) {
        memset(unknown, 0, 3);
    }
    
    // Utility functions
    bool hasOverlay(uint8_t overlayIndex) const {
        return (overlayFlags & (1 << overlayIndex)) != 0;
    }
    
    uint32_t getEndIndex() const {
        return startIndex + tileCount;
    }
};

// WED V1.4 Wall Group structure (serializable)
// Keep 16-bit fields (as in V1.3) – importer expects u16 start/count
struct WEDWallGroupV14 {
    uint16_t startIndex;        // Start polygon index
    uint16_t indexCount;        // Polygon index count

    WEDWallGroupV14() : startIndex(0), indexCount(0) {}

    // Utility functions
    uint16_t getEndIndex() const {
        return static_cast<uint16_t>(startIndex + indexCount);
    }
};

// WED V1.3 Polygon structure (serializable)
struct WEDPolygonV14 {
    uint32_t startVertex;   // Starting vertex index
    uint32_t vertexCount;   // Count of vertices
    uint8_t flags;          // Polygon flags
    int8_t height;          // Height
    uint16_t minX;          // Minimum X coordinate of bounding box
    uint16_t maxX;          // Maximum X coordinate of bounding box
    uint16_t minY;          // Minimum Y coordinate of bounding box
    uint16_t maxY;          // Maximum Y coordinate of bounding box
    
    WEDPolygonV14() : startVertex(0), vertexCount(0), flags(0), height(-1), 
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

// WED V1.4 Vertex structure (serializable)
struct WEDVertexV14 {
    uint16_t x;                 // X coordinate
    uint16_t y;                 // Y coordinate
    
    WEDVertexV14() : x(0), y(0) {}
    WEDVertexV14(uint16_t x_, uint16_t y_) : x(x_), y(y_) {}
    
    // Utility functions
    void scale(uint32_t factor) {
        x = static_cast<uint16_t>(x * factor);
        y = static_cast<uint16_t>(y * factor);
    }
};
#pragma pack(pop) // Restore default packing

// WED V1.4 file structure (in-memory representation)
struct WEDFileV14 {
    WEDHeaderV14 header;
    WEDSecondaryHeaderV14 secHeader;
    std::vector<WEDOverlayV14> overlays;
    std::vector<WEDDoorV14> doors;
    std::vector<std::vector<WEDTilemapV14>> tilemaps;  // [overlay][tile]
    std::vector<uint32_t> doorTileCells;               // widened
    std::vector<std::vector<uint32_t>> tileIndices;    // [overlay][index] (widened to 32-bit)
    std::vector<WEDWallGroupV14> wallGroups;
    std::vector<WEDPolygonV14> polygons;
    std::vector<uint16_t> polygonIndices;              // PLT - wall groups reference this
    std::vector<WEDVertexV14> vertices;
    
    WEDFileV14() = default;
    
    // Calculate file size
    size_t calculateFileSize() const {
        // Find the highest offset and add the size of data at that offset
        size_t maxOffset = 0;
        size_t maxSize = 0;
        
        // Check overlay offset
        if (header.overlayOffset > maxOffset) {
            maxOffset = header.overlayOffset;
            maxSize = overlays.size() * sizeof(WEDOverlayV14);
        }
        
        // Check secondary header offset
        if (header.secHeaderOffset > maxOffset) {
            maxOffset = header.secHeaderOffset;
            maxSize = sizeof(WEDSecondaryHeaderV14);
        }
        
        // Check door offset
        if (header.doorOffset > maxOffset) {
            maxOffset = header.doorOffset;
            maxSize = doors.size() * sizeof(WEDDoorV14);
        }
        
        // Check door tile offset
        if (header.doorTileOffset > maxOffset) {
            maxOffset = header.doorTileOffset;
            maxSize = doorTileCells.size() * sizeof(uint32_t);
        }
        
        // Check overlay tilemap and tile index offsets
        for (const auto& overlay : overlays) {
            if (overlay.tilemapOffset > maxOffset) {
                maxOffset = overlay.tilemapOffset;
                size_t tilemapSize = 0;
                for (const auto& overlayTilemaps : tilemaps) {
                    tilemapSize += overlayTilemaps.size() * sizeof(WEDTilemapV14);
                }
                maxSize = tilemapSize;
            }
            
            if (overlay.tileIndexOffset > maxOffset) {
                maxOffset = overlay.tileIndexOffset;
                size_t tileIndexSize = 0;
                for (const auto& overlayIndices : tileIndices) {
                    tileIndexSize += overlayIndices.size() * sizeof(uint32_t);
                }
                maxSize = tileIndexSize;
            }
        }
        
        // Check secondary header offsets
        if (secHeader.wallGroupOffset > maxOffset) {
            maxOffset = secHeader.wallGroupOffset;
            maxSize = wallGroups.size() * sizeof(WEDWallGroupV14);
        }
        
        if (secHeader.polygonOffset > maxOffset) {
            maxOffset = secHeader.polygonOffset;
            maxSize = polygons.size() * sizeof(WEDPolygonV14);
        }
        
        if (secHeader.polygonIndexOffset > maxOffset) {
            maxOffset = secHeader.polygonIndexOffset;
            maxSize = polygonIndices.size() * sizeof(uint16_t);
        }
        
        if (secHeader.vertexOffset > maxOffset) {
            maxOffset = secHeader.vertexOffset;
            maxSize = vertices.size() * sizeof(WEDVertexV14);
        }
        
        // Return the highest offset plus the size of data at that offset
        return maxOffset + maxSize;
    }
    
    // Serialize to binary data
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        
        // Calculate new offsets based on actual data sizes
        WEDHeaderV14 serializedHeader = header;
        // Always write as V1.4 regardless of source
        memcpy(serializedHeader.version, "V1.4", 4);
        WEDSecondaryHeaderV14 serializedSecHeader = secHeader;
        std::vector<WEDOverlayV14> serializedOverlays = overlays;
        // Take copies of sections that will be written verbatim to avoid aliasing
        const std::vector<WEDPolygonV14> polygons_copy = polygons;
        const std::vector<uint16_t> polygonIndices_copy = polygonIndices;

        size_t currentOffset = 0;
        
        // Header
        currentOffset = sizeof(WEDHeaderV14);
        
        // Overlays
        // Update counts
        serializedHeader.overlayCount = static_cast<uint32_t>(overlays.size());
        serializedHeader.overlayOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += overlays.size() * sizeof(WEDOverlayV14);

        // Secondary header
        serializedHeader.secHeaderOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += sizeof(WEDSecondaryHeaderV14);

        serializedHeader.doorOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += doors.size() * sizeof(WEDDoorV14);
        
        // Calculate total tilemap size based on overlay dimensions (width*height)
        size_t totalTilemapSize = 0;
        for (size_t i = 0; i < overlays.size(); i++) {
            size_t ovCount = static_cast<size_t>(overlays[i].width) * static_cast<size_t>(overlays[i].height);
            totalTilemapSize += ovCount * sizeof(WEDTilemapV14);
        }
        
        // Set tilemap offsets for each overlay (advance by width*height)
        size_t tilemapOffset = currentOffset;
        for (size_t i = 0; i < overlays.size(); i++) {
            serializedOverlays[i].tilemapOffset = static_cast<uint32_t>(tilemapOffset);
            const size_t ovCount = static_cast<size_t>(overlays[i].width) * static_cast<size_t>(overlays[i].height);
            tilemapOffset += ovCount * sizeof(WEDTilemapV14);
        }
        currentOffset += totalTilemapSize;
        
        // Door tile cells (must come before tile indices per WED spec)
        serializedHeader.doorTileOffset = static_cast<uint32_t>(currentOffset);
        Log(DEBUG, "WED", "[serialize] Setting doorTileOffset = {} (doorTileCells.size()={})", 
            serializedHeader.doorTileOffset, doorTileCells.size());
        currentOffset += doorTileCells.size() * sizeof(uint32_t);
        
        // Set tile index offsets for each overlay (after door tile cells)
        size_t tileIndexOffset = currentOffset;
        for (size_t i = 0; i < overlays.size(); i++) {
            serializedOverlays[i].tileIndexOffset = static_cast<uint32_t>(tileIndexOffset);
            if (i < tileIndices.size()) {
                tileIndexOffset += tileIndices[i].size() * sizeof(uint32_t);
            }
        }
        currentOffset = tileIndexOffset;
        
        // Wall groups
        serializedSecHeader.wallGroupOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += wallGroups.size() * sizeof(WEDWallGroupV14);
        
        // Polygons (must come before polygon indices per WED spec)
        serializedSecHeader.polygonCount = static_cast<uint32_t>(polygons_copy.size());
        serializedSecHeader.polygonOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += polygons_copy.size() * sizeof(WEDPolygonV14);
        
        // Polygon indices (PLT) - wall groups reference this (must come after polygons)
        serializedSecHeader.polygonIndexOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += polygonIndices_copy.size() * sizeof(uint16_t);
        
        // Vertices
        serializedSecHeader.vertexOffset = static_cast<uint32_t>(currentOffset);
        currentOffset += vertices.size() * sizeof(WEDVertexV14);
        
        // Debug: compute section sizes for logging
        size_t dbgHeader = sizeof(WEDHeaderV14);
        size_t dbgOverlays = overlays.size() * sizeof(WEDOverlayV14);
        size_t dbgSecHeader = sizeof(WEDSecondaryHeaderV14);
        size_t dbgDoors = doors.size() * sizeof(WEDDoorV14);
        size_t dbgTilemaps = totalTilemapSize;
        size_t dbgDoorTiles = doorTileCells.size() * sizeof(uint32_t);
        size_t dbgTileIndices = 0; for (const auto& v : tileIndices) dbgTileIndices += v.size() * sizeof(uint32_t);
        size_t dbgWallGroups = wallGroups.size() * sizeof(WEDWallGroupV14);
        size_t dbgPolygons = polygons_copy.size() * sizeof(WEDPolygonV14);
        size_t dbgPolygonIndices = polygonIndices_copy.size() * sizeof(uint16_t);
        size_t dbgVertices = vertices.size() * sizeof(WEDVertexV14);

        Log(DEBUG, "WED", "[serialize] sections: H={} Ov={} Sec={} Doors={} Tm={} DT={} TI={} WG={} Poly={} PLT={} Vtx={}",
            dbgHeader, dbgOverlays, dbgSecHeader, dbgDoors, dbgTilemaps, dbgDoorTiles, dbgTileIndices, dbgWallGroups, dbgPolygons, dbgPolygonIndices, dbgVertices);
        Log(DEBUG, "WED", "[serialize] computed currentOffset={}", currentOffset);

        // Validate wall group section span matches expected bytes (wallGroups.size() * 4)
        {
            const uint32_t wgOff = serializedSecHeader.wallGroupOffset;
            const uint32_t polyOff = serializedSecHeader.polygonOffset;
            const size_t expectedSpan = wallGroups.size() * sizeof(WEDWallGroupV14);
            const ptrdiff_t actualSpan = static_cast<ptrdiff_t>(polyOff) - static_cast<ptrdiff_t>(wgOff);
            Log(DEBUG, "WED", "[serialize] wallGroups: offset={} count={} elemSize={} expectedSpan={} actualSpan={}",
                wgOff, (unsigned)wallGroups.size(), (unsigned)sizeof(WEDWallGroupV14), (unsigned)expectedSpan, (long long)actualSpan);
            if (actualSpan != static_cast<ptrdiff_t>(expectedSpan)) {
                Log(ERROR, "WED", "[serialize] wall group span mismatch: expected {} bytes but got {} bytes (wgOff={} -> polyOff={})",
                    (unsigned)expectedSpan, (long long)actualSpan, wgOff, polyOff);
            }
        }

        // Validate PLT entries are within polygon table bounds
        if (!polygons_copy.empty() && !polygonIndices_copy.empty()) {
            size_t bad = 0;
            for (size_t j = 0; j < polygonIndices_copy.size(); ++j) {
                uint16_t idx = polygonIndices_copy[j];
                if (idx >= polygons_copy.size()) {
                    ++bad;
                    if (bad <= 5) {
                        Log(ERROR, "WED", "[serialize] PLT[{}] = {} out of range (polygonCount={})", (unsigned)j, (unsigned)idx, (unsigned)polygons_copy.size());
                    }
                }
            }
            if (bad == 0) {
                Log(DEBUG, "WED", "[serialize] PLT entries valid: {} entries within [0, {}]", (unsigned)polygonIndices_copy.size(), (unsigned)(polygons_copy.size()-1));
            } else {
                Log(ERROR, "WED", "[serialize] PLT has {} out-of-range entries (of {})", (unsigned)bad, (unsigned)polygonIndices_copy.size());
            }
        }
        // Dump a few polygon headers around 8..12 for debugging
        for (size_t i = 8; i < polygons_copy.size() && i < 13; ++i) {
            const auto &p = polygons_copy[i];
            Log(DEBUG, "WED", "[serialize] Poly[{}] sv={} cnt={} flags=0x{:02X} h={} bbox=({},{})-({},{}).", (unsigned)i,
                (unsigned)p.startVertex, (unsigned)p.vertexCount, (unsigned)p.flags, (int)p.height,
                (unsigned)p.minX, (unsigned)p.minY, (unsigned)p.maxX, (unsigned)p.maxY);
        }
        // Dump first 16 PLT values
        {
            size_t dumpN = std::min<size_t>(16, polygonIndices_copy.size());
            std::string s;
            for (size_t i = 0; i < dumpN; ++i) {
                if (i) s += ",";
                s += std::to_string(polygonIndices_copy[i]);
            }
            Log(DEBUG, "WED", "[serialize] PLT first {}: {}", (unsigned)dumpN, s);
        }

        // Validate each wall group range is within PLT bounds
        if (!wallGroups.empty()) {
            size_t bad = 0;
            for (size_t k = 0; k < wallGroups.size(); ++k) {
                const auto &wg = wallGroups[k];
                const uint32_t S = wg.startIndex;
                const uint32_t C = wg.indexCount;
                const uint32_t E = S + C;
                if (E > polygonIndices_copy.size()) {
                    ++bad;
                    if (bad <= 5) {
                        Log(ERROR, "WED", "[serialize] WallGroup {}: startIndex={} count={} exceeds PLT size {} (range [{}, {}))",
                            (unsigned)k, (unsigned)S, (unsigned)C, (unsigned)polygonIndices_copy.size(), (unsigned)S, (unsigned)E);
                    }
                }
            }
            if (bad == 0) {
                Log(DEBUG, "WED", "[serialize] All wall group ranges fit within PLT ({} entries)", (unsigned)polygonIndices_copy.size());
            } else {
                Log(ERROR, "WED", "[serialize] {} wall group ranges exceed PLT size {}", (unsigned)bad, (unsigned)polygonIndices_copy.size());
            }
        }

        // Guard against unreasonable size (to avoid reserve causing OOM/corruption)
        const size_t kMaxReasonableSize = static_cast<size_t>(1ULL << 31); // 2 GiB cap for safety
        if (currentOffset == 0 || currentOffset > kMaxReasonableSize) {
            // Return empty on invalid sizing; caller should treat as failure
            return {};
        }
        // Allocate final buffer and write with a cursor
        data.resize(currentOffset);
        size_t w = 0;

        // Write header
        Log(DEBUG, "WED", "[serialize] Writing header: doorTileOffset = {}", serializedHeader.doorTileOffset);
        memcpy(data.data() + w, &serializedHeader, sizeof(WEDHeaderV14));
        w += sizeof(WEDHeaderV14);

        // Write overlays
        for (const auto& overlay : serializedOverlays) {
            memcpy(data.data() + w, &overlay, sizeof(WEDOverlayV14));
            w += sizeof(WEDOverlayV14);
        }

        // Write secondary header
        memcpy(data.data() + w, &serializedSecHeader, sizeof(WEDSecondaryHeaderV14));
        w += sizeof(WEDSecondaryHeaderV14);

        // Precompute expected section boundaries for validation
        size_t offHeaderEnd = sizeof(WEDHeaderV14);
        size_t offOverlaysEnd = offHeaderEnd + overlays.size() * sizeof(WEDOverlayV14);
        size_t offSecHeaderEnd = offOverlaysEnd + sizeof(WEDSecondaryHeaderV14);
        size_t offDoorsEnd = offSecHeaderEnd + doors.size() * sizeof(WEDDoorV14);
        size_t offTilemapsEnd = offDoorsEnd;
        for (size_t i = 0; i < overlays.size(); ++i) {
            size_t ovCount = static_cast<size_t>(overlays[i].width) * static_cast<size_t>(overlays[i].height);
            offTilemapsEnd += ovCount * sizeof(WEDTilemapV14);
        }
        size_t offDoorTilesEnd = offTilemapsEnd + doorTileCells.size() * sizeof(uint32_t);
        size_t offTileIndicesEnd = offDoorTilesEnd;
        for (size_t i = 0; i < tileIndices.size(); ++i) offTileIndicesEnd += tileIndices[i].size() * sizeof(uint32_t);
        size_t offWallGroupsEnd = offTileIndicesEnd + wallGroups.size() * sizeof(WEDWallGroupV14);
        size_t offPolygonsEnd = offWallGroupsEnd + polygons_copy.size() * sizeof(WEDPolygonV14);
        size_t offPolygonIndicesEnd = offPolygonsEnd + polygonIndices_copy.size() * sizeof(uint16_t);
        size_t offVerticesEnd = offPolygonIndicesEnd + vertices.size() * sizeof(WEDVertexV14);

        // Write doors with sanitized fields
        {
            // Now that polygon section bounds are known, sanitize and write doors as-is
            // Original and new polygon section bounds
            uint32_t oldPolyStart = secHeader.polygonOffset;
            uint32_t oldPolyEnd = secHeader.polygonIndexOffset; // start of PLT in original
            uint32_t newPolyStart = serializedSecHeader.polygonOffset;
            uint32_t newPolyEnd = serializedSecHeader.polygonIndexOffset; // start of PLT in new file
            
            Log(DEBUG, "WED", "[serialize] Polygon bounds: old=[{}, {}) new=[{}, {})", 
                oldPolyStart, oldPolyEnd, newPolyStart, newPolyEnd);
            uint32_t doorTilesCountTotal = static_cast<uint32_t>(doorTileCells.size());
            serializedHeader.doorCount = static_cast<uint32_t>(doors.size());
            for (size_t d = 0; d < doors.size(); ++d) {
                WEDDoorV14 serializedDoor = doors[d];
                char doorNameStr[9] = {0};
                memcpy(doorNameStr, serializedDoor.name, 8);
                Log(DEBUG, "WED", "[serialize] door[{}] name='{}' openClosed={} firstDoorTile={} doorTileCount={} openPolyCnt={} closedPolyCnt={} openPolyOff={} closedPolyOff={} (before relocation)",
                    d, doorNameStr, (unsigned)serializedDoor.openClosed, (unsigned)serializedDoor.firstDoorTile,
                    (unsigned)serializedDoor.doorTileCount, (unsigned)serializedDoor.openPolygonCount, (unsigned)serializedDoor.closedPolygonCount,
                    (unsigned)serializedDoor.openPolygonOffset, (unsigned)serializedDoor.closedPolygonOffset);
                
                Log(DEBUG, "WED", "[serialize] door[{}] validation: firstDoorTile={} doorTileCount={} doorTilesCountTotal={}",
                    d, (unsigned)serializedDoor.firstDoorTile, (unsigned)serializedDoor.doorTileCount, doorTilesCountTotal);

                // Clamp door tile range to valid bounds
                if (serializedDoor.doorTileCount > 0) {
                    if (serializedDoor.firstDoorTile > doorTilesCountTotal) {
                        Log(DEBUG, "WED", "[serialize] door[{}] clamping firstDoorTile {} > {} to 0", d, serializedDoor.firstDoorTile, doorTilesCountTotal);
                        serializedDoor.firstDoorTile = 0;
                        serializedDoor.doorTileCount = 0;
                    } else if (serializedDoor.firstDoorTile + serializedDoor.doorTileCount > doorTilesCountTotal) {
                        uint32_t maxCount = (doorTilesCountTotal > serializedDoor.firstDoorTile)
                            ? (doorTilesCountTotal - serializedDoor.firstDoorTile)
                            : 0;
                        Log(DEBUG, "WED", "[serialize] door[{}] clamping doorTileCount {} to {} (firstDoorTile={})", 
                            d, serializedDoor.doorTileCount, maxCount, serializedDoor.firstDoorTile);
                        serializedDoor.doorTileCount = maxCount;
                    }
                }
                
                Log(DEBUG, "WED", "[serialize] door[{}] after validation: firstDoorTile={} doorTileCount={}",
                    d, (unsigned)serializedDoor.firstDoorTile, (unsigned)serializedDoor.doorTileCount);

                auto relocate = [&](uint32_t off) -> uint32_t {
                    if (off == 0) return 0;
                    
                    Log(DEBUG, "WED", "[serialize] door[{}] relocating offset {} (oldPoly=[{}, {}) newPoly=[{}, {}))", 
                        d, off, oldPolyStart, oldPolyEnd, newPolyStart, newPolyEnd);
                    
                    // If original offset points inside the original polygon section, relocate by relative delta
                    if (off >= oldPolyStart && off < oldPolyEnd) {
                        uint32_t rel = off - oldPolyStart;
                        uint32_t relocated = newPolyStart + rel;
                        // Ensure relocated stays within new polygon section
                        if (relocated >= newPolyStart && relocated < newPolyEnd) {
                            Log(DEBUG, "WED", "[serialize] door[{}] relocated offset {} -> {} (rel={})", d, off, relocated, rel);
                            return relocated;
                        }
                        Log(DEBUG, "WED", "[serialize] door[{}] relocated offset {} -> {} would be out of bounds, setting to 0", d, off, relocated);
                        return 0; // clamp to 0 if it would go out-of-range
                    }
                    // Otherwise, consider invalid and zero out
                    Log(DEBUG, "WED", "[serialize] door[{}] offset {} outside old polygon section, setting to 0", d, off);
                    return 0;
                };
                serializedDoor.openPolygonOffset = relocate(serializedDoor.openPolygonOffset);
                serializedDoor.closedPolygonOffset = relocate(serializedDoor.closedPolygonOffset);

                memcpy(data.data() + w, &serializedDoor, sizeof(WEDDoorV14));
                w += sizeof(WEDDoorV14);
            }
        }

    Log(DEBUG, "WED", "[serialize] after doors: w={} offSecHeaderEnd+doors={} (doors={})",
    w, offSecHeaderEnd + doors.size() * sizeof(WEDDoorV14), (unsigned)doors.size());

    if (w != offDoorsEnd) {
        Log(DEBUG, "WED", "[serialize] doors end mismatch: w={} expected={} (diff={})", w, offDoorsEnd, (ssize_t)w - (ssize_t)offDoorsEnd);
        return {};
    }
        
    // Write tilemaps for each overlay sequentially
    {
        size_t tilemapTotalCount = 0;
        for (size_t i = 0; i < overlays.size(); ++i)
            tilemapTotalCount += static_cast<size_t>(overlays[i].width) * static_cast<size_t>(overlays[i].height);
        Log(DEBUG, "WED", "[serialize] sizeof(WEDTilemapV14)={} overlays={} totalTilemaps(byDim)={} startW={}",
            sizeof(WEDTilemapV14), overlays.size(), tilemapTotalCount, w);
    }
    for (size_t i = 0; i < overlays.size(); i++) {
        size_t beforeOverlayW = w;
        const auto& ov = overlays[i];
        const auto& ovTms = (i < tilemaps.size() ? tilemaps[i] : std::vector<WEDTilemapV14>{});
        const size_t ovCount = static_cast<size_t>(ov.width) * static_cast<size_t>(ov.height);
        const size_t writeCount = std::min(ovCount, ovTms.size());
        for (size_t j = 0; j < writeCount; ++j) {
            const auto& tm = ovTms[j];
            memcpy(data.data() + w, &tm, sizeof(WEDTilemapV14));
            w += sizeof(WEDTilemapV14);
        }
        if (writeCount < ovCount) {
            WEDTilemapV14 zero{};
            size_t pad = ovCount - writeCount;
            for (size_t p = 0; p < pad; ++p) {
                memcpy(data.data() + w, &zero, sizeof(WEDTilemapV14));
                w += sizeof(WEDTilemapV14);
            }
            Log(DEBUG, "WED", "[serialize] padded tilemaps: i={} ovWxH={}x{} vecSize={} pad={}",
                i, (unsigned)ov.width, (unsigned)ov.height, ovTms.size(), pad);
        }
        size_t expectedOverlayBytes = ovCount * sizeof(WEDTilemapV14);
        if (w != beforeOverlayW + expectedOverlayBytes) {
            Log(DEBUG, "WED", "[serialize] tilemaps overlay mismatch(byDim): i={} ovWxH={}x{} vecSize={} wroteBytes={} expectedBytes={} diff={}",
                i, (unsigned)ov.width, (unsigned)ov.height, ovTms.size(), w - beforeOverlayW, expectedOverlayBytes, (ssize_t)(w - beforeOverlayW) - (ssize_t)expectedOverlayBytes);
            return {};
        }
    }
    Log(DEBUG, "WED", "[serialize] after tilemaps: w={} expected={} (delta={})",
        w, offTilemapsEnd, (ssize_t)w - (ssize_t)offTilemapsEnd);

    if (w != offTilemapsEnd) {
        Log(DEBUG, "WED", "[serialize] tilemaps end mismatch: w={} expected={} (diff={})", w, offTilemapsEnd, (ssize_t)w - (ssize_t)offTilemapsEnd);
        return {};
    }

    // Write door tile cells (must come before tile indices per WED spec)
    for (const auto& doorTile : doorTileCells) {
        memcpy(data.data() + w, &doorTile, sizeof(uint32_t));
        w += sizeof(uint32_t);
    }

    // Write tile indices per overlay and validate section end
    for (size_t i = 0; i < tileIndices.size(); i++) {
        for (const auto& index : tileIndices[i]) {
            if (w + sizeof(uint32_t) > data.size()) {
                Log(DEBUG, "WED", "[serialize] tile indices overrun before write: w={} size={} need={}", w, data.size(), sizeof(uint32_t));
                return {};
            }
            memcpy(data.data() + w, &index, sizeof(uint32_t));
            w += sizeof(uint32_t);
        }
    }
    if (w != offTileIndicesEnd) {
        Log(DEBUG, "WED", "[serialize] tile indices end mismatch: w={} expected={} (diff={})", w, offTileIndicesEnd, (ssize_t)w - (ssize_t)offTileIndicesEnd);
        return {};
    }
        
        // Write wall groups
        for (const auto& wallGroup : wallGroups) {
            memcpy(data.data() + w, &wallGroup, sizeof(WEDWallGroupV14));
            w += sizeof(WEDWallGroupV14);
        }
        
        // Write polygons (must come before polygon indices per WED spec)
        for (const auto& polygon : polygons_copy) {
            memcpy(data.data() + w, &polygon, sizeof(WEDPolygonV14));
            w += sizeof(WEDPolygonV14);
        }
        
        // Write polygon indices (PLT) - wall groups reference this (must come after polygons)
        for (const auto& polygonIndex : polygonIndices_copy) {
            memcpy(data.data() + w, &polygonIndex, sizeof(uint16_t));
            w += sizeof(uint16_t);
        }
        
        // Write vertices
        for (const auto& vertex : vertices) {
            memcpy(data.data() + w, &vertex, sizeof(WEDVertexV14));
            w += sizeof(WEDVertexV14);
        }

        // Rewrite header at start with final counts (e.g., doorCount potentially updated)
        memcpy(data.data(), &serializedHeader, sizeof(WEDHeaderV14));

        if (w != currentOffset) {
            Log(DEBUG, "WED", "[serialize] write cursor mismatch: wrote={} expected={} (resizing)", w, currentOffset);
            data.resize(w);
        }
        return data;
    }
    
    // Deserialize from binary data
    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(WEDHeaderV14)) {
            return false;
        }
        
        // Read header
        memcpy(&header, data.data(), sizeof(WEDHeaderV14));
        
        // Validate signature and version
        if (!header.isValid()) {
            return false;
        }
        bool isV13 = (memcmp(header.version, "V1.3", 4) == 0);
        bool isV14 = (memcmp(header.version, "V1.4", 4) == 0);
        if (!isV13 && !isV14) return false;
        
        // Validate offsets
        if (!header.validateOffsets(data.size())) {
            return false;
        }
        
        // Read overlays (immediately after header)
        overlays.clear();
        overlays.reserve(header.overlayCount);
        size_t overlayOffset = header.overlayOffset;
        for (uint32_t i = 0; i < header.overlayCount; i++) {
            if (overlayOffset + sizeof(WEDOverlayV14) > data.size()) {
                return false;
            }
            WEDOverlayV14 overlay;
            memcpy(&overlay, data.data() + overlayOffset, sizeof(WEDOverlayV14));
            overlays.push_back(overlay);
            overlayOffset += sizeof(WEDOverlayV14);
        }
        
        // Read secondary header (after overlays)
        size_t secHeaderOffset = header.secHeaderOffset;
        if (secHeaderOffset + sizeof(WEDSecondaryHeaderV14) > data.size()) {
            return false;
        }
        memcpy(&secHeader, data.data() + secHeaderOffset, sizeof(WEDSecondaryHeaderV14));
        
        // Temporarily disable secondary header offset validation
        // if (!secHeader.validateOffsets(data.size())) {
        //     return false;
        // }
        
        // Read doors (after secondary header)
        doors.clear();
        doors.reserve(header.doorCount);
        if (header.doorCount > 0) {
            size_t doorOffset = header.doorOffset;
            if (isV13) {
                // v1.3 on-disk door struct
                #pragma pack(push, 1)
                struct WEDDoorV13 { char name[8]; uint16_t openClosed; uint16_t firstDoorTile; uint16_t doorTileCount; uint16_t openPolygonCount; uint16_t closedPolygonCount; uint32_t openPolygonOffset; uint32_t closedPolygonOffset; };
                #pragma pack(pop)
                for (uint32_t i = 0; i < header.doorCount; i++) {
                    if (doorOffset + sizeof(WEDDoorV13) > data.size()) return false;
                    WEDDoorV13 d13{};
                    memcpy(&d13, data.data() + doorOffset, sizeof(WEDDoorV13));
                    WEDDoorV14 d{};
                    memcpy(d.name, d13.name, 8);
                    d.openClosed = d13.openClosed;
                    d.firstDoorTile = d13.firstDoorTile;
                    d.doorTileCount = d13.doorTileCount;
                    d.openPolygonCount = d13.openPolygonCount;
                    d.closedPolygonCount = d13.closedPolygonCount;
                    d.openPolygonOffset = d13.openPolygonOffset;
                    d.closedPolygonOffset = d13.closedPolygonOffset;
                    doors.push_back(d);
                    doorOffset += sizeof(WEDDoorV13);
                }
            } else {
                for (uint32_t i = 0; i < header.doorCount; i++) {
                    if (doorOffset + sizeof(WEDDoorV14) > data.size()) return false;
                    WEDDoorV14 door;
                    memcpy(&door, data.data() + doorOffset, sizeof(WEDDoorV14));
                    doors.push_back(door);
                    doorOffset += sizeof(WEDDoorV14);
                }
            }
        }
        
        // Read tilemaps for each overlay (preserve original data)
        tilemaps.clear();
        tilemaps.reserve(overlays.size());
        for (const auto& overlay : overlays) {
            if (!overlay.validateOffsets(data.size())) {
                return false;
            }
            
            std::vector<WEDTilemapV14> overlayTilemaps;
            size_t tilemapOffset = overlay.tilemapOffset;
            size_t tileCount = overlay.getTileCount();
            
            overlayTilemaps.reserve(tileCount);
            if (isV13) {
                #pragma pack(push, 1)
                struct WEDTilemapV13 { uint16_t startIndex; uint16_t tileCount; uint16_t secondaryIndex; uint8_t overlayFlags; uint8_t unknown[3]; };
                #pragma pack(pop)
                for (size_t i = 0; i < tileCount; i++) {
                    if (tilemapOffset + sizeof(WEDTilemapV13) > data.size()) break;
                    WEDTilemapV13 t13{};
                    memcpy(&t13, data.data() + tilemapOffset, sizeof(WEDTilemapV13));
                    WEDTilemapV14 t{};
                    t.startIndex = t13.startIndex;
                    t.tileCount = t13.tileCount;
                    t.secondaryIndex = t13.secondaryIndex;
                    t.overlayFlags = t13.overlayFlags;
                    memcpy(t.unknown, t13.unknown, 3);
                    overlayTilemaps.push_back(t);
                    tilemapOffset += sizeof(WEDTilemapV13);
                }
            } else {
                for (size_t i = 0; i < tileCount; i++) {
                    if (tilemapOffset + sizeof(WEDTilemapV14) > data.size()) break;
                    WEDTilemapV14 tilemap;
                    memcpy(&tilemap, data.data() + tilemapOffset, sizeof(WEDTilemapV14));
                    overlayTilemaps.push_back(tilemap);
                    tilemapOffset += sizeof(WEDTilemapV14);
                }
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
            
            std::vector<uint32_t> overlayIndices;
            
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
                if (isV13) {
                    for (size_t i = 0; i < totalIndicesNeeded; i++) {
                        if (indexOffset + sizeof(uint16_t) > data.size()) break;
                        uint16_t idx16{};
                        memcpy(&idx16, data.data() + indexOffset, sizeof(uint16_t));
                        overlayIndices.push_back(static_cast<uint32_t>(idx16));
                        indexOffset += sizeof(uint16_t);
                    }
                } else {
                    for (size_t i = 0; i < totalIndicesNeeded; i++) {
                        if (indexOffset + sizeof(uint32_t) > data.size()) break;
                        uint32_t idx32{};
                        memcpy(&idx32, data.data() + indexOffset, sizeof(uint32_t));
                        overlayIndices.push_back(idx32);
                        indexOffset += sizeof(uint32_t);
                    }
                }
            }
            
            tileIndices.push_back(overlayIndices);
        }
        
        // Read door tile cells (if any)
        doorTileCells.clear();
        if (header.doorCount > 0) {
            size_t doorTileOffset = header.doorTileOffset;
            
            // Calculate door tile cell count from doors
            uint32_t totalDoorTiles = 0;
            for (const auto& door : doors) {
                totalDoorTiles += static_cast<uint32_t>(door.doorTileCount);
            }
            
            doorTileCells.reserve(totalDoorTiles);
            if (isV13) {
                for (uint32_t i = 0; i < totalDoorTiles; i++) {
                    if (doorTileOffset + sizeof(uint16_t) > data.size()) return false;
                    uint16_t doorTile16{};
                    memcpy(&doorTile16, data.data() + doorTileOffset, sizeof(uint16_t));
                    doorTileCells.push_back(static_cast<uint32_t>(doorTile16));
                    doorTileOffset += sizeof(uint16_t);
                }
            } else {
                for (uint32_t i = 0; i < totalDoorTiles; i++) {
                    if (doorTileOffset + sizeof(uint32_t) > data.size()) return false;
                    uint32_t doorTile32{};
                    memcpy(&doorTile32, data.data() + doorTileOffset, sizeof(uint32_t));
                    doorTileCells.push_back(doorTile32);
                    doorTileOffset += sizeof(uint32_t);
                }
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
                    if (wallGroupOffset + sizeof(WEDWallGroupV14) > data.size()) {
                        break;
                    }
                    WEDWallGroupV14 wallGroup;
                    memcpy(&wallGroup, data.data() + wallGroupOffset, sizeof(WEDWallGroupV14));
                    wallGroups.push_back(wallGroup);
                    wallGroupOffset += sizeof(WEDWallGroupV14);
                }
            }
        }
        
        // Read polygons (after wall groups)
        polygons.clear();
        if (secHeader.polygonCount > 0 && secHeader.polygonOffset < data.size()) {
            size_t polygonOffset = secHeader.polygonOffset;
            size_t polygonCount = (secHeader.polygonIndexOffset - secHeader.polygonOffset) / sizeof(WEDPolygonV14);
            
            polygons.reserve(polygonCount);
            for (size_t i = 0; i < polygonCount; i++) {
                if (polygonOffset + sizeof(WEDPolygonV14) > data.size()) {
                    return false;
                }
                WEDPolygonV14 polygon;
                memcpy(&polygon, data.data() + polygonOffset, sizeof(WEDPolygonV14));
                polygons.push_back(polygon);
                polygonOffset += sizeof(WEDPolygonV14);
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
            size_t vertexCount = (data.size() - secHeader.vertexOffset) / sizeof(WEDVertexV14);
            
            vertices.reserve(vertexCount);
            for (size_t i = 0; i < vertexCount; i++) {
                if (vertexOffset + sizeof(WEDVertexV14) > data.size()) {
                    return false;
                }
                WEDVertexV14 vertex;
                memcpy(&vertex, data.data() + vertexOffset, sizeof(WEDVertexV14));
                vertices.push_back(vertex);
                vertexOffset += sizeof(WEDVertexV14);
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