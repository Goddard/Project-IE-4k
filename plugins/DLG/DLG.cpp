#include "DLG.h"

#include <fstream>
#include <vector>
#include <iostream>
#include <filesystem>
#include <sstream>

#include "core/SClassID.h"
#include "core/Logging/Logging.h"
#include "plugins/CommandRegistry.h"
#include "plugins/BCS/BCS.h"
#include "plugins/BCS/IdsMapCache.h"

namespace ProjectIE4k {
// Auto-register the DLG plugin
REGISTER_PLUGIN(DLG, IE_DLG_CLASS_ID);

// DLG Class Implementation
DLG::DLG(const std::string& resourceName) : PluginBase(resourceName, IE_DLG_CLASS_ID) {
    if (resourceName.empty()) {
        valid_ = false;
        return;
    }

    Log(DEBUG, "DLG", "DLG plugin initialized for resource: {}", resourceName);
    
    // Initialize BCS components (lazy initialization)
    decompiler_ = std::make_unique<BcsDecompiler>();
    compiler_ = std::make_unique<BCSCompiler>();
    
    std::vector<uint8_t> dlgData = originalFileData;

    // set original extension
    originalExtension = ".DLG";
    
    Log(DEBUG, "DLG", "Loaded DLG resource: {} bytes", dlgData.size());
    
    if (!dlgFile.deserialize(dlgData)) {
        Log(ERROR, "DLG", "Failed to deserialize DLG data for resource: {}", resourceName);
        return;
    }

    // Log the loaded DLG file structure
    Log(DEBUG, "DLG", "Loaded DLG file structure for {}:", resourceName);
    Log(DEBUG, "DLG", "  Signature: {:.4s}", dlgFile.header.signature);
    Log(DEBUG, "DLG", "  Version: {:.4s}", dlgFile.header.version);
    Log(DEBUG, "DLG", "  States: {} (offset: {})", dlgFile.header.statesCount, dlgFile.header.statesOffset);
    Log(DEBUG, "DLG", "  Transitions: {} (offset: {})", dlgFile.header.transitionsCount, dlgFile.header.transitionsOffset);
    Log(DEBUG, "DLG", "  State Triggers: {} (offset: {})", dlgFile.header.stateTriggersCount, dlgFile.header.stateTriggersOffset);
    Log(DEBUG, "DLG", "  Transition Triggers: {} (offset: {})", dlgFile.header.transitionTriggersCount, dlgFile.header.transitionTriggersOffset);
    Log(DEBUG, "DLG", "  Actions: {} (offset: {})", dlgFile.header.actionsCount, dlgFile.header.actionsOffset);
    if (PIE4K_CFG.GameType != "bg1") {
        Log(DEBUG, "DLG", "  Flags: 0x{:08x}", dlgFile.header.gameSpecific.bg2plus.flags);
    }

    valid_ = true;
}

DLG::~DLG() {
    // Clean up large data structures in DLGFile to prevent memory leaks
    dlgFile.states.clear();
    dlgFile.states.shrink_to_fit();
    
    dlgFile.transitions.clear();
    dlgFile.transitions.shrink_to_fit();
    
    dlgFile.stateTriggers.clear();
    dlgFile.stateTriggers.shrink_to_fit();
    
    dlgFile.transitionTriggers.clear();
    dlgFile.transitionTriggers.shrink_to_fit();
    
    dlgFile.actions.clear();
    dlgFile.actions.shrink_to_fit();
    
    dlgFile.stateTriggerStrings.clear();
    dlgFile.stateTriggerStrings.shrink_to_fit();
    
    dlgFile.transitionTriggerStrings.clear();
    dlgFile.transitionTriggerStrings.shrink_to_fit();
    
    dlgFile.actionStrings.clear();
    dlgFile.actionStrings.shrink_to_fit();
    
    // Clean up BCS components (handled by smart pointers)
    decompiler_.reset();
    compiler_.reset();
}

bool DLG::extract() {
    if (!valid_) {
        Log(ERROR, "DLG", "DLG data is not valid, cannot extract.");
        return false;
    }

    std::string outputDir = getExtractDir(true);
    if (outputDir.empty()) {
        Log(ERROR, "DLG", "Failed to create output directory.");
        return false;
    }

    std::string outputPath = outputDir + "/" + resourceName_ + originalExtension;
    
    if (!saveToFile(outputPath)) {
        Log(ERROR, "DLG", "Failed to extract DLG file to: {}", outputPath);
        return false;
    }

    Log(MESSAGE, "DLG", "Successfully extracted DLG file to: {}", outputPath);
    return true;
}

bool DLG::assemble() {
    if (!valid_) {
        Log(ERROR, "DLG", "DLG data is not valid, cannot assemble.");
        return false;
    }

    Log(MESSAGE, "DLG", "Starting DLG assembly for resource: {}", resourceName_);

    // Get the upscaled file path
    std::string upscaledPath = getUpscaledDir(false) + "/" + resourceName_ + originalExtension;
    if (!std::filesystem::exists(upscaledPath)) {
        Log(ERROR, "DLG", "Upscaled DLG file not found: {}", upscaledPath);
        return false;
    }

    // Get the assemble directory
    std::string assembleDir = getAssembleDir(true);
    if (assembleDir.empty()) {
        Log(ERROR, "DLG", "Failed to create assemble directory.");
        return false;
    }

    std::string assemblePath = assembleDir + "/" + originalFileName;
    
    // Copy the upscaled file to the assembled directory
    try {
        std::filesystem::copy_file(upscaledPath, assemblePath, std::filesystem::copy_options::overwrite_existing);
        Log(MESSAGE, "DLG", "Successfully assembled DLG file to: {} (copied from upscaled)", assemblePath);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "DLG", "Failed to copy upscaled DLG file: {}", e.what());
        return false;
    }
}

