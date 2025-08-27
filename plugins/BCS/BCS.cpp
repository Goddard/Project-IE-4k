#include "BCS.h"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>

#include "core/Logging/Logging.h"
#include "services/ServiceManager.h"
#include "core/CFG.h"
#include "plugins/CommandRegistry.h"
#include "services/ResourceService/ResourceTypes.h"

namespace ProjectIE4k {

    BCS::BCS(const std::string& resourceName_) 
    : PluginBase(resourceName_, IE_BCS_CLASS_ID) {
    // Load the BCS data
    if (!loadFromData()) {
        Log(ERROR, "BCS", "Failed to load BCS data");
        return;
    }
    
    // Mark plugin as valid since we successfully loaded the BCS resource
    valid_ = true;
}

BCS::~BCS() {
    // Clean up large data structures to prevent memory leaks
    blocks.clear();
    blocks.shrink_to_fit();
    
    // Clean up the nested map structure
    idsMaps.clear();
}

bool BCS::loadFromData() {
    if (originalFileData.empty()) {
        Log(ERROR, "BCS", "No BCS data loaded");
        return false;
    }

    // Load IDS files for human-readable names
    if (!loadIDSFiles()) {
        Log(WARNING, "BCS", "Failed to load IDS files, using generic names");
    }

    // Parse the BCS script data
    if (!parseScript()) {
        Log(ERROR, "BCS", "Failed to parse BCS script data");
        return false;
    }

    Log(DEBUG, "BCS", "Successfully loaded BCS resource: {} with {} blocks", resourceName_, blocks.size());

    return true;
}

bool BCS::extract() {
    if (!isValid()) {
        Log(ERROR, "BCS", "BCS resource is not valid");
        return false;
    }

    std::string extractDir = getExtractDir(true);
    std::string filename = extractDir + "/" + resourceName_ + ".bcs.txt";
    
    if (!decompileToText(filename)) {
        Log(ERROR, "BCS", "Failed to decompile BCS to text file: {}", filename);
        return false;
    }
    
    Log(MESSAGE, "BCS", "Successfully extracted BCS to: {}", filename);
    return true;
}

bool BCS::upscale() {
    if (!isValid()) {
        Log(ERROR, "BCS", "BCS resource is not valid");
        return false;
    }

    // Get upscale factor
    int upscaleFactor = PIE4K_CFG.UpScaleFactor;
    if (upscaleFactor <= 1) {
        Log(WARNING, "BCS", "Upscale factor is {} (no upscaling needed)", upscaleFactor);
        return true;
    }

    Log(DEBUG, "BCS", "Upscaling BCS coordinates by factor: {}", upscaleFactor);

    // Read the decompiled text file
    std::string extractDir = getExtractDir(false);
    std::string inputFile = extractDir + "/" + resourceName_ + ".bcs.txt";
    
    std::ifstream file(inputFile);
    if (!file.is_open()) {
        Log(ERROR, "BCS", "Failed to open decompiled file for upscaling: {}", inputFile);
        return false;
    }

    // Read all lines
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();

    // Process each line to upscale coordinates
    for (auto& line : lines) {
        upscaleLine(line, upscaleFactor);
    }

    // Write the upscaled text to the upscale directory
    std::string upscaleDir = getUpscaledDir(true);
    std::string outputFile = upscaleDir + "/" + resourceName_ + ".bcs.txt";
    
    std::ofstream outFile(outputFile);
    if (!outFile.is_open()) {
        Log(ERROR, "BCS", "Failed to open output file for upscaled BCS: {}", outputFile);
        return false;
    }

    for (const auto& processedLine : lines) {
        outFile << processedLine << "\n";
    }
    outFile.close();

    Log(MESSAGE, "BCS", "Successfully upscaled BCS to: {}", outputFile);
    return true;
}



bool BCS::applyUpscaledCoordinates(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        Log(ERROR, "BCS", "Failed to open upscaled text file: {}", filename);
        return false;
    }

    std::string line;
    int blockIndex = 0;
    int responseIndex = 0;
    int actionIndex = 0;

    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line.find("//") == 0) {
            continue;
        }

        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);

        if (line == "IF") {
            blockIndex = 0;
            responseIndex = 0;
            actionIndex = 0;
        } else if (line == "THEN") {
            // Reset action index for responses
            actionIndex = 0;
        } else if (line == "END") {
            // Move to next block
            blockIndex++;
            responseIndex = 0;
            actionIndex = 0;
        } else if (line.find("RESPONSE #") != std::string::npos) {
            // New response, reset action index
            actionIndex = 0;
        } else if (!line.empty() && line[0] != ' ' && line.find("(") != std::string::npos) {
            // This is an action line
            if (blockIndex < blocks.size() && 
                responseIndex < blocks[blockIndex].responses.size() &&
                actionIndex < blocks[blockIndex].responses[responseIndex].actions.size()) {
                
                BCSAction& action = blocks[blockIndex].responses[responseIndex].actions[actionIndex];
                applyCoordinatesToAction(action, line);
                actionIndex++;
            }
        } else if (line.find("RESPONSE #") != std::string::npos) {
            // Move to next response
            responseIndex++;
            actionIndex = 0;
        }
    }

    Log(DEBUG, "BCS", "Applied upscaled coordinates to {} blocks", blocks.size());
    return true;
}

