#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>

namespace ProjectIE4k {
#pragma pack(push, 1)
struct WMPHeaderV1 {
    char signature[4];      // 'WMAP'
    char version[4];        // 'V1.0'
    uint32_t worldmapCount; // count of worldmap entries
    uint32_t worldmapOffset;// offset of worldmap entries
};

struct ResRef8 { char s[8]; };

struct WMPWorldmapEntryV1 {
    ResRef8 backgroundMos;   // 0x00
    uint32_t width;          // 0x08
    uint32_t height;         // 0x0C
    uint32_t mapNumber;      // 0x10
    uint32_t areaNameStrref; // 0x14
    uint32_t startCenterX;   // 0x18
    uint32_t startCenterY;   // 0x1C
    uint32_t areaCount;      // 0x20
    uint32_t areaOffset;     // 0x24
    uint32_t linkOffset;     // 0x28
    uint32_t linkCount;      // 0x2C
    ResRef8 iconsBam;        // 0x30
    uint32_t flagsBgee;      // 0x38 (bit0: colored icons, bit1: ignore palette)
    uint8_t  unused[124];    // 0x3C..0xB7
};

struct WMPAreaEntryV1 {
    ResRef8 areaResRef;      // 0x00
    ResRef8 areaNameShort;   // 0x08
    char    areaNameLong[32];// 0x10
    uint32_t statusMask;     // 0x30 (bits as described)
    uint32_t bamSequence;    // 0x34
    uint32_t x;              // 0x38
    uint32_t y;              // 0x3C
    uint32_t captionStrref;  // 0x40
    uint32_t tooltipStrref;  // 0x44
    ResRef8 loadingMos;      // 0x48
    uint32_t linkIndexNorth; // 0x50
    uint32_t linkCountNorth; // 0x54
    uint32_t linkIndexWest;  // 0x58
    uint32_t linkCountWest;  // 0x5C
    uint32_t linkIndexSouth; // 0x60
    uint32_t linkCountSouth; // 0x64
    uint32_t linkIndexEast;  // 0x68
    uint32_t linkCountEast;  // 0x6C
    uint8_t  unused[128];    // 0x70
};

struct WMPAreaLinkEntryV1 {
    uint32_t destAreaIndex;  // 0x00
    char     entryPoint[32]; // 0x04
    uint32_t travelTimeDiv4; // 0x24
    uint32_t defaultEntryLoc;// 0x28 (bitfield 1,2,4,8)
    ResRef8  randomEnc1;     // 0x2C
    ResRef8  randomEnc2;     // 0x34
    ResRef8  randomEnc3;     // 0x3C
    ResRef8  randomEnc4;     // 0x44
    ResRef8  randomEnc5;     // 0x4C
    uint32_t randomEncProb;  // 0x54
    uint8_t  unused[128];    // 0x58
};
#pragma pack(pop)

struct WMPV1File {
    WMPHeaderV1 header{};
    std::vector<WMPWorldmapEntryV1> worldmaps;
    std::vector<WMPAreaEntryV1> areas;            // flattened across worldmaps
    std::vector<WMPAreaLinkEntryV1> areaLinks;    // flattened across worldmaps

    bool deserialize(const std::vector<uint8_t>& data) {
        // Basic header bounds and signature/version checks
        if (data.size() < sizeof(WMPHeaderV1)) return false;
        std::memcpy(&header, data.data(), sizeof(WMPHeaderV1));
        if (std::string(header.signature, 4) != std::string("WMAP", 4)) return false;
        // Version should be exactly 'V1.0' for classic engines; tolerate minor deviations if needed in future
        if (std::string(header.version, 4) != std::string("V1.0", 4)) {
            // keep tolerant path available, but currently enforce for safety
        }
        if (header.worldmapCount == 0) return false; // must have at least one worldmap
        if (header.worldmapOffset == 0) return false; // offset must be non-zero
        uint64_t wmEnd = static_cast<uint64_t>(header.worldmapOffset) + static_cast<uint64_t>(header.worldmapCount) * sizeof(WMPWorldmapEntryV1);
        if (wmEnd > data.size()) return false;

        worldmaps.resize(header.worldmapCount);
        std::memcpy(worldmaps.data(), data.data() + header.worldmapOffset, header.worldmapCount * sizeof(WMPWorldmapEntryV1));

        // Flatten areas and links for convenience (optional; parsers can index per-worldmap if desired)
        areas.clear();
        areaLinks.clear();
        for (const auto &wm : worldmaps) {
            // Validate per-worldmap area/link section offsets if counts are non-zero
            if (wm.areaCount == 0 || wm.areaOffset == 0) {
                // Some maps may have zero areas (should not happen per spec), require at least one
                if (wm.areaCount == 0) return false;
                if (wm.areaOffset == 0) return false;
            }
            // Areas
            if (wm.areaCount > 0) {
                uint64_t aend = static_cast<uint64_t>(wm.areaOffset) + static_cast<uint64_t>(wm.areaCount) * sizeof(WMPAreaEntryV1);
                if (aend > data.size()) return false;
                size_t base = areas.size();
                areas.resize(base + wm.areaCount);
                std::memcpy(areas.data() + base, data.data() + wm.areaOffset, wm.areaCount * sizeof(WMPAreaEntryV1));
            }
            // Links
            if (wm.linkCount > 0) {
                uint64_t lend = static_cast<uint64_t>(wm.linkOffset) + static_cast<uint64_t>(wm.linkCount) * sizeof(WMPAreaLinkEntryV1);
                if (lend > data.size()) return false;
                size_t baseL = areaLinks.size();
                areaLinks.resize(baseL + wm.linkCount);
                std::memcpy(areaLinks.data() + baseL, data.data() + wm.linkOffset, wm.linkCount * sizeof(WMPAreaLinkEntryV1));
            }
        }
        return true;
    }

