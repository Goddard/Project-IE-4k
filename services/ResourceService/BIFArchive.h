#pragma once

#include "ResourceTypes.h"
#include <string>
#include <vector>
#include <map>
#include <cstdio>

namespace ProjectIE4k {

/**
 * @brief Class for handling BIF archive files
 * 
 * This class is responsible for:
 * - Opening and reading BIF files
 * - Extracting resource data from BIF archives
 * - Managing BIF file entries and tile entries
 */
class BIFArchive {
private:
    // Bitmasks for resource locator parsing
    static constexpr uint32_t TILESET_INDEX_MASK = 0x0003F000;
    static constexpr uint32_t FILE_INDEX_MASK = 0x00003FFF;
    
    std::string filePath;
    FILE* fileHandle;
    BIFHeader header;
    std::vector<BIFFileEntry> fileEntries;
    std::vector<BIFTileEntry> tileEntries;
    
    // Complete size index for all resources in this BIF
    std::map<uint32_t, uint32_t> resourceSizeIndex; // locator -> size
    
    // Cache file tracking for cleanup
    std::string cacheFilePath;

    // Verbose logging control
    bool verbose;

  public:
    BIFArchive(const std::string& path);
    ~BIFArchive();
    
    // File operations
    bool open();
    void close();
    
    // Data extraction
    std::vector<uint8_t> getResourceData(uint32_t locator, uint16_t type);
    std::vector<uint8_t> getResourceDataByOffset(uint32_t offset, uint32_t size);
    std::vector<uint8_t> getResourceDataOptimized(uint32_t locator, uint16_t type);
    
    // Resource info
    bool getResourceInfo(uint32_t locator, uint16_t type, uint32_t& offset, uint32_t& size);
    bool getTileEntryInfo(uint32_t locator, uint32_t& tileCount);
    
    // Complete size indexing
    bool buildCompleteSizeIndex();
    uint32_t getResourceSize(uint32_t locator) const;
    const std::map<uint32_t, uint32_t>& getSizeIndex() const { return resourceSizeIndex; }
    
    // Utility methods
    bool isOpen() const { return fileHandle != nullptr; }
    const std::string& getFilePath() const { return filePath; }
    const BIFHeader& getHeader() const { return header; }

    // Cache Directory
    std::string getCacheDirectory();
    std::string getCacheFilePath(const std::string& originalPath);

    // Cache validation
    bool validateCachedBIF();

  private:
    // Internal parsing methods
    bool readHeader();
    bool readEntries();
    uint32_t getResourceOffset(uint32_t locator, uint16_t type, uint32_t& size);
    
    // Compression handling
    bool isCompressed() const;
    bool decompressBIF();
};

} // namespace ProjectIE4k 