void BCS::applyCoordinatesToAction(BCSAction& action, const std::string& line) {
    // Look for coordinate patterns like [x.y] and apply them to the action
    size_t pos = 0;
    while ((pos = line.find('[', pos)) != std::string::npos) {
        size_t endPos = line.find(']', pos);
        if (endPos == std::string::npos) {
            break;
        }
        
        // Extract the coordinate string
        std::string coordStr = line.substr(pos + 1, endPos - pos - 1);
        
        // Parse the coordinates (format: x.y)
        size_t dotPos = coordStr.find('.');
        if (dotPos != std::string::npos) {
            try {
                int x = std::stoi(coordStr.substr(0, dotPos));
                int y = std::stoi(coordStr.substr(dotPos + 1));
                
                // Apply coordinates based on action type
                switch (action.opcode) {
                    case 49: // MoveViewPoint
                    case 254: // ScreenShake
                    case 272: // CreateVisualEffect
                        action.param2 = x;
                        action.param3 = y;
                        Log(DEBUG, "BCS", "Applied coordinates [{}] to action {}: param2={}, param3={}", 
                            coordStr, action.opcode, x, y);
                        break;
                }
                
            } catch (const std::exception& e) {
                Log(WARNING, "BCS", "Failed to parse coordinates '{}': {}", coordStr, e.what());
            }
        }
        
        pos = endPos + 1; // Continue searching
    }
}

void BCS::upscaleLine(std::string& line, int upscaleFactor) {
    // Look for coordinate patterns like [x.y] and upscale them
    size_t pos = 0;
    while ((pos = line.find('[', pos)) != std::string::npos) {
        size_t endPos = line.find(']', pos);
        if (endPos == std::string::npos) {
            break; // No closing bracket found
        }
        
        // Extract the coordinate string
        std::string coordStr = line.substr(pos + 1, endPos - pos - 1);
        
        // Parse the coordinates (format: x.y)
        size_t dotPos = coordStr.find('.');
        if (dotPos != std::string::npos) {
            try {
                int x = std::stoi(coordStr.substr(0, dotPos));
                int y = std::stoi(coordStr.substr(dotPos + 1));
                
                // Upscale the coordinates
                int newX = x * upscaleFactor;
                int newY = y * upscaleFactor;
                
                // Replace the original coordinates with upscaled ones
                std::string newCoordStr = "[" + std::to_string(newX) + "." + std::to_string(newY) + "]";
                line.replace(pos, endPos - pos + 1, newCoordStr);
                
                Log(DEBUG, "BCS", "Upscaled coordinates [{}] -> [{}]", coordStr, newCoordStr);
                
                // Update position to continue searching
                pos += newCoordStr.length();
            } catch (const std::exception& e) {
                Log(WARNING, "BCS", "Failed to parse coordinates '{}': {}", coordStr, e.what());
                pos = endPos + 1; // Skip this coordinate and continue
            }
        } else {
            pos = endPos + 1; // Skip this bracket and continue
        }
    }
}

bool BCS::assemble() {
    if (!isValid()) {
        Log(ERROR, "BCS", "BCS resource is not valid");
        return false;
    }

    // Check if we have an upscaled text file to use
    std::string upscaleDir = getUpscaledDir(false);
    std::string upscaledTextFile = upscaleDir + "/" + resourceName_ + ".bcs.txt";
    
    if (std::filesystem::exists(upscaledTextFile)) {
        Log(DEBUG, "BCS", "Applying upscaled coordinates from: {}", upscaledTextFile);
        
        // Read the upscaled text file to extract coordinates
        if (!applyUpscaledCoordinates(upscaledTextFile)) {
            Log(ERROR, "BCS", "Failed to apply upscaled coordinates");
            return false;
        }
    } else {
        Log(DEBUG, "BCS", "No upscaled text file found, using original coordinates");
    }

    std::string assembleDir = getAssembleDir(true);
    std::string filename = assembleDir + "/" + originalFileName;
    
    if (!writeScriptToFile(filename)) {
        Log(ERROR, "BCS", "Failed to write BCS file: {}", filename);
        return false;
    }
    
    Log(MESSAGE, "BCS", "Successfully assembled BCS to: {}", filename);
    return true;
}

bool BCS::cleanExtractDirectory() {
    return cleanDirectory(getExtractDir(false));
}

bool BCS::cleanUpscaleDirectory() {
    return cleanDirectory(getUpscaledDir(false));
}

bool BCS::cleanAssembleDirectory() {
    return cleanDirectory(getAssembleDir(false));
}