bool DLG::upscale() {
    if (!valid_) {
        Log(ERROR, "DLG", "DLG data is not valid, cannot upscale.");
        return false;
    }

    Log(MESSAGE, "DLG", "Starting DLG upscale for resource: {}", resourceName_);

    // Get upscale factor
    int upscaleFactor = PIE4K_CFG.UpScaleFactor;
    if (upscaleFactor <= 1) {
        Log(WARNING, "DLG", "Upscale factor is {} (no upscaling needed)", upscaleFactor);
        return true;
    }

    Log(DEBUG, "DLG", "Upscaling DLG embedded BCS scripts by factor: {}", upscaleFactor);

    // Ensure decompiler is initialized
    if (!ensureDecompilerInitialized()) {
        Log(ERROR, "DLG", "Failed to initialize BCS decompiler for upscaling");
        return false;
    }

    // Enable upscaling mode
    decompiler_->setUpscaling(true, upscaleFactor);

    // Process state trigger strings
    for (auto& triggerString : dlgFile.stateTriggerStrings) {
        if (!triggerString.empty() && triggerString.length() > 0) {
            upscaleScriptString(triggerString, upscaleFactor);
        }
    }

    // Process transition trigger strings
    for (auto& triggerString : dlgFile.transitionTriggerStrings) {
        if (!triggerString.empty() && triggerString.length() > 0) {
            upscaleScriptString(triggerString, upscaleFactor);
        }
    }

    // Process action strings
    for (auto& actionString : dlgFile.actionStrings) {
        if (!actionString.empty() && actionString.length() > 0) {
            upscaleScriptString(actionString, upscaleFactor);
        }
    }

    // Save upscaled file
    std::string upscaledDir = getUpscaledDir(true);
    if (upscaledDir.empty()) {
        Log(ERROR, "DLG", "Failed to create upscaled directory.");
        return false;
    }

    std::string upscaledPath = upscaledDir + "/" + resourceName_ + originalExtension;

    if (!saveToFile(upscaledPath)) {
        Log(ERROR, "DLG", "Failed to save upscaled DLG file to: {}", upscaledPath);
        return false;
    }

    Log(MESSAGE, "DLG", "Successfully upscaled DLG file to: {}", upscaledPath);
    return true;
}

