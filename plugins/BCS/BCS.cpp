#include "BCS.h"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <cctype>

#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include "plugins/CommandRegistry.h"
#include "IdsMapCache.h"

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
    
    // Cleanup handled by smart pointers
}

bool BCS::loadFromData() {
    // Some BCS files are actually empty
    // if (originalFileData.empty()) {
    //     Log(ERROR, "BCS", "No BCS data loaded");
    //     return false;
    // }

    // Create decompiler but don't initialize it yet (lazy initialization)
    decompiler_ = std::make_unique<BcsDecompiler>();


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

    // Save decompiled BAF file
    std::string bafFilename = extractDir + "/" + resourceName_ + ".baf";
    if (!decompileToText(bafFilename)) {
        Log(ERROR, "BCS", "Failed to decompile BCS to text file: {}", bafFilename);
        return false;
    }

    // Save original BCS data as .bcs file
    std::string bcsFilename = extractDir + "/" + originalFileName;
    std::ofstream bcsFile(bcsFilename, std::ios::binary);
    if (!bcsFile) {
        Log(ERROR, "BCS", "Failed to open BCS file for writing: {}", bcsFilename);
        return false;
    }

    if (!originalFileData.empty()) {
        bcsFile.write(reinterpret_cast<const char*>(originalFileData.data()), originalFileData.size());
        if (!bcsFile) {
            Log(ERROR, "BCS", "Failed to write BCS data to file: {}", bcsFilename);
            return false;
        }
    } else {
        Log(WARNING, "BCS", "No original BCS data available to save");
    }
    bcsFile.close();

    Log(MESSAGE, "BCS", "Successfully extracted BCS to: {} and {}", bafFilename, bcsFilename);
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

    // Ensure decompiler is initialized before upscaling
    if (!ensureDecompilerInitialized()) {
        Log(ERROR, "BCS", "Failed to initialize decompiler for upscaling");
        return false;
    }
    
    // Create upscale-enabled decompiler and decompile directly from original binary data
    auto upscaleDecompiler = std::make_unique<BcsDecompiler>();
    
    if (!upscaleDecompiler->initialize()) {
        Log(ERROR, "BCS", "Failed to initialize upscale decompiler");
        return false;
    }
    
    // Enable upscaling mode
    upscaleDecompiler->setUpscaling(true, upscaleFactor);

    // Decompile directly to upscaled output using the original binary data
    std::string upscaleDir = getUpscaledDir(true);
    std::string outputFile = upscaleDir + "/" + resourceName_ + ".baf";
    
    std::ofstream file(outputFile);
    if (!file.is_open()) {
        Log(ERROR, "BCS", "Failed to open output file for upscaled BCS: {}", outputFile);
        return false;
    }
    
    
    for (size_t i = 0; i < blocks.size(); i++) {
        file << "IF\n";
        
        // Write triggers with upscaling
        for (const auto& trigger : blocks[i].triggers) {
            file << "  " << upscaleDecompiler->decompileTrigger(trigger) << "\n";
        }
        
        file << "THEN\n";
        
        // Write responses with upscaling
        for (const auto& response : blocks[i].responses) {
            file << "  RESPONSE #" << response.weight << "\n";
            for (const auto& action : response.actions) {
                file << "    " << upscaleDecompiler->decompileAction(action) << "\n";
            }
            file << "\n";
        }
        
        file << "END\n\n";
    }
    
    file.close();

    Log(MESSAGE, "BCS", "Successfully upscaled BCS to: {}", outputFile);
    
    // Log upscaled file for tracking
    Log(DEBUG, "BCS", "UPSCALED FILE: {} -> {}", resourceName_, outputFile);
    
    return true;
}