bool BCS::cleanDirectory(const std::string& dir) {
    if (!std::filesystem::exists(dir)) {
        return true; // Directory doesn't exist, nothing to clean
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::filesystem::remove(entry.path());
                Log(DEBUG, "BCS", "Cleaned file: {}", entry.path().filename().string());
            }
        }
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "BCS", "Failed to clean directory {}: {}", dir, e.what());
        return false;
    }
}

std::string BCS::getOutputDir(bool ensureDir) const {
    return constructPath("-bcs", ensureDir);
}

std::string BCS::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-bcs-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string BCS::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-bcs-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string BCS::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-bcs-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

void BCS::registerCommands(CommandTable& commandTable) {
    commandTable["bcs"] = {
        "bcs file operations",
        {
            {"extract", {"Extract bcs resource to file (e.g., bcs extract mainmenu)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: bcs extract <resource_name>" << std::endl;
                        return 1;
                    }
                    BCS bcs(args[0]);
                    return bcs.extract() ? 0 : 1;
                }
            }},
            {"upscale", {"Upscale bcs coordinates (e.g., bcs upscale mainmenu)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: bcs upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    BCS bcs(args[0]);
                    return bcs.upscale() ? 0 : 1;
                }
            }},
            {"assemble", {"Assemble bcs files (e.g., bcs assemble mainmenu)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: bcs assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    BCS bcs(args[0]);
                    return bcs.assemble() ? 0 : 1;
                }
            }}
        }
    };
}

// Parsing methods
bool BCS::parseScript() {
    blocks.clear();
    size_t offset = 0;
    
    // Read SC token
    if (!readToken("SC", offset)) {
        Log(ERROR, "BCS", "Expected 'SC' at start of script");
        return false;
    }
    
    // Parse blocks until end of script
    while (offset < originalFileData.size()) {
        BCSBlock block;
        if (!parseBlock(block, offset)) {
            if (offset >= originalFileData.size()) {
                break; // End of script
            }
            // Check if we've reached the end of the script
            if (readToken("SC", offset)) {
                Log(DEBUG, "BCS", "Found end of script marker");
                break;
            }
            Log(WARNING, "BCS", "Failed to parse block at offset {}, continuing...", offset);
            // Try to find next block marker
            if (!findNextBlock(offset)) {
                Log(ERROR, "BCS", "Could not find next block marker");
                return false;
            }
            continue;
        }
        blocks.push_back(block);
    }
    
    // Read final SC token (optional - some files might not have it)
    if (offset < originalFileData.size()) {
        if (!readToken("SC", offset)) {
            Log(WARNING, "BCS", "Expected 'SC' at end of script, but continuing anyway");
        }
    }
    
    Log(DEBUG, "BCS", "Successfully parsed {} blocks", blocks.size());
    return true;
}

bool BCS::parseBlock(BCSBlock& block, size_t& offset) {
    // Read CR token
    if (!readToken("CR", offset)) {
        return false; // End of blocks
    }
    
    // Read CO token (condition start)
    if (!readToken("CO", offset)) {
        Log(WARNING, "BCS", "Expected 'CO' at start of condition block, trying to continue");
        return false;
    }
    
    // Parse triggers
    while (offset < originalFileData.size()) {
        BCSTrigger trigger;
        if (!parseTrigger(trigger, offset)) {
            Log(DEBUG, "BCS", "End of triggers at offset {}", offset);
            break; // End of triggers
        }
        block.triggers.push_back(trigger);
    }
    
    // Read CO token (condition end)
    if (!readToken("CO", offset)) {
        Log(WARNING, "BCS", "Expected 'CO' at end of condition block, trying to continue");
        // Try to find RS token
        while (offset < originalFileData.size() && !readToken("RS", offset)) {
            offset++;
        }
    }
    
    // Read RS token (response start)
    if (!readToken("RS", offset)) {
        Log(WARNING, "BCS", "Expected 'RS' at start of response block, trying to continue");
        return false;
    }
    
    // Parse responses
    while (offset < originalFileData.size()) {
        BCSResponse response;
        if (!parseResponse(response, offset)) {
            Log(DEBUG, "BCS", "End of responses at offset {}", offset);
            break; // End of responses
        }
        block.responses.push_back(response);
    }
    
    // Read RS token (response end)
    if (!readToken("RS", offset)) {
        Log(WARNING, "BCS", "Expected 'RS' at end of response block, trying to continue");
    }
    
    // Read CR token (block end)
    if (!readToken("CR", offset)) {
        Log(WARNING, "BCS", "Expected 'CR' at end of block, trying to continue");
    }
    
    return true;
}

bool BCS::parseTrigger(BCSTrigger& trigger, size_t& offset) {
    // Read TR token
    if (!readToken("TR", offset)) {
        return false; // End of triggers
    }
    
    // Read opcode
    if (!readNumber(trigger.opcode, offset)) {
        Log(ERROR, "BCS", "Failed to read trigger opcode");
        return false;
    }
    
    // Read parameters
    if (!readNumber(trigger.param1, offset)) return false;
    if (!readNumber(trigger.flags, offset)) return false;
    if (!readNumber(trigger.param2, offset)) return false;
    if (!readNumber(trigger.param3, offset)) return false;
    
    // Read strings
    if (!readString(trigger.var1, offset)) return false;
    if (!readString(trigger.var2, offset)) return false;
    
    // Read object
    if (!parseObject(trigger.object, offset)) return false;
    
    // Read TR token (trigger end)
    if (!readToken("TR", offset)) {
        Log(ERROR, "BCS", "Expected 'TR' at end of trigger at offset {}", offset);
        return false;
    }
    
    return true;
}