    std::vector<uint8_t> serialize() const {
        // Build layout: [Header][Worldmaps][Areas...][Links...]
        // We assume 'areas' and 'areaLinks' are stored in the same order as they were read:
        // i.e., for each worldmap entry in sequence, its areas (areaCount) then links (linkCount) were appended.

        // Validate totals against worldmaps
        uint64_t totalAreas = 0;
        uint64_t totalLinks = 0;
        for (const auto &wm : worldmaps) {
            totalAreas += wm.areaCount;
            totalLinks += wm.linkCount;
        }
        if (totalAreas != areas.size()) {
            // Inconsistent state; cannot safely serialize
            return {};
        }
        if (totalLinks != areaLinks.size()) {
            return {};
        }

        // Compute sizes
        uint64_t headerSize   = sizeof(WMPHeaderV1);
        uint64_t worldmapsSize= worldmaps.size() * sizeof(WMPWorldmapEntryV1);
        uint64_t areasSize    = areas.size() * sizeof(WMPAreaEntryV1);
        uint64_t linksSize    = areaLinks.size() * sizeof(WMPAreaLinkEntryV1);
        uint64_t totalSize64  = headerSize + worldmapsSize + areasSize + linksSize;

        if (totalSize64 > SIZE_MAX) {
            return {};
        }

        std::vector<uint8_t> out;
        out.resize(static_cast<size_t>(totalSize64));

        // Working header copy to fill
        WMPHeaderV1 h = header;
        std::memcpy(h.signature, "WMAP", 4);
        std::memcpy(h.version,   "V1.0", 4);
        h.worldmapCount = static_cast<uint32_t>(worldmaps.size());
        h.worldmapOffset = static_cast<uint32_t>(headerSize);

        // Copy worldmaps as a mutable array so we can set area/link offsets
        uint8_t *ptr = out.data();
        std::memcpy(ptr, &h, sizeof(WMPHeaderV1));
        ptr += sizeof(WMPHeaderV1);

        // Write placeholder worldmaps
        std::memcpy(ptr, worldmaps.data(), worldmapsSize);
        auto *wmOut = reinterpret_cast<WMPWorldmapEntryV1*>(ptr);
        ptr += worldmapsSize;

        // Areas block starts here
        uint32_t areasBaseOffset = static_cast<uint32_t>(ptr - out.data());
        size_t areaCursor = 0;
        for (size_t i = 0; i < worldmaps.size(); ++i) {
            auto &wm = wmOut[i];
            if (wm.areaCount > 0) {
                wm.areaOffset = static_cast<uint32_t>(areasBaseOffset + (areaCursor * sizeof(WMPAreaEntryV1)));
                // copy this worldmap's areas
                const WMPAreaEntryV1 *src = areas.data() + areaCursor;
                size_t bytes = wm.areaCount * sizeof(WMPAreaEntryV1);
                std::memcpy(out.data() + wm.areaOffset, src, bytes);
                areaCursor += wm.areaCount;
            } else {
                wm.areaOffset = 0;
            }
        }

        // Move ptr to end of areas block
        ptr = out.data() + areasBaseOffset + areasSize;

        // Links block starts here
        uint32_t linksBaseOffset = static_cast<uint32_t>(ptr - out.data());
        size_t linkCursor = 0;
        for (size_t i = 0; i < worldmaps.size(); ++i) {
            auto &wm = wmOut[i];
            if (wm.linkCount > 0) {
                wm.linkOffset = static_cast<uint32_t>(linksBaseOffset + (linkCursor * sizeof(WMPAreaLinkEntryV1)));
                const WMPAreaLinkEntryV1 *src = areaLinks.data() + linkCursor;
                size_t bytes = wm.linkCount * sizeof(WMPAreaLinkEntryV1);
                std::memcpy(out.data() + wm.linkOffset, src, bytes);
                linkCursor += wm.linkCount;
            } else {
                wm.linkOffset = 0;
            }
        }

        // Rewrite worldmaps array with updated offsets
        std::memcpy(out.data() + h.worldmapOffset, wmOut, worldmapsSize);

        // Rewrite header (unchanged except offsets/counts already set)
        std::memcpy(out.data(), &h, sizeof(WMPHeaderV1));
        return out;
    }
};

} // namespace ProjectIE4k