bool BCS::assemble() {
    if (!isValid()) {
        Log(ERROR, "BCS", "BCS resource is not valid");
        return false;
    }

    // Read the upscaled BCS text file
    std::string upscaledDir = getUpscaledDir(false);
    std::string inputFile = upscaledDir + "/" + resourceName_ + ".baf";
    
    if (!std::filesystem::exists(inputFile)) {
        Log(ERROR, "BCS", "Upscaled BCS file not found: {}", inputFile);
        return false;
    }
    
    // Ensure decompiler is initialized (needed for compilation)
    if (!ensureDecompilerInitialized()) {
        Log(ERROR, "BCS", "Failed to initialize decompiler for assembly");
        return false;
    }
    
    // Read the text file content
    std::ifstream file(inputFile);
    if (!file.is_open()) {
        Log(ERROR, "BCS", "Failed to open upscaled BCS file: {}", inputFile);
        return false;
    }
    
    std::string textContent;
    std::string line;
    while (std::getline(file, line)) {
        textContent += line + "\n";
    }
    file.close();
    
    // Compile the text content to binary
    if (!compileTextToBinary(textContent)) {
        Log(ERROR, "BCS", "Failed to compile BCS text to binary");
        return false;
    }
    
    // Write compiled binary to assembled directory
    std::string assembleDir = getAssembleDir(true);
    std::string outputFile = assembleDir + "/" + originalFileName;
    
    if (!writeScriptToFile(outputFile)) {
        Log(ERROR, "BCS", "Failed to write compiled BCS to file: {}", outputFile);
        return false;
    }
    
    Log(MESSAGE, "BCS", "Successfully compiled and assembled BCS to: {}", outputFile);
    
    return true;
}

bool BCS::compileTextToBinary(const std::string& textContent) {
    // Ensure compiler is initialized
    if (!ensureCompilerInitialized()) {
        Log(ERROR, "BCS", "Failed to initialize compiler");
        return false;
    }
    
    // Clear existing blocks before compilation
    blocks.clear();
    
    // Use the BCSCompiler to compile text to binary blocks
    return compiler_->compileText(textContent, blocks);
}