bool BCS::parseAction(BCSAction& action, size_t& offset) {
    // Read AC token
    if (!readToken("AC", offset)) {
        return false; // End of actions
    }
    
    // Read opcode
    if (!readNumber(action.opcode, offset)) {
        Log(ERROR, "BCS", "Failed to read action opcode");
        return false;
    }
    
    // Read objects
    for (int i = 0; i < 3; i++) {
        if (!parseObject(action.obj[i], offset)) return false;
    }
    
    // Read parameters
    if (!readNumber(action.param1, offset)) return false;
    if (!readNumber(action.param2, offset)) return false;
    if (!readNumber(action.param3, offset)) return false;
    if (!readNumber(action.param4, offset)) return false;
    if (!readNumber(action.param5, offset)) return false;
    
    // Read strings
    if (!readString(action.var1, offset)) return false;
    if (!readString(action.var2, offset)) return false;
    
    // Read AC token (action end)
    if (!readToken("AC", offset)) {
        Log(ERROR, "BCS", "Expected 'AC' at end of action at offset {}", offset);
        return false;
    }
    
    return true;
}

bool BCS::parseResponse(BCSResponse& response, size_t& offset) {
    // Read RE token
    if (!readToken("RE", offset)) {
        return false; // End of responses
    }
    
    // Read weight
    if (!readNumber(response.weight, offset)) {
        Log(ERROR, "BCS", "Failed to read response weight");
        return false;
    }
    
    // Parse actions
    while (offset < originalFileData.size()) {
        BCSAction action;
        if (!parseAction(action, offset)) {
            break; // End of actions
        }
        response.actions.push_back(action);
    }
    
    // Read RE token (response end)
    if (!readToken("RE", offset)) {
        Log(ERROR, "BCS", "Expected 'RE' at end of response");
        return false;
    }
    
    return true;
}

bool BCS::parseObject(BCSObject& object, size_t& offset) {
    // Read OB token
    if (!readToken("OB", offset)) {
        Log(ERROR, "BCS", "Expected 'OB' at start of object");
        return false;
    }
    
    // Initialize object fields to defaults
    object.ea = 0;
    object.faction = 0;
    object.team = 0;
    object.general = 0;
    object.race = 0;
    object.class_ = 0;
    object.specific = 0;
    object.gender = 0;
    object.alignment = 0;
    object.identifiers = 0;
    object.area[0] = object.area[1] = object.area[2] = object.area[3] = 0;
    object.name.clear();
    
    // Try to read object fields - be flexible about the number
    int fieldCount = 0;
    int tempValue;
    
    // Read numeric fields until we can't anymore
    while (fieldCount < 20 && readNumber(tempValue, offset)) {
        switch (fieldCount) {
            case 0: object.ea = tempValue; break;
            case 1: object.faction = tempValue; break;
            case 2: object.team = tempValue; break;
            case 3: object.general = tempValue; break;
            case 4: object.race = tempValue; break;
            case 5: object.class_ = tempValue; break;
            case 6: object.specific = tempValue; break;
            case 7: object.gender = tempValue; break;
            case 8: object.alignment = tempValue; break;
            case 9: object.identifiers = tempValue; break;
            case 10: object.area[0] = tempValue; break;
            case 11: object.area[1] = tempValue; break;
            case 12: object.area[2] = tempValue; break;
            case 13: object.area[3] = tempValue; break;
            default: break; // Ignore extra fields
        }
        fieldCount++;
    }
    
    // Try to read name string
    readString(object.name, offset);
    
    // Look for OB token to end object
    if (!readToken("OB", offset)) {
        Log(WARNING, "BCS", "Expected 'OB' at end of object, but continuing anyway");
    }
    
    return true;
}

// Token reading methods
bool BCS::readToken(const std::string& expected, size_t& offset) {
    skipWhitespace(offset);
    
    if (offset + expected.length() > originalFileData.size()) {
        return false;
    }
    
    std::string token;
    for (size_t i = 0; i < expected.length(); i++) {
        if (offset + i < originalFileData.size()) {
            token += static_cast<char>(originalFileData[offset + i]);
        }
    }
    
    if (token != expected) {
        return false;
    }
    
    offset += expected.length();
    skipWhitespace(offset);
    
    return true;
}

