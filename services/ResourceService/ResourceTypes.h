#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ProjectIE4k {

// Simple struct for data passing
struct ResourceData {
    std::vector<uint8_t> data;
    std::string filename;
    
    ResourceData() = default;
    ResourceData(const std::vector<uint8_t>& d, const std::string& f) : data(d), filename(f) {}
};

// KEY file structures
struct KEYHeader {
    char signature[4];      // "KEY "
    char version[4];        // "V1  "
    uint32_t bifCount;      // Number of BIF files
    uint32_t keyCount;      // Number of resources
    uint32_t bifOffset;     // Offset to BIF entries
    uint32_t keyOffset;     // Offset to resource entries
};

struct BIFEntry {
    uint32_t fileSize;      // Size of BIF file
    uint32_t filenameOffset; // Offset to filename
    uint16_t filenameLength; // Length of filename (including NUL)
    uint16_t flags;         // BIF flags
    
    std::string filename;   // BIF filename
    std::string fullPath;   // Full path to BIF file
    bool found;             // Whether BIF file exists
};

struct ResourceEntry {
    char name[8];           // Resource name (8 bytes, null-padded)
    uint16_t type;          // Resource type
    uint32_t locator;       // BIF index and offset
};

// Resource management structures
struct ResourceKey {
    std::string name;
    uint16_t type;
    
    ResourceKey(const std::string& n, uint16_t t) : name(n), type(t) {}
    
    bool operator==(const ResourceKey& other) const {
        return name == other.name && type == other.type;
    }
};

struct ResourceInfo {
    std::string name;
    uint16_t type;
    uint32_t bifIndex;
    uint32_t locator;  // Resource locator from KEY file
    uint32_t offset;   // Offset from KEY file (for backward compatibility)
    uint32_t size;     // Size (will be filled by BIF service)
    bool isValid;
    
    ResourceInfo() : type(0), bifIndex(0), locator(0), offset(0), size(0), isValid(false) {}
    ResourceInfo(const std::string& n, uint16_t t, uint32_t bif, uint32_t loc, uint32_t off, uint32_t sz)
        : name(n), type(t), bifIndex(bif), locator(loc), offset(off), size(sz), isValid(true) {}
};

// Hash function for ResourceKey
struct ResourceKeyHash {
    std::size_t operator()(const ResourceKey& key) const {
        std::size_t h1 = std::hash<std::string>{}(key.name);
        std::size_t h2 = std::hash<uint16_t>{}(key.type);
        return h1 ^ (h2 << 1);
    }
};

// BIF file structures
struct BIFHeader {
    char signature[4];      // "BIFF"
    char version[4];        // "V1 " or "V2 "
    uint32_t fileCount;     // Number of file entries
    uint32_t tileCount;     // Number of tile entries
    uint32_t fileOffset;    // Offset to file entries
};

struct BIFCHeader {
    char signature[4];      // "BIFC"
    char version[4];        // "V1.0"
    uint32_t origlen;       // Original uncompressed length
};

struct BIFFileEntry {
    uint32_t resLoc;        // Resource locator
    uint32_t offset;        // Offset to resource data
    uint32_t size;          // Size of resource data
    uint16_t type;          // Resource type
    uint16_t unused;        // Unused field
};

struct BIFTileEntry {
    uint32_t resLoc;        // Resource locator
    uint32_t offset;        // Offset to tile data
    uint32_t numTiles;      // Number of tiles
    uint32_t size;          // Size of tile data
    uint16_t type;          // Resource type
    uint16_t unused;        // Unused field
};

// Forward declaration
class BIFArchive;

} // namespace ProjectIE4k
