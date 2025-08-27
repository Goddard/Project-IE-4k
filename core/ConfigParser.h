#pragma once

#include <string>
#include <map>
#include <fstream>
#include <sstream>

namespace ProjectIE4k {

class ConfigParser {
private:
    std::map<std::string, std::string> configValues;

public:
    ConfigParser() = default;
    
    // Load config from file
    bool loadFromFile(const std::string& filename);
    
    // Get a config value with default
    std::string get(const std::string& key, const std::string& defaultValue = "") const;
    
    // Set a config value
    void set(const std::string& key, const std::string& value);
    
    // Check if a key exists
    bool hasKey(const std::string& key) const;
    
    // Get all config values
    const std::map<std::string, std::string>& getAllValues() const { return configValues; }

private:
    // Parse a single line
    bool parseLine(const std::string& line);
    
    // Trim whitespace
    std::string trim(const std::string& str) const;
};

} // namespace ProjectIE4k 