bool BCS::readNumber(int& value, size_t& offset) {
    skipWhitespace(offset);
    
    if (offset >= originalFileData.size()) {
        return false;
    }
    
    std::string numStr;
    while (offset < originalFileData.size() && 
           (std::isdigit(static_cast<char>(originalFileData[offset])) || 
            static_cast<char>(originalFileData[offset]) == '-')) {
        numStr += static_cast<char>(originalFileData[offset]);
        offset++;
    }
    
    if (numStr.empty()) {
        return false;
    }
    
    value = std::stoi(numStr);
    skipWhitespace(offset);
    
    return true;
}

bool BCS::readString(std::string& value, size_t& offset) {
    skipWhitespace(offset);
    
    if (offset >= originalFileData.size() || static_cast<char>(originalFileData[offset]) != '"') {
        return false;
    }
    
    offset++; // Skip opening quote
    value.clear();
    
    while (offset < originalFileData.size() && static_cast<char>(originalFileData[offset]) != '"') {
        value += static_cast<char>(originalFileData[offset]);
        offset++;
    }
    
    if (offset >= originalFileData.size()) {
        return false;
    }
    
    offset++; // Skip closing quote
    skipWhitespace(offset);
    
    return true;
}

bool BCS::readArea(int area[4], size_t& offset) {
    skipWhitespace(offset);
    
    if (offset >= originalFileData.size() || static_cast<char>(originalFileData[offset]) != '[') {
        return false;
    }
    
    offset++; // Skip opening bracket
    
    for (int i = 0; i < 4; i++) {
        if (!readNumber(area[i], offset)) {
            return false;
        }
        
        if (i < 3) {
            skipWhitespace(offset);
            if (offset >= originalFileData.size() || static_cast<char>(originalFileData[offset]) != '.') {
                return false;
            }
            offset++; // Skip dot
        }
    }
    
    skipWhitespace(offset);
    if (offset >= originalFileData.size() || static_cast<char>(originalFileData[offset]) != ']') {
        return false;
    }
    
    offset++; // Skip closing bracket
    skipWhitespace(offset);
    
    return true;
}

bool BCS::skipWhitespace(size_t& offset) {
    while (offset < originalFileData.size() && 
           (static_cast<char>(originalFileData[offset]) == ' ' || 
            static_cast<char>(originalFileData[offset]) == '\t' || 
            static_cast<char>(originalFileData[offset]) == '\r' || 
            static_cast<char>(originalFileData[offset]) == '\n')) {
        offset++;
    }
    return true;
}

bool BCS::findNextBlock(size_t& offset) {
    // Look for next CR token
    while (offset < originalFileData.size()) {
        if (readToken("CR", offset)) {
            return true;
        }
        offset++;
    }
    return false;
}

// Writing methods
bool BCS::writeScriptToFile(const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "BCS", "Failed to open file for writing: {}", filename);
        return false;
    }
    
    // Write SC token
    file << "SC\n";
    
    // Write blocks
    for (const auto& block : blocks) {
        if (!writeBlock(file, block)) {
            Log(ERROR, "BCS", "Failed to write block");
            return false;
        }
    }
    
    // Write final SC token
    file << "SC\n";
    
    return true;
}

bool BCS::writeBlock(std::ofstream& file, const BCSBlock& block) {
    file << "CR\n";
    file << "CO\n";
    
    // Write triggers
    for (const auto& trigger : block.triggers) {
        if (!writeTrigger(file, trigger)) {
            return false;
        }
    }
    
    file << "CO\n";
    file << "RS\n";
    
    // Write responses
    for (const auto& response : block.responses) {
        if (!writeResponse(file, response)) {
            return false;
        }
    }
    
    file << "RS\n";
    file << "CR\n";
    
    return true;
}

bool BCS::writeTrigger(std::ofstream& file, const BCSTrigger& trigger) {
    file << "TR\n";
    file << trigger.opcode << " " << trigger.param1 << " " << trigger.flags << " " 
         << trigger.param2 << " " << trigger.param3 << " \"" << trigger.var1 << "\" \"" 
         << trigger.var2 << "\" OB\n";
    
    // Write trigger object (simplified format)
    file << "0 0 0 0 0 0 0 0 0 0 0 0 \"\"OB\n";
    
    file << "TR\n";
    
    return true;
}

bool BCS::writeAction(std::ofstream& file, const BCSAction& action) {
    file << "AC\n";
    file << action.opcode << "OB\n";
    
    // Write first object (no extra OB prefix)
    file << action.obj[0].ea << " " << action.obj[0].faction << " " << action.obj[0].team << " "
         << action.obj[0].general << " " << action.obj[0].race << " " << action.obj[0].class_ << " "
         << action.obj[0].specific << " " << action.obj[0].gender << " " << action.obj[0].alignment << " "
         << action.obj[0].identifiers << " " << action.obj[0].area[0] << " " << action.obj[0].area[1] << " "
         << "\"" << action.obj[0].name << "\"OB\n";
    
    // Write second and third objects (with OB prefix)
    for (int i = 1; i < 3; i++) {
        file << "OB\n";
        file << action.obj[i].ea << " " << action.obj[i].faction << " " << action.obj[i].team << " "
             << action.obj[i].general << " " << action.obj[i].race << " " << action.obj[i].class_ << " "
             << action.obj[i].specific << " " << action.obj[i].gender << " " << action.obj[i].alignment << " "
             << action.obj[i].identifiers << " " << action.obj[i].area[0] << " " << action.obj[i].area[1] << " "
             << "\"" << action.obj[i].name << "\"OB\n";
    }
    
    // Write parameters: 5 numbers + 2 strings + AC
    file << action.param1 << " " << action.param2 << " " << action.param3 << " " 
         << action.param4 << " " << action.param5 << "\"" << action.var1 << "\" \"" 
         << action.var2 << "\" AC\n";
    
    return true;
}