bool DLG::saveToFile(const std::string& filePath) {
    if (!valid_) {
        Log(ERROR, "DLG", "DLG data is not valid, cannot save.");
        return false;
    }

    // Add debugging information about the DLG file structure
    Log(DEBUG, "DLG", "Serializing DLG file with structure:");
    Log(DEBUG, "DLG", "  States: {}", dlgFile.states.size());
    Log(DEBUG, "DLG", "  Transitions: {}", dlgFile.transitions.size());
    Log(DEBUG, "DLG", "  State Triggers: {}", dlgFile.stateTriggers.size());
    Log(DEBUG, "DLG", "  Transition Triggers: {}", dlgFile.transitionTriggers.size());
    Log(DEBUG, "DLG", "  Actions: {}", dlgFile.actions.size());
    Log(DEBUG, "DLG", "  State Trigger Strings: {}", dlgFile.stateTriggerStrings.size());
    Log(DEBUG, "DLG", "  Transition Trigger Strings: {}", dlgFile.transitionTriggerStrings.size());
    Log(DEBUG, "DLG", "  Action Strings: {}", dlgFile.actionStrings.size());

    std::vector<uint8_t> data = dlgFile.serialize();

    Log(DEBUG, "DLG", "Serialized data size: {} bytes", data.size());

    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "DLG", "Could not create file: {}", filePath);
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());

    if (file.fail()) {
        Log(ERROR, "DLG", "Failed to write file: {}", filePath);
        return false;
    }

    Log(MESSAGE, "DLG", "Successfully saved DLG file to: {} ({} bytes)", filePath, data.size());
    return true;
}

void DLG::registerCommands(CommandTable& commandTable) {
    commandTable["dlg"] = {
        "DLG file operations",
        {
            {"extract", {
                "Extract DLG resource to file (e.g., dlg extract ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: dlg extract <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().extractResource(args[0], IE_DLG_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {
                "Upscale DLG file (e.g., dlg upscale ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: dlg upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().upscaleResource(args[0], IE_DLG_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {
                "Assemble DLG file (e.g., dlg assemble ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: dlg assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().assembleResource(args[0], IE_DLG_CLASS_ID) ? 0 : 1;
                }
            }}
        }
    };
}

// Path management overrides
std::string DLG::getOutputDir(bool ensureDir) const {
    return constructPath("-dlg", ensureDir);
}

std::string DLG::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-dlg-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string DLG::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-dlg-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string DLG::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-dlg-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

// Clean directories before operations - operation-specific
bool DLG::cleanExtractDirectory() {
    Log(MESSAGE, "DLG", "Cleaning extract directory for resource: {}", resourceName_);
    return cleanDirectory(getOutputDir(false));
}

bool DLG::cleanUpscaleDirectory() {
    Log(MESSAGE, "DLG", "Cleaning upscale directory for resource: {}", resourceName_);
    return cleanDirectory(getUpscaledDir(false));
}

bool DLG::cleanAssembleDirectory() {
    Log(MESSAGE, "DLG", "Cleaning assemble directory for resource: {}", resourceName_);
    return cleanDirectory(getAssembleDir(false));
}

bool DLG::cleanDirectory(const std::string& dir) {
    if (std::filesystem::exists(dir)) {
        try {
            std::filesystem::remove_all(dir);
            Log(MESSAGE, "DLG", "Cleaned directory: {}", dir);
            return true;
        } catch (const std::filesystem::filesystem_error& e) {
            Log(ERROR, "DLG", "Failed to clean directory {}: {}", dir, e.what());
            return false;
        }
    }
    return true; // Directory doesn't exist, nothing to clean
}

// BCS script processing methods
bool DLG::ensureDecompilerInitialized() {
    if (!decompilerInitialized_) {
        // IDS files should already be loaded by shared resource system
        if (!decompiler_->initialize()) {
            Log(ERROR, "DLG", "Failed to initialize BCS decompiler");
            return false;
        }
        decompilerInitialized_ = true;
    }
    return true;
}

