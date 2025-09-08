#pragma once

#include <map>
#include <string>
#include <memory>
#include <vector>

namespace ProjectIE4k {

/**
 * Entry in an IDS map
 */
class IdsMapEntry {
public:
    IdsMapEntry(long value, const std::string& symbol) : value_(value), symbol_(symbol) {}

    long getValue() const { return value_; }
    const std::string& getSymbol() const { return symbol_; }

private:
    long value_;
    std::string symbol_;
};

/**
 * IDS map container
 */
class IdsMap {
public:
    IdsMap(const std::string& name) : name_(name) {}

    const std::string& getName() const { return name_; }
    
    /** Get entry by value */
    IdsMapEntry* get(long value);
    
    /** Get entry by symbol */
    IdsMapEntry* get(const std::string& symbol);
    
    /** Add entry */
    void addEntry(long value, const std::string& symbol);
    
    /** Get all entries */
    const std::map<long, std::unique_ptr<IdsMapEntry>>& getEntries() const { return entries_; }

private:
    std::string name_;
    std::map<long, std::unique_ptr<IdsMapEntry>> entries_;
    std::map<std::string, IdsMapEntry*> symbolMap_;
};

/**
 * Near Infinity-style IDS cache system
 */
class IdsMapCache {
public:
    /** Initialize cache with all available IDS files */
    static void initializeWithIdsFiles(const std::map<std::string, std::vector<uint8_t>>& idsFiles);
    
    /** Initialize global cache once for batch operations (thread-safe) */
    static bool initializeGlobalCache();
    
    /** Get IDS map by name (case-insensitive) */
    static IdsMap* get(const std::string& idsName);
    
    /** Get IDS symbol by value */
    static std::string getIdsSymbol(const std::string& idsName, long value);
    
    /** Get IDS value by symbol */
    static long getIdsValue(const std::string& idsName, const std::string& symbol);
    
    /** Clear all cached maps */
    static void clearCache();
    
    /** Get raw IDS file content for signatures */
    static std::string getRawContent(const std::string& idsName);

private:
    static std::map<std::string, std::unique_ptr<IdsMap>> cache_;
    static std::map<std::string, std::string> rawContentCache_;
    
    /** Load IDS file from data */
    static IdsMap* loadIdsFile(const std::string& idsName, const std::vector<uint8_t>& data);
    
    /** Normalize IDS name */
    static std::string normalizeName(const std::string& name);
    
    /** Decrypt encrypted IDS file */
    static std::string decryptIdsFile(const std::vector<uint8_t>& data);
};

} // namespace ProjectIE4k