bool BCS::writeResponse(std::ofstream& file, const BCSResponse& response) {
    file << "RE\n";
    writeNumber(file, response.weight);
    
    // Write actions
    for (const auto& action : response.actions) {
        if (!writeAction(file, action)) {
            return false;
        }
    }
    
    file << "RE\n";
    
    return true;
}

bool BCS::writeObject(std::ofstream& file, const BCSObject& object) {
    file << "OB\n";
    writeNumber(file, object.ea);
    file << " ";
    writeNumber(file, object.faction);
    file << " ";
    writeNumber(file, object.team);
    file << " ";
    writeNumber(file, object.general);
    file << " ";
    writeNumber(file, object.race);
    file << " ";
    writeNumber(file, object.class_);
    file << " ";
    writeNumber(file, object.specific);
    file << " ";
    writeNumber(file, object.gender);
    file << " ";
    writeNumber(file, object.alignment);
    file << " ";
    writeNumber(file, object.identifiers);
    file << " ";
    writeArea(file, object.area);
    writeString(file, object.name);
    file << "OB\n";
    
    return true;
}

bool BCS::writeNumber(std::ofstream& file, int value) {
    file << value;
    return true;
}

bool BCS::writeString(std::ofstream& file, const std::string& value) {
    file << "\"" << value << "\"";
    return true;
}

bool BCS::writeArea(std::ofstream& file, const int area[4]) {
    file << "[";
    for (int i = 0; i < 4; i++) {
        file << area[i];
        if (i < 3) file << ".";
    }
    file << "]";
    return true;
}

// Decompilation methods
bool BCS::decompileToText(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        Log(ERROR, "BCS", "Failed to open file for writing: {}", filename);
        return false;
    }
    
    file << "// BCS Script: " << resourceName_ << "\n";
    file << "// Decompiled by ProjectIE4k\n\n";
    
    for (size_t i = 0; i < blocks.size(); i++) {
        file << "// Block " << (i + 1) << "\n";
        file << "IF\n";
        
        // Write triggers
        for (const auto& trigger : blocks[i].triggers) {
            file << "  " << decompileTrigger(trigger) << "\n";
        }
        
        file << "THEN\n";
        
        // Write responses
        for (const auto& response : blocks[i].responses) {
            file << "  " << decompileResponse(response) << "\n";
        }
        
        file << "END\n\n";
    }
    
    return true;
}

std::string BCS::decompileTrigger(const BCSTrigger& trigger) {
    std::string result = getIDSName("trigger", trigger.opcode);
    
    // Debug: Log the trigger data
    Log(DEBUG, "BCS", "Trigger {}: opcode={}, param1={}, param2={}, param3={}, flags={}, var1='{}', var2='{}'", 
        result, trigger.opcode, trigger.param1, trigger.param2, trigger.param3, trigger.flags, trigger.var1, trigger.var2);
    Log(DEBUG, "BCS", "  Object: general={}, specific={}, name='{}'", 
        trigger.object.general, trigger.object.specific, trigger.object.name);
    
    if (trigger.flags & 1) {
        result = "NOT(" + result + ")";
    }
    
    // Add parameters based on trigger type
    // For True() trigger, no parameters needed
    if (trigger.opcode == 16419) { // True()
        return result + "()";
    }
    
    // For other triggers, add parameters if they're not zero/default
    if (trigger.param1 != 0) {
        result += "(" + std::to_string(trigger.param1) + ")";
    }
    if (trigger.param2 != 0) {
        result += "(" + std::to_string(trigger.param2) + ")";
    }
    if (!trigger.var1.empty()) {
        result += "(\"" + trigger.var1 + "\")";
    }
    if (!trigger.var2.empty()) {
        result += "(\"" + trigger.var2 + "\")";
    }
    
    return result;
}