bool DLG::ensureCompilerInitialized() {
    if (!compilerInitialized_) {
        // IDS files should already be loaded by shared resource system
        try {
            if (!compiler_->initialize()) {
                Log(ERROR, "DLG", "Failed to initialize BCS compiler");
                return false;
            }
        } catch (const std::exception& e) {
            Log(ERROR, "DLG", "Exception during BCS compiler initialization: {}", e.what());
            return false;
        }
        compilerInitialized_ = true;
    }
    return true;
}

// Shared resource management implementation
bool DLG::initializeSharedResources() {
    Log(MESSAGE, "DLG", "Initializing shared IDS resources for batch operations...");
    
    // Load all IDS files once for all DLG instances
    // Create a temporary BCS instance to access loadIDSFiles()
    BCS tempBCS("temp");
    if (!tempBCS.loadIDSFiles()) {
        Log(ERROR, "DLG", "Failed to load IDS files for shared resources");
        return false;
    }
    
    // Initialize the global cache
    if (!IdsMapCache::initializeGlobalCache()) {
        Log(ERROR, "DLG", "Failed to initialize global IDS cache");
        return false;
    }
    
    Log(MESSAGE, "DLG", "Shared IDS resources initialized successfully");
    return true;
}

void DLG::cleanupSharedResources() {
    Log(DEBUG, "DLG", "Cleaning up shared IDS resources");
    // IDS cache cleanup is handled automatically by static destructors
}

bool DLG::hasCoordinates(const std::string& scriptString) {
    if (scriptString.empty() || scriptString.length() == 0) {
        return false;
    }

    // Look for coordinate patterns like [x.y] in the script
    size_t pos = 0;
    while ((pos = scriptString.find('[', pos)) != std::string::npos) {
        size_t endPos = scriptString.find(']', pos);
        if (endPos == std::string::npos) {
            break;
        }
        
        // Extract the content between brackets
        std::string coordStr = scriptString.substr(pos + 1, endPos - pos - 1);
        
        // Check if it contains a dot (coordinate format: x.y)
        if (coordStr.find('.') != std::string::npos) {
            // Additional validation: check if it's numeric
            try {
                size_t dotPos = coordStr.find('.');
                std::stoi(coordStr.substr(0, dotPos));
                std::stoi(coordStr.substr(dotPos + 1));
                Log(DEBUG, "DLG", "Found coordinate pattern: [{}] in script: {}", coordStr, scriptString.substr(0, std::min(static_cast<size_t>(50), scriptString.length())));
                return true;
            } catch (const std::exception&) {
                // Not a valid coordinate, continue searching
            }
        }
        
        pos = endPos + 1;
    }
    
    return false;
}

std::string DLG::stripComments(const std::string& scriptString) {
    if (scriptString.empty()) {
        return scriptString;
    }

    std::istringstream iss(scriptString);
    std::string line;
    std::string result;
    
    while (std::getline(iss, line)) {
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Skip empty lines
        if (line.empty()) {
            continue;
        }
        
        // Skip comment lines (starting with //)
        if (line.length() >= 2 && line.substr(0, 2) == "//") {
            Log(DEBUG, "DLG", "Skipping comment line: {}", line.substr(0, std::min(static_cast<size_t>(50), line.length())));
            continue;
        }
        
        // Add non-comment lines to result
        result += line + "\r\n";
    }
    
    return result;
}

