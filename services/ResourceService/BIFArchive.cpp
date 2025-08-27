#include "BIFArchive.h"

#include <zlib.h>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "core/CFG.h"
#include "core/Logging/Logging.h"
#include "core/SClassID.h"

namespace ProjectIE4k {

BIFArchive::BIFArchive(const std::string &path)
    : filePath(path), fileHandle(nullptr), verbose(false) {
  memset(&header, 0, sizeof(header));
}

BIFArchive::~BIFArchive() {
    close();
}

bool BIFArchive::open() {
    if (fileHandle) {
        close();
    }
    
    fileHandle = fopen(filePath.c_str(), "rb");
    if (!fileHandle) {
        Log(ERROR, "BIFArchive", "Failed to open BIF file: {}", filePath);
        return false;
    }
    
    // Check if the file is compressed and decompress if needed
    if (isCompressed()) {
      Log(DEBUG, "BIFArchive",
          "BIF file is compressed, checking for cached decompressed "
          "version...");

      // Check if we already have a decompressed version in cache
      std::string cachePath = getCacheFilePath(filePath);
      if (std::filesystem::exists(cachePath)) {
        Log(DEBUG, "BIFArchive", "Found cached decompressed file: {}",
            cachePath);

        // Close the compressed file and open the cached version
        fclose(fileHandle);
        fileHandle = nullptr;

        fileHandle = fopen(cachePath.c_str(), "rb");
        if (fileHandle) {
          Log(DEBUG, "BIFArchive",
              "Successfully opened cached decompressed file");

          // Validate that the cached file is a valid BIF file
          if (validateCachedBIF()) {
            Log(DEBUG, "BIFArchive",
                "Cached file validation successful, using cached version");
            // Continue with reading header and entries
          } else {
            Log(WARNING, "BIFArchive",
                "Cached file validation failed, will decompress again");
            fclose(fileHandle);
            fileHandle = nullptr;

            // Reopen the original compressed file for decompression
            fileHandle = fopen(filePath.c_str(), "rb");
            if (!fileHandle) {
              Log(ERROR, "BIFArchive",
                  "Failed to reopen compressed file for decompression");
              return false;
            }
            // Fall through to decompression
          }
        } else {
          Log(WARNING, "BIFArchive",
              "Failed to open cached file, will decompress again");
          // Reopen the original compressed file for decompression
          fileHandle = fopen(filePath.c_str(), "rb");
          if (!fileHandle) {
            Log(ERROR, "BIFArchive",
                "Failed to reopen compressed file for decompression");
            return false;
          }
          // Fall through to decompression
        }
      } else {
        Log(DEBUG, "BIFArchive", "No cached version found, decompressing...");
      }

      // If we still have the compressed file open, decompress it
      if (fileHandle && isCompressed()) {
        if (!decompressBIF()) {
          Log(ERROR, "BIFArchive", "Failed to decompress BIF file: {}",
              filePath);
          close();
          return false;
        }
      }
    } else {
        Log(DEBUG, "BIFArchive", "BIF file is not compressed");
    }
    
    if (!readHeader()) {
        Log(ERROR, "BIFArchive", "Failed to read BIF header: {}", filePath);
        close();
        return false;
    }
    
    if (!readEntries()) {
        Log(ERROR, "BIFArchive", "Failed to read BIF entries: {}", filePath);
        close();
        return false;
    }
    
    Log(DEBUG, "BIFArchive", "Successfully opened BIF file: {} ({} files, {} tiles)", 
        filePath, header.fileCount, header.tileCount);
    
    // Debug: Show the first few tile entries to verify we're reading the right file
    if (header.tileCount > 0) {
        Log(DEBUG, "BIFArchive", "First 3 tile entries:");
        for (size_t i = 0; i < std::min(header.tileCount, 3u); i++) {
            Log(DEBUG, "BIFArchive", "  Tile {}: resLoc={:08x}, offset={}, numTiles={}, size={}", 
                i, tileEntries[i].resLoc, tileEntries[i].offset, tileEntries[i].numTiles, tileEntries[i].size);
        }
    }
    
    // Build complete size index after successfully reading all entries
    if (!buildCompleteSizeIndex()) {
        Log(WARNING, "BIFArchive", "Failed to build complete size index for BIF: {}", filePath);
        // Don't fail the open operation, just warn about size indexing
    }
    
    return true;
}

void BIFArchive::close() {
    if (fileHandle) {
        fclose(fileHandle);
        fileHandle = nullptr;
    }
    fileEntries.clear();
    tileEntries.clear();
    resourceSizeIndex.clear();
}

std::vector<uint8_t> BIFArchive::getResourceData(uint32_t locator, uint16_t type) {
    Log(DEBUG, "BIFArchive", "getResourceData called: locator={}, type={}", locator, type);
    
    if (!fileHandle) {
        Log(ERROR, "BIFArchive", "BIF file not open: {}", filePath);
        return std::vector<uint8_t>();
    }
    
    Log(DEBUG, "BIFArchive", "About to call getResourceOffset");
    uint32_t size;
    uint32_t offset = getResourceOffset(locator, type, size);
    Log(DEBUG, "BIFArchive", "getResourceOffset returned: offset={}, size={}", offset, size);
    
    if (offset == 0 || size == 0) {
        Log(ERROR, "BIFArchive", "Resource not found in BIF: locator={}, type={}", locator, type);
        return std::vector<uint8_t>();
    }
    
    // Seek to the resource data
    Log(DEBUG, "BIFArchive", "Seeking to offset: {}", offset);
    if (fseek(fileHandle, offset, SEEK_SET) != 0) {
        Log(ERROR, "BIFArchive", "Failed to seek to resource data: {}", filePath);
        return std::vector<uint8_t>();
    }
    
    // Read the resource data
    Log(DEBUG, "BIFArchive", "Reading {} bytes of resource data", size);
    std::vector<uint8_t> data(size);
    size_t bytesRead = fread(data.data(), 1, size, fileHandle);
    Log(DEBUG, "BIFArchive", "Read {} bytes of resource data", bytesRead);
    
    if (bytesRead != size) {
        Log(ERROR, "BIFArchive", "Failed to read resource data: expected {}, got {}", size, bytesRead);
        return std::vector<uint8_t>();
    }
    
    Log(DEBUG, "BIFArchive", "Successfully read resource data");
    return data;
}

std::vector<uint8_t> BIFArchive::getResourceDataByOffset(uint32_t offset, uint32_t size) {
    Log(DEBUG, "BIFArchive", "getResourceDataByOffset called: offset={}, size={}", offset, size);
    
    if (!fileHandle) {
        Log(ERROR, "BIFArchive", "BIF file not open: {}", filePath);
        return std::vector<uint8_t>();
    }
    
    // Seek to the resource data
    Log(DEBUG, "BIFArchive", "Seeking to offset: {}", offset);
    if (fseek(fileHandle, offset, SEEK_SET) != 0) {
        Log(ERROR, "BIFArchive", "Failed to seek to resource data: {}", filePath);
        return std::vector<uint8_t>();
    }
    
    // Read the resource data
    Log(DEBUG, "BIFArchive", "Reading {} bytes of resource data", size);
    std::vector<uint8_t> data(size);
    size_t bytesRead = fread(data.data(), 1, size, fileHandle);
    Log(DEBUG, "BIFArchive", "Read {} bytes of resource data", bytesRead);
    
    if (bytesRead != size) {
        Log(ERROR, "BIFArchive", "Failed to read resource data: expected {}, got {}", size, bytesRead);
        return std::vector<uint8_t>();
    }
    
    Log(DEBUG, "BIFArchive", "Successfully read resource data by offset");
    return data;
}

std::vector<uint8_t> BIFArchive::getResourceDataOptimized(uint32_t locator, uint16_t type) {
    Log(DEBUG, "BIFArchive", "getResourceDataOptimized called: locator={:08x}, type={}", locator, type);
    
    if (!fileHandle) {
        Log(ERROR, "BIFArchive", "BIF file not open: {}", filePath);
        return std::vector<uint8_t>();
    }
    
    uint32_t offset = 0;
    uint32_t size = 0;
    
    // Single lookup pass - find offset and size in one operation
    if (type == IE_TIS_CLASS_ID) {
        // For TIS files, use tile entries
        // Extract tileset index from locator (bits 14-19)
        uint32_t tilesetIndex = (locator & TILESET_INDEX_MASK) >> 14;
        Log(DEBUG, "BIFArchive", "Searching {} tile entries for tileset index {}", tileEntries.size(), tilesetIndex);
        for (const auto& tile : tileEntries) {
            // Extract tileset index from BIF tile entry (bits 14-19)
            uint32_t tileTilesetIndex = (tile.resLoc & TILESET_INDEX_MASK) >> 14;
            if (tileTilesetIndex == tilesetIndex) {
                offset = tile.offset;
                size = tile.numTiles * tile.size; // Total size for all tiles
                Log(DEBUG, "BIFArchive", "Found TIS resource: offset={}, tileSize={}, numTiles={}, totalSize={}", 
                    offset, tile.size, tile.numTiles, size);
                break;
            }
        }
    } else {
        // For other files, use file entries
        // Extract file index from locator (bits 0-13)
        uint32_t fileIndex = locator & FILE_INDEX_MASK;
        Log(DEBUG, "BIFArchive", "Searching {} file entries for file index {}", fileEntries.size(), fileIndex);
        for (const auto& file : fileEntries) {
            // Extract file index from BIF file entry (bits 0-13)
            uint32_t bifFileIndex = file.resLoc & FILE_INDEX_MASK;
            if (bifFileIndex == fileIndex) {
                offset = file.offset;
                size = file.size;
                Log(DEBUG, "BIFArchive", "Found file resource: offset={}, size={}", offset, size);
                break;
            }
        }
    }
    
    if (offset == 0 || size == 0) {
        Log(ERROR, "BIFArchive", "Resource not found in BIF: locator={}, type={}", locator, type);
        return std::vector<uint8_t>();
    }
    
    // Single file I/O operation - seek and read in one pass
    Log(DEBUG, "BIFArchive", "Seeking to offset: {} and reading {} bytes", offset, size);
    if (fseek(fileHandle, offset, SEEK_SET) != 0) {
        Log(ERROR, "BIFArchive", "Failed to seek to resource data: {}", filePath);
        return std::vector<uint8_t>();
    }
    
    std::vector<uint8_t> data(size);
    size_t bytesRead = fread(data.data(), 1, size, fileHandle);
    Log(DEBUG, "BIFArchive", "Read {} bytes of resource data", bytesRead);
    
    if (bytesRead != size) {
        Log(ERROR, "BIFArchive", "Failed to read resource data: expected {}, got {}", size, bytesRead);
        return std::vector<uint8_t>();
    }

    Log(DEBUG, "BIFArchive", "Successfully read resource data");
    return data;
}

bool BIFArchive::getResourceInfo(uint32_t locator, uint16_t type, uint32_t& offset, uint32_t& size) {
    Log(DEBUG, "BIFArchive", "getResourceInfo called: locator={:08x}, type={}", locator, type);
    
    // For TIS files, use tile entries
    if (type == IE_TIS_CLASS_ID) {
        // Extract tileset index from locator (bits 14-19)
        uint32_t tilesetIndex = (locator & TILESET_INDEX_MASK) >> 14;
        Log(DEBUG, "BIFArchive", "Searching {} tile entries for tileset index {}", tileEntries.size(), tilesetIndex);
        for (size_t i = 0; i < tileEntries.size(); i++) {
            const auto& tile = tileEntries[i];
            // Extract tileset index from BIF tile entry (bits 14-19)
            uint32_t tileTilesetIndex = (tile.resLoc & TILESET_INDEX_MASK) >> 14;
            Log(DEBUG, "BIFArchive", "Tile {}: resLoc={:08x}, tilesetIndex={}, offset={}, numTiles={}, size={}", 
                i, tile.resLoc, tileTilesetIndex, tile.offset, tile.numTiles, tile.size);
            if (tileTilesetIndex == tilesetIndex) {
                offset = tile.offset;
                // For TIS files, the size field represents the size of one tile
                // We need to return the total size: numTiles * tileSize
                size = tile.numTiles * tile.size;
                Log(DEBUG, "BIFArchive", "Found TIS resource info: offset={}, tileSize={}, numTiles={}, totalSize={}", 
                    offset, tile.size, tile.numTiles, size);
                return true;
            }
        }
    } else {
        // For other files, use file entries
        // Extract file index from locator (bits 0-13)
        uint32_t fileIndex = locator & FILE_INDEX_MASK;
        Log(DEBUG, "BIFArchive", "Searching {} file entries for file index {}", fileEntries.size(), fileIndex);
        for (size_t i = 0; i < fileEntries.size(); i++) {
            const auto& file = fileEntries[i];
            // Extract file index from BIF file entry (bits 0-13)
            uint32_t bifFileIndex = file.resLoc & FILE_INDEX_MASK;
            Log(DEBUG, "BIFArchive", "File {}: resLoc={:08x}, fileIndex={}, offset={}, size={}", 
                i, file.resLoc, bifFileIndex, file.offset, file.size);
            if (bifFileIndex == fileIndex) {
                offset = file.offset;
                size = file.size;
                Log(DEBUG, "BIFArchive", "Found file resource info: offset={}, size={}", offset, size);
                return true;
            }
        }
    }
    
    Log(ERROR, "BIFArchive", "Resource info not found: locator={:08x}, type={}", locator, type);
    return false;
}

bool BIFArchive::getTileEntryInfo(uint32_t locator, uint32_t& tileCount) {
    Log(DEBUG, "BIFArchive", "getTileEntryInfo called: locator={:08x}", locator);
    
    // Extract tileset index from locator (bits 14-19)
    uint32_t tilesetIndex = (locator & TILESET_INDEX_MASK) >> 14;
    Log(DEBUG, "BIFArchive", "Looking for tileset index: {}", tilesetIndex);
    
    for (const auto& tile : tileEntries) {
        // Extract tileset index from BIF tile entry (bits 14-19)
        uint32_t tileTilesetIndex = (tile.resLoc & TILESET_INDEX_MASK) >> 14;
        if (tileTilesetIndex == tilesetIndex) {
            tileCount = tile.numTiles;
            Log(DEBUG, "BIFArchive", "Found tile entry info: tileCount={}", tileCount);
            return true;
        }
    }
    
    Log(ERROR, "BIFArchive", "Tile entry not found: locator={:08x}, tilesetIndex={}", locator, tilesetIndex);
    return false;
}

bool BIFArchive::readHeader() {
    if (!fileHandle) {
        return false;
    }
    
    // Read BIF header directly (no byte order conversion needed)
    if (fread(&header, sizeof(header), 1, fileHandle) != 1) {
        Log(ERROR, "BIFArchive", "Failed to read BIF header");
        return false;
    }
    
    // Validate signature
    if (strncmp(header.signature, "BIFF", 4) != 0) {
        Log(ERROR, "BIFArchive", "Invalid BIF signature: {:.4s}", header.signature);
        return false;
    }
    
    // Check BIF version
    bool isV2 = (strncmp(header.version, "V2 ", 3) == 0);
    Log(DEBUG, "BIFArchive", "BIF version: {}", isV2 ? "V2" : "V1");
    
    Log(DEBUG, "BIFArchive", "BIF header: files={}, tiles={}, fileOffset={}", 
        header.fileCount, header.tileCount, header.fileOffset);
    
    return true;
}

bool BIFArchive::readEntries() {
    if (!fileHandle) {
        return false;
    }
    
    Log(DEBUG, "BIFArchive", "Reading {} file entries from offset {}", header.fileCount, header.fileOffset);

    // Read file entries - BULK READ OPTIMIZATION
    if (header.fileCount > 0) {
        fileEntries.resize(header.fileCount);
        
        if (fseek(fileHandle, header.fileOffset, SEEK_SET) != 0) {
            Log(ERROR, "BIFArchive", "Failed to seek to file entries");
            return false;
        }

        // Bulk read all file entries at once instead of one-by-one
        size_t totalFileBytes = header.fileCount * sizeof(BIFFileEntry);
        if (fread(fileEntries.data(), 1, totalFileBytes, fileHandle) !=
            totalFileBytes) {
          Log(ERROR, "BIFArchive",
              "Failed to bulk read {} file entries ({} bytes)",
              header.fileCount, totalFileBytes);
          return false;
        }

        if (verbose) {
          Log(DEBUG, "BIFArchive",
              "Successfully bulk read all {} file entries ({} bytes)",
              header.fileCount, totalFileBytes);
        }
    }

    // Read tile entries (if any) - BULK READ OPTIMIZATION
    if (header.tileCount > 0) {
        tileEntries.resize(header.tileCount);
        
        // Tile entries follow file entries
        uint32_t tileOffset = header.fileOffset + (header.fileCount * sizeof(BIFFileEntry));
        if (fseek(fileHandle, tileOffset, SEEK_SET) != 0) {
            Log(ERROR, "BIFArchive", "Failed to seek to tile entries");
            return false;
        }

        // Bulk read all tile entries at once instead of one-by-one
        size_t totalTileBytes = header.tileCount * sizeof(BIFTileEntry);
        if (fread(tileEntries.data(), 1, totalTileBytes, fileHandle) !=
            totalTileBytes) {
          Log(ERROR, "BIFArchive",
              "Failed to bulk read {} tile entries ({} bytes)",
              header.tileCount, totalTileBytes);
          return false;
        }

        if (verbose) {
          Log(DEBUG, "BIFArchive",
              "Successfully bulk read all {} tile entries ({} bytes)",
              header.tileCount, totalTileBytes);
        }
    }
    
    return true;
}

uint32_t BIFArchive::getResourceOffset(uint32_t locator, uint16_t type, uint32_t& size) {
  if (verbose) {
    Log(DEBUG, "BIFArchive", "Looking for resource: locator={}, type={}",
        locator, type);
  }

    // For TIS files, use tile entries
    if (type == IE_TIS_CLASS_ID) {
        for (const auto& tile : tileEntries) {
            if (tile.resLoc == locator) {
                size = tile.size;
                if (verbose) {
                  Log(DEBUG, "BIFArchive",
                      "Found TIS resource: offset={}, size={}", tile.offset,
                      size);
                }
                return tile.offset;
            }
        }
    } else {
        // For other files, use file entries
        for (const auto& file : fileEntries) {
            if (file.resLoc == locator) {
                size = file.size;
                if (verbose) {
                  Log(DEBUG, "BIFArchive",
                      "Found file resource: offset={}, size={}", file.offset,
                      size);
                }
                return file.offset;
            }
        }
    }
    
    Log(ERROR, "BIFArchive", "Resource not found: locator={}, type={}", locator, type);
    size = 0;
    return 0;
}

bool BIFArchive::isCompressed() const {
    if (!fileHandle) {
        return false;
    }
    
    // Save current position
    long currentPos = ftell(fileHandle);
    if (currentPos == -1) {
        return false;
    }
    
    // Seek to beginning and read signature
    if (fseek(fileHandle, 0, SEEK_SET) != 0) {
        return false;
    }
    
    char signature[4];
    if (fread(signature, 1, 4, fileHandle) != 4) {
        return false;
    }
    
    // Restore position
    fseek(fileHandle, currentPos, SEEK_SET);
    
    Log(DEBUG, "BIFArchive", "File signature: {:.4s}", signature);
    
    return (memcmp(signature, "BIFC", 4) == 0);
}

bool BIFArchive::validateCachedBIF() {
  if (!fileHandle) {
    return false;
  }

  // Save current position
  long currentPos = ftell(fileHandle);
  if (currentPos == -1) {
    return false;
  }

  // Seek to beginning and read signature
  if (fseek(fileHandle, 0, SEEK_SET) != 0) {
    return false;
  }

  char signature[4];
  if (fread(signature, 1, 4, fileHandle) != 4) {
    // Restore position and return false
    fseek(fileHandle, currentPos, SEEK_SET);
    return false;
  }

  // Check if it's a valid BIF signature (not BIFC)
  bool isValidBIF = (memcmp(signature, "BIF ", 4) == 0);

  if (isValidBIF) {
    // Additional validation: check file size and basic structure
    if (fseek(fileHandle, 0, SEEK_END) != 0) {
      fseek(fileHandle, currentPos, SEEK_SET);
      return false;
    }

    long fileSize = ftell(fileHandle);
    if (fileSize < 24) { // Minimum BIF header size
      Log(WARNING, "BIFArchive", "Cached file too small: {} bytes", fileSize);
      fseek(fileHandle, currentPos, SEEK_SET);
      return false;
    }

    // Check if we can read the header fields
    if (fseek(fileHandle, 4, SEEK_SET) != 0) { // Skip signature, read version
      fseek(fileHandle, currentPos, SEEK_SET);
      return false;
    }

    char version[4];
    if (fread(version, 1, 4, fileHandle) != 4) {
      fseek(fileHandle, currentPos, SEEK_SET);
      return false;
    }

    Log(DEBUG, "BIFArchive",
        "Cached file validation: signature={:.4s}, version={:.4s}, size={} "
        "bytes",
        signature, version, fileSize);
  }

  // Restore position
  fseek(fileHandle, currentPos, SEEK_SET);

  return isValidBIF;
}

bool BIFArchive::decompressBIF() {
    if (!fileHandle) {
        return false;
    }
    
    Log(DEBUG, "BIFArchive", "Decompressing BIF file: {}", filePath);
    
    // Read BIFC header
    BIFCHeader bifcHeader;
    if (fseek(fileHandle, 0, SEEK_SET) != 0) {
        Log(ERROR, "BIFArchive", "Failed to seek to beginning of file");
        return false;
    }
    
    if (fread(&bifcHeader, sizeof(BIFCHeader), 1, fileHandle) != 1) {
        Log(ERROR, "BIFArchive", "Failed to read BIFC header");
        return false;
    }
    
    // Validate signature
    if (memcmp(bifcHeader.signature, "BIFC", 4) != 0) {
        Log(ERROR, "BIFArchive", "Invalid BIFC signature");
        return false;
    }
    
    Log(DEBUG, "BIFArchive", "BIFC header: original length = {} bytes", bifcHeader.origlen);
    
    // Decompress block by block
    std::vector<uint8_t> decompressedData;
    decompressedData.reserve(bifcHeader.origlen);
    
    uint32_t totalDecompressed = 0;
    int blockCount = 0;
    
    while (totalDecompressed < bifcHeader.origlen) {
        // Read block header (8 bytes: 4 bytes uncompressed size + 4 bytes compressed size)
        uint32_t uncompressedSize, compressedSize;
        if (fread(&uncompressedSize, sizeof(uint32_t), 1, fileHandle) != 1) {
            Log(ERROR, "BIFArchive", "Failed to read block {} uncompressed size", blockCount);
            return false;
        }
        if (fread(&compressedSize, sizeof(uint32_t), 1, fileHandle) != 1) {
            Log(ERROR, "BIFArchive", "Failed to read block {} compressed size", blockCount);
            return false;
        }

        if (verbose) {
          Log(DEBUG, "BIFArchive", "Block {}: uncompressed={}, compressed={}",
              blockCount, uncompressedSize, compressedSize);
        }

        // Read compressed block data
        std::vector<uint8_t> compressedBlock(compressedSize);
        if (fread(compressedBlock.data(), 1, compressedSize, fileHandle) != compressedSize) {
            Log(ERROR, "BIFArchive", "Failed to read block {} compressed data", blockCount);
            return false;
        }
        
        // Decompress block
        std::vector<uint8_t> decompressedBlock(uncompressedSize);
        uLongf actualDecompressedSize = uncompressedSize;
        
        int result = uncompress(decompressedBlock.data(), &actualDecompressedSize,
                               compressedBlock.data(), compressedSize);
        
        if (result != Z_OK) {
            Log(ERROR, "BIFArchive", "zlib decompression failed for block {} with error: {}", blockCount, result);
            return false;
        }
        
        if (actualDecompressedSize != uncompressedSize) {
            Log(ERROR, "BIFArchive", "Block {} decompressed size mismatch: expected {}, got {}",
                blockCount, uncompressedSize, actualDecompressedSize);
            return false;
        }
        
        // Append to total decompressed data
        decompressedData.insert(decompressedData.end(), decompressedBlock.begin(), decompressedBlock.end());
        totalDecompressed += uncompressedSize;
        blockCount++;

        if (verbose) {
          Log(DEBUG, "BIFArchive",
              "Successfully decompressed block {}: {} bytes", blockCount,
              uncompressedSize);
        }
    }
    
    if (totalDecompressed != bifcHeader.origlen) {
        Log(ERROR, "BIFArchive", "Total decompressed size mismatch: expected {}, got {}",
            bifcHeader.origlen, totalDecompressed);
        return false;
    }
    
    // Close the original file and create a temporary file with decompressed data
    fclose(fileHandle);
    fileHandle = nullptr;
    
    // Create a temporary file with the decompressed data in the cache directory
    std::string tempPath = getCacheFilePath(filePath);
    FILE* tempFile = fopen(tempPath.c_str(), "wb");
    if (!tempFile) {
        Log(ERROR, "BIFArchive", "Failed to create temporary decompressed file: {}", tempPath);
        return false;
    }
    
    if (fwrite(decompressedData.data(), 1, decompressedData.size(), tempFile) != decompressedData.size()) {
        Log(ERROR, "BIFArchive", "Failed to write decompressed data to temporary file");
        fclose(tempFile);
        return false;
    }
    
    fclose(tempFile);
    
    // Open the temporary file for reading
    fileHandle = fopen(tempPath.c_str(), "rb");
    if (!fileHandle) {
        Log(ERROR, "BIFArchive", "Failed to open temporary decompressed file");
        return false;
    }
    
    Log(DEBUG, "BIFArchive", "Successfully decompressed BIF file: {} bytes in {} blocks", 
        decompressedData.size(), blockCount);
    return true;
}

// Complete size indexing implementation
bool BIFArchive::buildCompleteSizeIndex() {
    Log(DEBUG, "BIFArchive", "Building complete size index for BIF: {}", filePath);
    
    if (!fileHandle) {
        Log(ERROR, "BIFArchive", "Cannot build size index: BIF file not open");
        return false;
    }
    
    resourceSizeIndex.clear();
    
    // Index all file entries
    for (const auto& file : fileEntries) {
        resourceSizeIndex[file.resLoc] = file.size;
        if (verbose) {
          Log(DEBUG, "BIFArchive",
              "Indexed file entry: locator={:08x}, size={}", file.resLoc,
              file.size);
        }
    }
    
    // Index all tile entries (TIS files)
    for (const auto& tile : tileEntries) {
        uint32_t totalSize = tile.numTiles * tile.size;
        resourceSizeIndex[tile.resLoc] = totalSize;
        if (verbose) {
          Log(DEBUG, "BIFArchive",
              "Indexed tile entry: locator={:08x}, tileSize={}, numTiles={}, "
              "totalSize={}",
              tile.resLoc, tile.size, tile.numTiles, totalSize);
        }
    }
    
    Log(DEBUG, "BIFArchive", "Complete size index built with {} entries", resourceSizeIndex.size());
    return true;
}

uint32_t BIFArchive::getResourceSize(uint32_t locator) const {
    auto it = resourceSizeIndex.find(locator);
    if (it != resourceSizeIndex.end()) {
        return it->second;
    }
    return 0; // Not found in index
}

std::string BIFArchive::getCacheDirectory() {
    // Get the directory where the executable is located
    std::filesystem::path exePath = std::filesystem::current_path();
    std::filesystem::path cacheDir = exePath / ".pie4kcache";

    // Add game type subdirectory if available
    if (!PIE4K_CFG.GameType.empty()) {
      cacheDir = cacheDir / PIE4K_CFG.GameType;
    }

    // Create the cache directory if it doesn't exist
    if (!std::filesystem::exists(cacheDir)) {
        try {
          std::filesystem::create_directories(cacheDir);
          Log(DEBUG, "BIFArchive", "Created cache directory: {}",
              cacheDir.string());
        } catch (const std::filesystem::filesystem_error& e) {
            Log(ERROR, "BIFArchive", "Failed to create cache directory {}: {}", cacheDir.string(), e.what());
            // Fallback to current directory
            return ".";
        }
    }
    
    return cacheDir.string();
}

std::string BIFArchive::getCacheFilePath(const std::string& originalPath) {
    std::filesystem::path original(originalPath);
    std::string filename = original.filename().string() + ".decompressed";
    std::filesystem::path cachePath = std::filesystem::path(getCacheDirectory()) / filename;
    return cachePath.string();
}

} // namespace ProjectIE4k