bool BCS::ensureCompilerInitialized() {
    if (!compiler_) {
        compiler_ = std::make_unique<BCSCompiler>();
    }
    
    if (!compilerInitialized_) {
        if (compiler_->initialize()) {
            compilerInitialized_ = true;
            Log(DEBUG, "BCS", "Compiler initialized successfully");
        } else {
            Log(ERROR, "BCS", "Failed to initialize compiler");
            return false;
        }
    }
    
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

bool BCS::loadIDSFiles() {
    // Get all available IDS files from our resource service
    auto resources = PluginManager::getInstance().listResourcesByType(IE_IDS_CLASS_ID);
    
    if (resources.empty()) {
        Log(WARNING, "BCS", "No IDS files found");
        return false;
    }
    
    Log(DEBUG, "BCS", "Found {} IDS files", resources.size());
    
    // Load each IDS file into a map
    std::map<std::string, std::vector<uint8_t>> idsFiles;
    for (const auto& resourceName : resources) {
        Log(DEBUG, "BCS", "Loading IDS file: {}", resourceName);
        
        auto data = loadResourceFromService(resourceName, IE_IDS_CLASS_ID);
        if (!data.empty()) {
            // Store by bare resource name; normalizeName() will add .IDS
            idsFiles[resourceName] = data;
        } else {
            Log(WARNING, "BCS", "Failed to load IDS file: {}", resourceName);
        }
    }
    
    // Initialize the IDS cache with all loaded files
    IdsMapCache::initializeWithIdsFiles(idsFiles);
    
    Log(DEBUG, "BCS", "Loaded {} IDS files into cache", idsFiles.size());
    
    return !idsFiles.empty();
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
                    return PluginManager::getInstance().extractResource(args[0], IE_BCS_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {"Upscale bcs coordinates (e.g., bcs upscale mainmenu)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: bcs upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().upscaleResource(args[0], IE_BCS_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {"Assemble bcs files (e.g., bcs assemble mainmenu)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: bcs assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().assembleResource(args[0], IE_BCS_CLASS_ID) ? 0 : 1;
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
    
    // Parse triggers (tolerant scanning like Near Infinity)
    while (offset < originalFileData.size()) {
        // Stop when we encounter the end of condition block
        size_t save = offset;
        if (readToken("CO", save)) {
            // Reached end of triggers; do not consume here
            break;
        }
        // Try to parse a trigger at current offset
        BCSTrigger trigger;
        size_t before = offset;
        if (parseTrigger(trigger, offset)) {
            block.triggers.push_back(trigger);
            continue;
        }
        // No trigger here; advance by one byte and continue scanning
        offset = before + 1;
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
    
    // Parse responses (tolerant scanning like Near Infinity)
    while (offset < originalFileData.size()) {
        // Stop when we encounter the end of response set
        size_t save = offset;
        if (readToken("RS", save)) {
            // Reached end of responses; do not consume here
            break;
        }
        // Try to parse a response at current offset
        BCSResponse response;
        size_t before = offset;
        if (parseResponse(response, offset)) {
            block.responses.push_back(response);
            continue;
        }
        // No response here; advance by one byte and continue scanning
        offset = before + 1;
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

    // Near Infinity-style tolerant parsing: consume params until closing TR
    int cntNums = 0;
    int cntStrings = 0;
    bool haveObject = false;

    while (offset < originalFileData.size()) {
        // Check for end of trigger (peek without consuming)
        size_t save = offset;
        if (readToken("TR", save)) {
            offset = save; // consume end token
            break;
        }

        // Determine next param type
        char ch = 0;
        unsigned char c = static_cast<unsigned char>(originalFileData[offset]);
        if (c == 'O') {
            // Possible OB token
            size_t t = offset;
            if (readToken("OB", t)) {
                ch = 'O';
            }
        }
        if (ch == 0) {
            if (c == '"') ch = 'S';
            else if (c == '-' || (c >= '0' && c <= '9')) ch = 'I';
        }

        switch (ch) {
            case 'I': {
                int n;
                if (!readNumber(n, offset)) return false;
                switch (cntNums) {
                    case 0: trigger.opcode = n; break;
                    case 1: trigger.param1 = n; break;
                    case 2: trigger.flags = n; break;
                    case 3: trigger.param2 = n; break;
                    case 4: trigger.param3 = n; break;
                    default: break;
                }
                cntNums++;
                break;
            }
            case 'S': {
                std::string s;
                if (!readString(s, offset)) return false;
                if (cntStrings == 0) trigger.var1 = s;
                else if (cntStrings == 1) trigger.var2 = s;
                cntStrings++;
                break;
            }
            case 'O': {
                if (!haveObject) {
                    if (!parseObject(trigger.object, offset)) return false;
                    haveObject = true;
                } else {
                    // Extra object, parse and ignore
                    BCSObject dummy;
                    if (!parseObject(dummy, offset)) return false;
                }
                break;
            }
            default:
                // Unknown token; advance one byte
                offset++;
                break;
        }
    }

    return true;
}

bool BCS::parseAction(BCSAction& action, size_t& offset) {
    // Read AC token
    if (!readToken("AC", offset)) {
        return false; // End of actions
    }

    // Near Infinity-style tolerant parsing: consume params until closing AC
    int cntNums = 0;
    int cntStrings = 0;
    int cntObjects = 0;

    while (offset < originalFileData.size()) {
        // Check for end of action (peek without consuming)
        size_t save = offset;
        if (readToken("AC", save)) {
            offset = save; // consume end token
            break;
        }

        // Determine next param type
        char ch = 0;
        unsigned char c = static_cast<unsigned char>(originalFileData[offset]);
        if (c == 'O') {
            size_t t = offset;
            if (readToken("OB", t)) {
                ch = 'O';
            }
        }
        if (ch == 0) {
            if (c == '"') ch = 'S';
            else if (c == '[') ch = 'P';
            else if (c == '-' || (c >= '0' && c <= '9')) ch = 'I';
        }

        switch (ch) {
            case 'I': {
                int n;
                if (!readNumber(n, offset)) return false;
                switch (cntNums) {
                    case 0: action.opcode = n; break;     // id
                    case 1: action.param1 = n; break;     // a4 (first integer)
                    case 2: action.param2 = n; break;     // a5.x (point X)
                    case 3: action.param3 = n; break;     // a5.y (point Y)  
                    case 4: action.param4 = n; break;     // a6 (second integer)
                    case 5: action.param5 = n; break;     // a7 (third integer)
                    default: break;
                }
                cntNums++;
                break;
            }
            case 'S': {
                std::string s;
                if (!readString(s, offset)) return false;
                if (cntStrings == 0) action.var1 = s;
                else if (cntStrings == 1) action.var2 = s;
                cntStrings++;
                break;
            }
            case 'O': {
                if (cntObjects < 3) {
                    if (!parseObject(action.obj[cntObjects], offset)) {
                        return false;
                    }
                    cntObjects++;
                } else {
                    // Skip extra object silently
                    BCSObject dummy;
                    if (!parseObject(dummy, offset)) return false;
                }
                break;
            }
            case 'P': {
                // Points are stored as separate integers, not [x.y] format
                // This case should not occur in binary parsing - points are parsed as integers
                Log(ERROR, "BCS", "Unexpected point format in binary at offset {}", offset);
                return false;
            }
            default:
                // Unknown token; advance one byte
                offset++;
                break;
        }
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

    // Parse actions until closing RE
    while (offset < originalFileData.size()) {
        size_t save = offset;
        if (readToken("RE", save)) {
            offset = save; // consume
            break;
        }
        BCSAction action;
        if (parseAction(action, offset)) {
            response.actions.push_back(action);
        } else {
            // Advance by one byte and keep scanning
            offset++;
        }
    }

    return true;
}

bool BCS::parseObject(BCSObject& object, size_t& offset) {
    // Read OB token
    if (!readToken("OB", offset)) {
        Log(ERROR, "BCS", "Expected 'OB' at start of object");
        return false;
    }
    
    // Initialize object fields to defaults (Near Infinity approach)
    object.ea = 0;
    object.faction = 0;
    object.team = 0;
    object.general = 0;
    object.race = 0;
    object.class_ = 0;
    object.specific = 0;
    object.gender = 0;
    object.alignment = 0;
    for (int i = 0; i < 5; i++) object.identifiers[i] = 0;
    object.area[0] = object.area[1] = object.area[2] = object.area[3] = 0;
    object.name.clear();
    
    // Near Infinity approach: read parameters until closing OB
    int cntNums = 0;
    
    while (offset < originalFileData.size()) {
        // Check for end of object (peek without consuming)
        size_t save = offset;
        if (readToken("OB", save)) {
            offset = save; // consume end token
            break;
        }
        
        // Determine parameter type
        char ch = 0;
        unsigned char c = static_cast<unsigned char>(originalFileData[offset]);
        if (c == '"') {
            ch = 'S';
        } else if (c == '[') {
            ch = 'P'; // rectangle
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            ch = 'I';
        } else {
            // Unknown character, advance and continue
            offset++;
            continue;
        }
        
        switch (ch) {
            case 'I': {
                int n;
                if (readNumber(n, offset)) {
                    // Near Infinity BG format: T0:T3:T4:T5:T6:T7:T8:I0:I1:I2:I3:I4
                    // BG skips T1 (faction) and T2 (team), so we map accordingly
                    switch (cntNums) {
                        case 0: object.ea = n; break;        // T0 (EA)
                        case 1: object.general = n; break;   // T3 (General)
                        case 2: object.race = n; break;      // T4 (Race)
                        case 3: object.class_ = n; break;    // T5 (Class)
                        case 4: object.specific = n; break;  // T6 (Specific)
                        case 5: object.gender = n; break;    // T7 (Gender)
                        case 6: object.alignment = n; break; // T8 (Alignment)
                        case 7: case 8: case 9: case 10: case 11: // I0-I4 (Identifiers)
                            if (cntNums - 7 < 5) {
                                object.identifiers[cntNums - 7] = n;
                            }
                            break;
                    }
                    cntNums++;
                }
                break;
            }
            case 'S': {
                std::string s;
                if (readString(s, offset)) {
                    object.name = s;
                }
                break;
            }
            case 'P': {
                int area[4];
                if (readArea(area, offset)) {
                    for (int i = 0; i < 4; i++) {
                        object.area[i] = area[i];
                    }
                }
                break;
            }
        }
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
    
    // For now, use the existing text-based format to avoid compilation issues
    // TODO: Implement proper binary format using BCSCompiler bytecode generation
    
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
    Log(DEBUG, "BCS", "writeTrigger called for opcode={}, param1={}", trigger.opcode, trigger.param1);

    file << "TR\n";

    // Follow Near Infinity's actual implementation exactly
    file << trigger.opcode << " "
         << trigger.param1 << " "
         << trigger.flags << " "
         << trigger.param2 << " "
         << trigger.param3 << " \"" << trigger.var1 << "\" \""
         << trigger.var2 << "\" OB\n";
    

    Log(DEBUG, "BCS", "Writing trigger opcode={}: ea={}, general={}", trigger.opcode, trigger.object.ea, trigger.object.general);

    // Write trigger object content following game-specific parse code without leading OB token line
    std::string gameType = PIE4K_CFG.GameType;

    if (gameType == "pst") {
        // PST: "T0:T1:T2:T3:T4:T5:T6:T7:T8:I0:I1:I2:I3:I4:R0:S0"
        file << trigger.object.ea << " " << trigger.object.faction << " " << trigger.object.team << " "
             << trigger.object.general << " " << trigger.object.race << " " << trigger.object.class_ << " "
             << trigger.object.specific << " " << trigger.object.gender << " " << trigger.object.alignment << " ";
        for (int j = 0; j < 5; j++) file << trigger.object.identifiers[j] << " ";
        file << "[0,0,0,0] "; // rectangle (region) - placeholder
        file << "\"" << trigger.object.name << "\"OB\n";
    } else if (gameType == "iwd") {
        // IWD: "T0:T3:T4:T5:T6:T7:T8:I0:I1:I2:I3:I4:R0:S0"
        file << trigger.object.ea << " " << trigger.object.general << " " << trigger.object.race << " "
             << trigger.object.class_ << " " << trigger.object.specific << " " << trigger.object.gender << " "
             << trigger.object.alignment << " ";
        for (int j = 0; j < 5; j++) file << trigger.object.identifiers[j] << " ";
        file << "[0,0,0,0] "; // rectangle (region) - placeholder
        file << "\"" << trigger.object.name << "\"OB\n";
    } else if (gameType == "iwd2") {
        // IWD2: "T0:T3:T4:T5:T6:T7:T8:T9:I0:I1:I2:I3:I4:R0:S0:TA:TB"
        file << trigger.object.ea << " " << trigger.object.general << " " << trigger.object.race << " "
             << trigger.object.class_ << " " << trigger.object.specific << " " << trigger.object.gender << " "
             << trigger.object.alignment << " " << trigger.object.subrace << " ";
        for (int j = 0; j < 5; j++) file << trigger.object.identifiers[j] << " ";
        file << "[0,0,0,0] "; // rectangle (region) - placeholder
        file << "\"" << trigger.object.name << "\" 0 0"; // extra TA:TB parameters
        file << "OB\n";
    } else {
        // BG (default): "T0:T3:T4:T5:T6:T7:T8:I0:I1:I2:I3:I4:S0"
        file << trigger.object.ea << " " << trigger.object.general << " " << trigger.object.race << " "
             << trigger.object.class_ << " " << trigger.object.specific << " " << trigger.object.gender << " "
             << trigger.object.alignment << " ";
        for (int j = 0; j < 5; j++) file << trigger.object.identifiers[j] << " ";
        file << "\"" << trigger.object.name << "\"OB\n";
    }

    file << "TR\n";
    
    return true;
}

bool BCS::writeAction(std::ofstream& file, const BCSAction& action) {
    file << "AC\n";
    file << action.opcode;

    auto writeObjLine = [&](const BCSObject& o, bool isFirst = false) {
        if (isFirst) {
            file << "OB\n";  // First object gets OB on same line as opcode
        } else {
            file << "OB\n";  // Subsequent objects get OB on new line
        }
        // Game-specific object format based on Near Infinity parse codes
        std::string gameType = PIE4K_CFG.GameType;

        if (gameType == "pst") {
            // PST: "T0:T1:T2:T3:T4:T5:T6:T7:T8:I0:I1:I2:I3:I4:R0:S0" (14 values + rectangle + string)
            file << o.ea << " " << o.faction << " " << o.team << " "
                 << o.general << " " << o.race << " " << o.class_ << " "
                 << o.specific << " " << o.gender << " " << o.alignment << " ";
            for (int j = 0; j < 5; j++) file << o.identifiers[j] << " ";
            file << "[0,0,0,0] "; // rectangle (region) - placeholder for now
            file << "\"" << o.name << "\"";
        } else if (gameType == "iwd") {
            // IWD: "T0:T3:T4:T5:T6:T7:T8:I0:I1:I2:I3:I4:R0:S0" (13 values + rectangle + string)
            file << o.ea << " " << o.general << " " << o.race << " "
                 << o.class_ << " " << o.specific << " " << o.gender << " "
                 << o.alignment << " ";
            for (int j = 0; j < 5; j++) file << o.identifiers[j] << " ";
            file << "[0,0,0,0] "; // rectangle (region) - placeholder for now
            file << "\"" << o.name << "\"";
        } else if (gameType == "iwd2") {
            // IWD2: "T0:T3:T4:T5:T6:T7:T8:T9:I0:I1:I2:I3:I4:R0:S0:TA:TB" (16 values + rectangle + string + 2 extra)
            file << o.ea << " " << o.general << " " << o.race << " "
                 << o.class_ << " " << o.specific << " " << o.gender << " "
                 << o.alignment << " " << o.subrace << " ";
            for (int j = 0; j < 5; j++) file << o.identifiers[j] << " ";
            file << "[0,0,0,0] "; // rectangle (region) - placeholder for now
            file << "\"" << o.name << "\" 0 0"; // extra TA:TB parameters
        } else {
            // BG (default): "T0:T3:T4:T5:T6:T7:T8:I0:I1:I2:I3:I4:S0" (12 values + string)
            file << o.ea << " " << o.general << " " << o.race << " "
                 << o.class_ << " " << o.specific << " " << o.gender << " "
                 << o.alignment << " ";
            for (int j = 0; j < 5; j++) file << o.identifiers[j] << " ";
            file << "\"" << o.name << "\"";
        }

        Log(DEBUG, "BCS", "Writing object for game '{}': ea={}, general={}, name='{}'", gameType, o.ea, o.general, o.name);
        file << "OB\n";
    };

    // Near Infinity writes objects in order: a1, a2, a3 (corresponding to obj[0], obj[1], obj[2])
    writeObjLine(action.obj[0], true);   // a1 - first object written (OB on same line as opcode)
    writeObjLine(action.obj[1], false);  // a2 - second object written (OB on new line)
    writeObjLine(action.obj[2], false);  // a3 - third object written (OB on new line)

    // Write parameters in Near Infinity order: a4, a5.x, a5.y, a6, a7 + 2 strings + AC
    file << action.param1 << " " << action.param2 << " " << action.param3 << " "
         << action.param4 << " " << action.param5;
    // Write string parameters - always write them if present, or as empty strings for specific cases
    if (!action.var1.empty() || !action.var2.empty()) {
        file << "\"" << action.var1 << "\" \"" << action.var2 << "\"";
    } else {
        // For actions that should have empty string parameters, write them as empty strings
        // This handles cases like "2 0 0 0 0\"\" \"\" AC"
        file << "\"\" \"\"";
    }
    file << " AC\n";

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
    // Write following BG parse code: T0:T1:T2:T3:T4:T5:T6:T7:T8:I0:I1:I2:I3:I4:S0
    // Near Infinity includes faction/team even for BG (contrary to comments)
    writeNumber(file, object.ea);        // T0
    file << " ";
    writeNumber(file, object.faction);   // T1
    file << " ";
    writeNumber(file, object.team);      // T2
    file << " ";
    writeNumber(file, object.general);   // T3
    file << " ";
    writeNumber(file, object.race);      // T4
    file << " ";
    writeNumber(file, object.class_);    // T5
    file << " ";
    writeNumber(file, object.specific);  // T6
    file << " ";
    writeNumber(file, object.gender);    // T7
    file << " ";
    writeNumber(file, object.alignment); // T8
    file << " ";
    // Write all identifier values (I0-I4)
    for (int i = 0; i < 5; i++) {
        writeNumber(file, object.identifiers[i]);
        file << " ";
    }
    writeString(file, object.name);      // S0
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
    // Ensure decompiler is initialized before use
    if (!ensureDecompilerInitialized()) {
        Log(ERROR, "BCS", "Failed to initialize decompiler for text output");
        return false;
    }
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        Log(ERROR, "BCS", "Failed to open file for writing: {}", filename);
        return false;
    }
    
    for (size_t i = 0; i < blocks.size(); i++) {
        file << "IF\n";
        
        // Write triggers with logical grouping support
        size_t groupedTriggersRemaining = 0;
        for (size_t j = 0; j < blocks[i].triggers.size(); j++) {
            const auto& trigger = blocks[i].triggers[j];
            
            if (decompiler_) {
                std::string triggerText = decompiler_->decompileTrigger(trigger);
                
                // Check if this is a logical operator (OR, AND)
                if (triggerText.substr(0, 3) == "OR(" || triggerText.substr(0, 4) == "AND(") {
                    // Extract count parameter
                    size_t parenPos = triggerText.find('(');
                    size_t closeParenPos = triggerText.find(')', parenPos);
                    if (parenPos != std::string::npos && closeParenPos != std::string::npos) {
                        std::string countStr = triggerText.substr(parenPos + 1, closeParenPos - parenPos - 1);
                        try {
                            groupedTriggersRemaining = std::stoi(countStr);
                        } catch (...) {
                            groupedTriggersRemaining = 0;
                        }
                    }
                    // Write the logical operator with normal indentation
                    file << "    " << triggerText << "\n";
                } else if (groupedTriggersRemaining > 0) {
                    // This trigger belongs to a logical group - use extra indentation
                    file << "        " << triggerText << "\n";
                    groupedTriggersRemaining--;
                } else {
                    // Normal trigger - use normal indentation
                    file << "    " << triggerText << "\n";
                }
            } else {
                file << "    // Error: Decompiler not initialized\n";
            }
        }
        
        file << "THEN\n";
        
        // Write responses
        for (const auto& response : blocks[i].responses) {
            file << "    RESPONSE #" << response.weight << "\n";
            for (const auto& action : response.actions) {
                if (decompiler_) {
                    file << "        " << decompiler_->decompileAction(action) << "\n";
                } else {
                    file << "        // Error: Decompiler not initialized\n";
                }
            }
        }
        
        file << "END\n";
        
        // Add blank line between blocks (except after the last block)
        if (i < blocks.size() - 1) {
            file << "\n";
        }
    }
    
    // For empty scripts, write nothing further. For non-empty, add a final newline
    if (!blocks.empty()) {
        file << "\n";
    }
    
    return true;
}

// Shared resource management implementation
bool BCS::initializeSharedResources() {
    Log(MESSAGE, "BCS", "Initializing shared IDS resources for batch operations...");
    
    // Load all IDS files once for all BCS instances
    if (!loadIDSFiles()) {
        Log(ERROR, "BCS", "Failed to load IDS files for shared resources");
        return false;
    }
    
    // Initialize the global cache
    if (!IdsMapCache::initializeGlobalCache()) {
        Log(ERROR, "BCS", "Failed to initialize global IDS cache");
        return false;
    }
    
    Log(MESSAGE, "BCS", "Shared IDS resources initialized successfully");
    return true;
}

void BCS::cleanupSharedResources() {
    Log(DEBUG, "BCS", "Cleaning up shared IDS resources");
    // IDS cache cleanup is handled automatically by static destructors
}

bool BCS::ensureDecompilerInitialized() {
    if (!decompiler_) {
        decompiler_ = std::make_unique<BcsDecompiler>();
    }
    
    if (!decompilerInitialized_) {
        if (decompiler_->initialize()) {
            decompilerInitialized_ = true;
            Log(DEBUG, "BCS", "Decompiler initialized successfully");
        } else {
            Log(ERROR, "BCS", "Failed to initialize decompiler");
            return false;
        }
    }
    
    return true;
}

REGISTER_PLUGIN(BCS, IE_BCS_CLASS_ID);

}