void DLG::upscaleScriptString(std::string& scriptString, int upscaleFactor) {
    Log(DEBUG, "DLG", "Processing script string (length: {}): {}", 
        scriptString.length(), scriptString.substr(0, std::min(static_cast<size_t>(50), scriptString.length())));
        
    if (scriptString.empty()) {
        Log(DEBUG, "DLG", "Script string is empty, skipping");
        return;
    }

    // Strip comments from the script string
    std::string cleanedScript = stripComments(scriptString);
    if (cleanedScript.empty()) {
        Log(DEBUG, "DLG", "Script string is empty after comment stripping, skipping");
        return;
    }

    // Check if the cleaned script contains coordinates before attempting BCS compilation
    if (!hasCoordinates(cleanedScript)) {
        Log(DEBUG, "DLG", "Script contains no coordinates, skipping BCS processing: {}", 
            cleanedScript.substr(0, std::min(static_cast<size_t>(50), cleanedScript.length())));
        return;
    }

    Log(DEBUG, "DLG", "Upscaling script string using BCS compiler/decompiler: {}", 
        cleanedScript.substr(0, std::min(static_cast<size_t>(50), cleanedScript.length())));

    // Ensure compiler and decompiler are initialized
    if (!ensureCompilerInitialized()) {
        Log(ERROR, "DLG", "Failed to initialize BCS compiler for script upscaling");
        return;
    }
    
    if (!ensureDecompilerInitialized()) {
        Log(ERROR, "DLG", "Failed to initialize BCS decompiler for script upscaling");
        return;
    }

    try {
        // Step 1: Wrap the cleaned DLG script in minimal BCS block structure for compilation
        // DLG scripts are individual action lines, but BCS compiler expects IF/THEN/END blocks
        std::string wrappedScript = "IF\nTrue()\nTHEN\n";
        
        // Split the cleaned script into individual lines and add them as actions
        std::istringstream iss(cleanedScript);
        std::string line;
        while (std::getline(iss, line)) {
            // Remove carriage return if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            // Skip empty lines and lines that are just whitespace
            if (!line.empty() && line.find_first_not_of(" \t\r\n") != std::string::npos) {
                // Additional check: skip lines that might cause BCS compilation issues
                if (line.find("()") != std::string::npos && line.find("(") == line.find("()")) {
                    Log(DEBUG, "DLG", "Skipping potentially problematic line: {}", line);
                    continue;
                }
                wrappedScript += line + "\n";
            }
        }
        wrappedScript += "END\n";

        // Check if the wrapped script is empty or just contains the IF/THEN/END structure
        std::string trimmedScript = wrappedScript;
        trimmedScript.erase(0, trimmedScript.find_first_not_of(" \t\r\n"));
        trimmedScript.erase(trimmedScript.find_last_not_of(" \t\r\n") + 1);
        
        if (trimmedScript.empty() || trimmedScript == "IF\nTrue()\nTHEN\nEND\n") {
            Log(DEBUG, "DLG", "Wrapped script is empty or contains no actions, skipping BCS compilation");
            return;
        }

        // Step 2: Compile the wrapped script to BCSBlock structures
        std::vector<BCSBlock> blocks;
        try {
            if (!compiler_->compileText(wrappedScript, blocks)) {
            Log(WARNING, "DLG", "Failed to compile wrapped script string, skipping upscaling: {}", 
                scriptString.substr(0, std::min(static_cast<size_t>(50), scriptString.length())));
                return;
            }
        } catch (const std::exception& e) {
            Log(ERROR, "DLG", "Exception during BCS compilation: {}", e.what());
            Log(ERROR, "DLG", "Wrapped script that caused the exception: {}", wrappedScript);
            return;
        }

        // Step 3: Create a new decompiler with upscaling enabled
        auto upscaleDecompiler = std::make_unique<BcsDecompiler>();
        if (!upscaleDecompiler->initialize()) {
            Log(ERROR, "DLG", "Failed to initialize upscale decompiler");
            return;
        }
        
        // Enable upscaling mode
        upscaleDecompiler->setUpscaling(true, upscaleFactor);

        // Step 4: Decompile the blocks back to text with upscaling applied
        // Extract only the actions (skip the True() trigger we added)
        std::string upscaledScript;
        for (const auto& block : blocks) {
            for (const auto& response : block.responses) {
                for (const auto& action : response.actions) {
                    upscaledScript += upscaleDecompiler->decompileAction(action) + "\r\n";
                }
            }
        }

        if (upscaledScript.empty()) {
            Log(WARNING, "DLG", "Failed to decompile script with upscaling, keeping original");
            return;
        }

        // Step 5: Update the script string with the upscaled version
        scriptString = upscaledScript;
        
        Log(DEBUG, "DLG", "Successfully upscaled script string using BCS compiler/decompiler");
        
    } catch (const std::exception& e) {
        Log(ERROR, "DLG", "Exception during script upscaling: {}", e.what());
    }
}

} // namespace ProjectIE4k