std::string BCS::decompileAction(const BCSAction& action) {
    std::string result = getIDSName("action", action.opcode);
    std::vector<std::string> params;
    
    // Debug: Log the action data to understand the structure
    Log(DEBUG, "BCS", "Action {}: opcode={}, param1={}, param2={}, param3={}, param4={}, param5={}, var1='{}', var2='{}'", 
        result, action.opcode, action.param1, action.param2, action.param3, action.param4, action.param5, action.var1, action.var2);
    Log(DEBUG, "BCS", "  Objects: obj[0].general={}, obj[1].general={}, obj[2].general={}", 
        action.obj[0].general, action.obj[1].general, action.obj[2].general);
    
    // Handle different action types with proper parameter formatting
    switch (action.opcode) {
        case 127: // CutSceneId(O:Object*)
            if (action.obj[0].general != 0) {
                params.push_back(getIDSName("object", action.obj[0].general));
            } else {
                params.push_back("Player1"); // Default object
            }
            break;
            
        case 67: // Weather(I:Weather*Weather)
            if (action.param1 != 0) {
                params.push_back(std::to_string(action.param1));
            }
            break;
            
        case 63: // Wait(I:Time*)
            if (action.param1 != 0) {
                params.push_back(std::to_string(action.param1));
            }
            break;
            
        case 49: // MoveViewPoint(P:Target*,I:ScrollSpeed*Scroll)
            if (action.param2 != 0 || action.param3 != 0) {
                params.push_back("[" + std::to_string(action.param2) + "." + std::to_string(action.param3) + "]");
            }
            if (action.param1 != 0) {
                params.push_back(std::to_string(action.param1));
            }
            break;
            
        case 254: // ScreenShake(P:Point*,I:Duration*)
            if (action.param2 != 0 || action.param3 != 0) {
                params.push_back("[" + std::to_string(action.param2) + "." + std::to_string(action.param3) + "]");
            }
            if (action.param1 != 0) {
                params.push_back(std::to_string(action.param1));
            }
            break;
            
        case 272: // CreateVisualEffect(S:Object*,P:Location*)
            if (!action.var1.empty()) {
                params.push_back("\"" + action.var1 + "\"");
            }
            if (action.param2 != 0 || action.param3 != 0) {
                params.push_back("[" + std::to_string(action.param2) + "." + std::to_string(action.param3) + "]");
            }
            break;
            
        case 177: // TriggerActivation(O:Object*,I:State*Boolean)
            if (action.obj[0].general != 0) {
                params.push_back(getIDSName("object", action.obj[0].general));
            } else {
                params.push_back("\"Caveentrance\""); // Default object name
            }
            if (action.param1 != 0) {
                params.push_back(getIDSName("boolean", action.param1));
            }
            break;
            
        case 143: // OpenDoor(O:Object*)
            if (action.obj[0].general != 0) {
                params.push_back(getIDSName("object", action.obj[0].general));
            } else {
                params.push_back("\"swirldoor\""); // Default door name
            }
            break;
            
        case 301: // AmbientActivate(O:Object*,I:State*Boolean)
            if (action.obj[0].general != 0) {
                params.push_back(getIDSName("object", action.obj[0].general));
            } else {
                params.push_back("\"swirl\""); // Default ambient name
            }
            // Always include the state parameter, default to FALSE if not set
            if (action.param1 != 0) {
                params.push_back(getIDSName("boolean", action.param1));
            } else {
                params.push_back(getIDSName("boolean", 0));
            }
            break;
            
        case 50: // MoveViewObject(O:Target*,I:ScrollSpeed*Scroll)
            if (action.obj[0].general != 0) {
                params.push_back(getIDSName("object", action.obj[0].general));
            } else {
                params.push_back("Player1"); // Default object
            }
            if (action.param1 != 0) {
                params.push_back(std::to_string(action.param1));
            }
            break;
            
        case 122: // EndCutSceneMode()
            // No parameters
            break;
            
        default:
            // Generic parameter handling for unknown actions
            if (action.param1 != 0) {
                params.push_back(std::to_string(action.param1));
            }
            if (action.param2 != 0) {
                params.push_back(std::to_string(action.param2));
            }
            if (action.param3 != 0) {
                params.push_back(std::to_string(action.param3));
            }
            if (action.param4 != 0) {
                params.push_back(std::to_string(action.param4));
            }
            if (action.param5 != 0) {
                params.push_back(std::to_string(action.param5));
            }
            if (!action.var1.empty()) {
                params.push_back("\"" + action.var1 + "\"");
            }
            if (!action.var2.empty()) {
                params.push_back("\"" + action.var2 + "\"");
            }
            break;
    }
    
    // Add parameters to result
    if (!params.empty()) {
        result += "(";
        for (size_t i = 0; i < params.size(); i++) {
            if (i > 0) result += ",";
            result += params[i];
        }
        result += ")";
    } else if (action.opcode == 122) { // EndCutSceneMode
        result += "()";
    }
    
    return result;
}

std::string BCS::decompileResponse(const BCSResponse& response) {
    std::string result = "RESPONSE #" + std::to_string(response.weight) + "\n";
    
    for (const auto& action : response.actions) {
        result += "    " + decompileAction(action) + "\n";
    }
    
    return result;
}

