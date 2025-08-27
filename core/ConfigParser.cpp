#include "ConfigParser.h"
#include <algorithm>
#include <cctype>

namespace ProjectIE4k {

bool ConfigParser::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    int lineNumber = 0;
    
    while (std::getline(file, line)) {
        lineNumber++;
        if (!parseLine(line)) {
            // Log warning about invalid line, but continue parsing
            // We could add logging here if needed
        }
    }
    
    return true;
}

std::string ConfigParser::get(const std::string& key, const std::string& defaultValue) const {
    auto it = configValues.find(key);
    if (it != configValues.end()) {
        return it->second;
    }
    return defaultValue;
}

void ConfigParser::set(const std::string& key, const std::string& value) {
    configValues[key] = value;
}

bool ConfigParser::hasKey(const std::string& key) const {
    return configValues.find(key) != configValues.end();
}

bool ConfigParser::parseLine(const std::string& line) {
    std::string trimmedLine = trim(line);
    
    // Skip empty lines and comments
    if (trimmedLine.empty() || trimmedLine[0] == '#' || trimmedLine[0] == ';') {
        return true;
    }
    
    // Find the equals sign
    size_t equalsPos = trimmedLine.find('=');
    if (equalsPos == std::string::npos) {
        return false; // Invalid line format
    }
    
    // Extract key and value
    std::string key = trim(trimmedLine.substr(0, equalsPos));
    std::string value = trim(trimmedLine.substr(equalsPos + 1));
    
    // Remove quotes if present
    if (value.length() >= 2 && 
        ((value[0] == '"' && value[value.length()-1] == '"') ||
         (value[0] == '\'' && value[value.length()-1] == '\''))) {
        value = value.substr(1, value.length() - 2);
    }
    
    if (!key.empty()) {
        configValues[key] = value;
        return true;
    }
    
    return false;
}

std::string ConfigParser::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

} // namespace ProjectIE4k 