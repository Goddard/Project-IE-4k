#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "plugins/CommandRegistry.h"
#include "core/SClassID.h"

namespace ProjectIE4k {

// KEY V1 Header structure (24 bytes)
#pragma pack(push, 1)
struct KEYHeader {
    char signature[4];      // 'KEY '
    char version[4];        // 'V1  '
    uint32_t bifCount;      // Number of BIF files
    uint32_t resCount;      // Number of resources
    uint32_t bifOffset;     // Offset to BIF entries
    uint32_t resOffset;     // Offset to RES entries
};

// KEY V1 BIF Entry structure (14 bytes)
struct KEYBIFEntry {
    uint32_t fileSize;      // Size of BIF file
    uint32_t fileOffset;    // Offset to BIF file
    uint16_t filenameLength; // Length of BIF filename
    uint16_t filenameOffset; // Offset to BIF filename
    std::string filename;   // BIF filename (not in file, loaded separately)
};

// KEY V1 RES Entry structure (14 bytes)
struct KEYRESEntry {
    char name[8];           // Resource name (resref)
    uint16_t type;          // Resource type
    uint32_t locator;       // Resource locator (bits: 31-20 BIF index, 19-14 tileset index, 13-0 file index)
};
#pragma pack(pop)

class KEY {
public:
    KEY(const std::string& filename);
    ~KEY();
    
    bool load();
    bool save(const std::string& filename = "");
    void printInfo() const;
    bool addResource(const std::string& resourceName, SClass_ID resourceType);
    
    // Static helper function to get resource type from file extension

    
    // Command registry function
    static void registerCommands(CommandTable& commandTable);
    
    // Additional methods for resource management
    void addResource(const std::string& name, uint16_t type, uint32_t locator);
    void addResource(const std::string& name, uint16_t type, uint32_t bifIndex, uint32_t fileIndex);
    bool removeResource(const std::string& name);
    bool removeResource(size_t index);
    
    // Access methods
    KEYBIFEntry& getBIF(size_t index);
    KEYRESEntry& getResource(size_t index);
    const std::vector<KEYBIFEntry>& getBIFs() const { return bifs; }
    const std::vector<KEYRESEntry>& getResources() const { return resources; }
    
    // Search methods
    std::vector<size_t> findResourcesByType(uint16_t type) const;
    std::vector<size_t> findResourcesByName(const std::string& name) const;
    size_t findBIFByName(const std::string& name) const;

private:
    std::string filePath;
    bool valid;
    
    // File data
    KEYHeader header;
    std::vector<KEYBIFEntry> bifs;
    std::vector<KEYRESEntry> resources;
    
    // Helper methods
    bool readHeader();
    bool readBIFs();
    bool readResources();
    bool readBIFNames();
    
    bool writeHeader(std::ofstream& file);
    bool writeBIFs(std::ofstream& file);
    bool writeResources(std::ofstream& file);
    bool writeBIFNames(std::ofstream& file);
    
    uint32_t calculateOffsets();
};

} // namespace ProjectIE4k