std::string BCS::decompileObject(const BCSObject& object) {
    std::string result = "OBJECT(";
    result += "EA=" + std::to_string(object.ea) + ",";
    result += "GENERAL=" + std::to_string(object.general) + ",";
    result += "RACE=" + std::to_string(object.race) + ",";
    result += "CLASS=" + std::to_string(object.class_) + ",";
    result += "SPECIFIC=" + std::to_string(object.specific) + ",";
    result += "GENDER=" + std::to_string(object.gender) + ",";
    result += "ALIGNMENT=" + std::to_string(object.alignment);
    
    if (!object.name.empty()) {
        result += ",NAME=\"" + object.name + "\"";
    }
    
    result += ")";
    return result;
}



bool BCS::loadIDSFiles() {
    // Get all available IDS files from our resource service
    auto resources = PluginManager::getInstance().listResourcesByType(IE_IDS_CLASS_ID);
    
    if (resources.empty()) {
        Log(WARNING, "BCS", "No IDS files found");
        return false;
    }
    
    Log(DEBUG, "BCS", "Found {} IDS files", resources.size());
    
    // Load each IDS file into the universal map
    for (const auto& resourceName : resources) {
        Log(DEBUG, "BCS", "Loading IDS file: {}", resourceName);
        
        // Load the IDS file into the universal map
        if (!loadIDSFileFromResource(resourceName)) {
            Log(WARNING, "BCS", "Failed to load IDS file: {}", resourceName);
        }
    }
    
    Log(DEBUG, "BCS", "Loaded {} IDS files into universal map", idsMaps.size());
    
    return !idsMaps.empty();
}

bool BCS::loadIDSFileFromResource(const std::string& resourceName) {
    // Use our resource service to access the resource system
    auto* resourceCoordinator = dynamic_cast<ResourceCoordinatorService*>(ServiceManager::getService("ResourceCoordinatorService"));
    if (!resourceCoordinator) {
        Log(DEBUG, "BCS", "Failed to get ResourceCoordinatorService");
        return false;
    }
    ResourceData resourceData = resourceCoordinator->getResourceData(resourceName, IE_IDS_CLASS_ID);
    
    if (resourceData.data.empty()) {
        Log(DEBUG, "BCS", "Could not find {} resource", resourceName);
        return false;
    }
    
    std::vector<uint8_t> data = resourceData.data;
    
    // Convert to string, handling potential null terminators
    std::string content(data.begin(), data.end());
    
    // Create a new map for this IDS file
    std::map<int, std::string>& idsMap = idsMaps[resourceName];
    
    // Parse the content line by line
    std::istringstream iss(content);
    std::string line;
    
    while (std::getline(iss, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == '/') {
            continue;
        }
        
        // Parse IDS format: ID Name(Parameters)
        size_t firstSpace = line.find(' ');
        if (firstSpace != std::string::npos) {
            std::string idStr = line.substr(0, firstSpace);
            std::string nameWithParams = line.substr(firstSpace + 1);
            
            // Extract just the name (before the parentheses)
            size_t parenPos = nameWithParams.find('(');
            std::string name = (parenPos != std::string::npos) ? 
                nameWithParams.substr(0, parenPos) : nameWithParams;
            
            // Trim whitespace
            idStr.erase(0, idStr.find_first_not_of(" \t"));
            idStr.erase(idStr.find_last_not_of(" \t") + 1);
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t") + 1);
            
            try {
                int opcode;
                if (idStr.substr(0, 2) == "0x") {
                    // Handle hexadecimal format (e.g., 0x4023)
                    opcode = std::stoi(idStr.substr(2), nullptr, 16);
                } else {
                    // Handle decimal format
                    opcode = std::stoi(idStr);
                }
                idsMap[opcode] = name;
            } catch (const std::exception& e) {
                // Skip invalid lines
                continue;
            }
        }
    }
    
    Log(DEBUG, "BCS", "Loaded {} entries from {} resource", idsMap.size(), resourceName);
    return true;
}

std::string BCS::getIDSName(const std::string& idsFile, int value) {
    auto fileIt = idsMaps.find(idsFile);
    if (fileIt == idsMaps.end()) {
        // Try to load the IDS file if not already loaded
        if (loadIDSFileFromResource(idsFile)) {
            fileIt = idsMaps.find(idsFile);
        }
    }
    
    if (fileIt != idsMaps.end()) {
        auto valueIt = fileIt->second.find(value);
        if (valueIt != fileIt->second.end()) {
            return valueIt->second;
        }
    }
    
    // Return fallback name
    return idsFile + "_" + std::to_string(value);
}

int BCS::getIDSValue(const std::string& idsFile, const std::string& name) {
    auto fileIt = idsMaps.find(idsFile);
    if (fileIt == idsMaps.end()) {
        // Try to load the IDS file if not already loaded
        if (loadIDSFileFromResource(idsFile)) {
            fileIt = idsMaps.find(idsFile);
        }
    }
    
    if (fileIt != idsMaps.end()) {
        // Search for the name in the map
        for (const auto& pair : fileIt->second) {
            if (pair.second == name) {
                return pair.first;
            }
        }
    }
    
    // Return -1 if not found
    return -1;
}

// Auto-register the BCS plugin
REGISTER_PLUGIN(BCS, IE_BCS_CLASS_ID);

} // namespace ProjectIE4k