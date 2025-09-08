#include "IdsMapCache.h"
#include "core/Logging/Logging.h"
#include <algorithm>
#include <regex>
#include <sstream>
#include <iostream>
#include <mutex>


namespace ProjectIE4k {

// Static member definitions
std::map<std::string, std::unique_ptr<IdsMap>> IdsMapCache::cache_;
std::map<std::string, std::string> IdsMapCache::rawContentCache_;
static std::mutex cacheMutex; // guards cache_ and rawContentCache_
static std::once_flag initOnceFlag; // ensures single initialization per process
static std::mutex globalCacheMutex;
static bool globalCacheInitialized = false;

// IdsMap methods
IdsMapEntry* IdsMap::get(long value) {
    auto it = entries_.find(value);
    return (it != entries_.end()) ? it->second.get() : nullptr;
}

IdsMapEntry* IdsMap::get(const std::string& symbol) {
    auto it = symbolMap_.find(symbol);
    return (it != symbolMap_.end()) ? it->second : nullptr;
}

void IdsMap::addEntry(long value, const std::string& symbol) {
    auto entry = std::make_unique<IdsMapEntry>(value, symbol);
    IdsMapEntry* entryPtr = entry.get();
    
    entries_[value] = std::move(entry);
    symbolMap_[symbol] = entryPtr;
}

// IdsMapCache methods
void IdsMapCache::initializeWithIdsFiles(const std::map<std::string, std::vector<uint8_t>>& idsFiles) {
    // Ensure this runs only once per process to avoid concurrent initialization races
    std::call_once(initOnceFlag, [&]() {
        Log(DEBUG, "IdsMapCache", "Initializing with {} IDS files", idsFiles.size());
        
        for (const auto& [fileName, data] : idsFiles) {
            if (data.empty()) continue;
            auto idsMap = loadIdsFile(fileName, data);
            if (!idsMap) continue;
            std::string normalizedName = normalizeName(fileName);
            std::string rawContent(data.begin(), data.end());
            std::lock_guard<std::mutex> lk(cacheMutex);
            cache_[normalizedName] = std::unique_ptr<IdsMap>(idsMap);
            rawContentCache_[normalizedName] = rawContent;
            Log(DEBUG, "IdsMapCache", "Loaded IDS file: {} -> {} ({} bytes)", fileName, normalizedName, rawContent.length());
        }
        
        Log(DEBUG, "IdsMapCache", "Initialized cache with {} IDS maps", cache_.size());
    });
}

IdsMap* IdsMapCache::get(const std::string& idsName) {
    std::string normalizedName = normalizeName(idsName);
    
    std::lock_guard<std::mutex> lk(cacheMutex);
    auto it = cache_.find(normalizedName);
    if (it != cache_.end()) {
        return it->second.get();
    }
    
    return nullptr;
}

std::string IdsMapCache::getIdsSymbol(const std::string& idsName, long value) {
    IdsMap* map = get(idsName);
    if (!map) {
        return "";
    }
    
    IdsMapEntry* entry = map->get(value);
    return entry ? entry->getSymbol() : "";
}

long IdsMapCache::getIdsValue(const std::string& idsName, const std::string& symbol) {
    IdsMap* map = get(idsName);
    if (!map) {
        return -1;
    }
    
    IdsMapEntry* entry = map->get(symbol);
    return entry ? entry->getValue() : -1;
}

void IdsMapCache::clearCache() {
    std::lock_guard<std::mutex> lk(cacheMutex);
    cache_.clear();
    rawContentCache_.clear();
}

std::string IdsMapCache::getRawContent(const std::string& idsName) {
    std::string normalizedName = normalizeName(idsName);
    
    std::lock_guard<std::mutex> lk(cacheMutex);
    auto it = rawContentCache_.find(normalizedName);
    if (it != rawContentCache_.end()) {
        return it->second;
    }
    
    return "";
}

IdsMap* IdsMapCache::loadIdsFile(const std::string& idsName, const std::vector<uint8_t>& data) {
    if (data.empty()) {
        Log(DEBUG, "IdsMapCache", "No data provided for IDS: {}", idsName);
        return nullptr;
    }
    
    std::string idsContent(data.begin(), data.end());
    Log(DEBUG, "IdsMapCache", "Loading IDS from preloaded data: {} ({} bytes)", idsName, data.size());
    
    // Check if file is encrypted (starts with 0xFF 0xFF)
    if (data.size() >= 2 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFF) {
        Log(DEBUG, "IdsMapCache", "Decrypting encrypted IDS file: {}", idsName);
        idsContent = decryptIdsFile(data);
    }
    
    // Store raw content for Signatures class
    std::string normalizedName = normalizeName(idsName);
    {
        std::lock_guard<std::mutex> lk(cacheMutex);
        rawContentCache_[normalizedName] = idsContent;
    }
    

    
    auto idsMap = std::make_unique<IdsMap>(idsName);
    
    std::istringstream stream(idsContent);
    std::string line;
    while (std::getline(stream, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Parse line: "123 SYMBOL_NAME" or "0x123 SYMBOL_NAME(optional params)"
        static const std::regex pattern(R"(^\s*(\d+|0x[0-9a-fA-F]+)\s+([a-zA-Z_][a-zA-Z0-9_]*))");
        std::smatch match;
        
        if (std::regex_search(line, match, pattern)) {
            std::string valueStr = match[1].str();
            std::string symbol = match[2].str();
            
            long value;
            if (valueStr.substr(0, 2) == "0x" || valueStr.substr(0, 2) == "0X") {
                value = std::stol(valueStr, nullptr, 16);
            } else {
                value = std::stol(valueStr);
            }
            
            // Trim whitespace from symbol
            symbol.erase(symbol.find_last_not_of(" \t\r\n") + 1);
            
            idsMap->addEntry(value, symbol);
        }
    }
    
    Log(DEBUG, "IdsMapCache", "Loaded {} entries from resource {}", 
        idsMap->getEntries().size(), idsName);
    
    return idsMap.release(); // Transfer ownership to caller
}

std::string IdsMapCache::normalizeName(const std::string& name) {
    std::string result = name;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    
    // Add .IDS extension if not present
    if (result.find(".IDS") == std::string::npos) {
        result += ".IDS";
    }
    
    return result;
}

std::string IdsMapCache::decryptIdsFile(const std::vector<uint8_t>& data) {
    // Infinity Engine IDS decryption key (64 bytes)
    static const uint8_t decryptKey[64] = {
        0x88, 0xa8, 0x8f, 0xba, 0x8a, 0xd3, 0xb9, 0xf5, 
        0xed, 0xb1, 0xcf, 0xea, 0xaa, 0xe4, 0xb5, 0xfb,
        0xeb, 0x82, 0xf9, 0x90, 0xca, 0xc9, 0xb5, 0xe7, 
        0xdc, 0x8e, 0xb7, 0xac, 0xee, 0xf7, 0xe0, 0xca,
        0x8e, 0xea, 0xca, 0x80, 0xce, 0xc5, 0xad, 0xb7, 
        0xc4, 0xd0, 0x84, 0x93, 0xd5, 0xf0, 0xeb, 0xc8,
        0xb4, 0x9d, 0xcc, 0xaf, 0xa5, 0x95, 0xba, 0x99, 
        0x87, 0xd2, 0x9d, 0xe3, 0x91, 0xba, 0x90, 0xca
    };
    
    if (data.size() < 2) {
        return "";
    }
    
    // Skip the 0xFF 0xFF header
    std::string decrypted;
    decrypted.reserve(data.size() - 2);
    
    for (size_t i = 2; i < data.size(); ++i) {
        uint8_t encryptedByte = data[i];
        uint8_t keyByte = decryptKey[(i - 2) % 64];
        uint8_t decryptedByte = encryptedByte ^ keyByte;
        decrypted.push_back(static_cast<char>(decryptedByte));
    }
    
    return decrypted;
}

bool IdsMapCache::initializeGlobalCache() {
    std::lock_guard<std::mutex> lock(globalCacheMutex);
    
    if (globalCacheInitialized) {
        Log(DEBUG, "IdsMapCache", "Global cache already initialized");
        return true;
    }
    
    Log(MESSAGE, "IdsMapCache", "Initializing global IDS cache for batch operations...");
    
    // This will be called by BCS::initializeSharedResources() 
    // which has access to loadResourceFromService
    // For now, return true and let BCS handle the actual loading
    globalCacheInitialized = true;
    
    Log(MESSAGE, "IdsMapCache", "Global IDS cache initialized successfully");
    return true;
}

} // namespace ProjectIE4k
