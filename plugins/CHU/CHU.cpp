#include "CHU.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <iomanip>

#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

// Auto-register the CHU plugin
REGISTER_PLUGIN(CHU, IE_CHU_CLASS_ID);

CHU::CHU(const std::string& resourceName_) 
    : PluginBase(resourceName_, IE_CHU_CLASS_ID) {
    if (!loadFromData()) {
        Log(ERROR, "CHU", "Failed to load CHU data");
        return;
    }
    
    // Mark plugin as valid since we successfully loaded the CHU resource
    valid_ = true;
}

CHU::~CHU() {
    // Clean up large data structures to prevent memory leaks
    windows.clear();
    windows.shrink_to_fit();
    
    controlTable.clear();
    controlTable.shrink_to_fit();
    
    // Clean up the nested vector of control data
    controlData.clear();
    controlData.shrink_to_fit();
}

bool CHU::extract() {
    Log(MESSAGE, "CHU", "Starting CHU extraction for resource: {}", resourceName_);
    
    // Create output directory
    std::string outputDir = getExtractDir(true);
    std::string outputPath = outputDir + "/" + resourceName_ + originalExtension;
    
    // Write the original CHU data to file
    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
        Log(ERROR, "CHU", "Failed to open output file: {}", outputPath);
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(originalFileData.data()), originalFileData.size());
    file.close();
    
    Log(MESSAGE, "CHU", "Successfully extracted CHU file: {} ({} bytes)", outputPath, originalFileData.size());
    return true;
}

bool CHU::assemble() {
    Log(MESSAGE, "CHU", "Starting CHU assembly for resource: {}", resourceName_);
    
    std::string upscaledPath = getUpscaledDir();
    std::string assemblePath = getAssembleDir();
    
    if (!std::filesystem::exists(upscaledPath)) {
        Log(ERROR, "CHU", "Upscaled directory not found: {}", upscaledPath);
        return false;
    }
    
    // Create assemble directory if it doesn't exist
    if (!std::filesystem::exists(assemblePath)) {
        std::filesystem::create_directories(assemblePath);
        Log(MESSAGE, "CHU", "Created assemble directory: {}", assemblePath);
    }
    
    // Copy all files from upscaled to assembled
    try {
        for (const auto& entry : std::filesystem::directory_iterator(upscaledPath)) {
            if (entry.is_regular_file()) {
                std::string sourceFile = entry.path().string();
                std::string destFile = assemblePath + "/" + originalFileName;
                
                std::filesystem::copy_file(sourceFile, destFile, std::filesystem::copy_options::overwrite_existing);
                Log(MESSAGE, "CHU", "Copied {} to {}", entry.path().filename().string(), destFile);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "CHU", "Filesystem error during assembly: {}", e.what());
        return false;
    }
    
    Log(MESSAGE, "CHU", "Successfully assembled CHU: {}", resourceName_);
    return true;
}

bool CHU::upscale() {
    if (!valid_) {
        Log(ERROR, "CHU", "CHU file not loaded or invalid");
        return false;
    }

    int upscaleFactor = PIE4K_CFG.UpScaleFactor;
    Log(DEBUG, "CHU", "Upscaling CHU coordinates by factor: {}", upscaleFactor);
    if (upscaleFactor <= 1) {
        Log(WARNING, "CHU", "Upscale factor is {} (no upscaling needed)", upscaleFactor);
        return true;
    }

    // --- Parse original windows and controls ---
    std::vector<CHUWindow> windows = this->windows;
    std::vector<CHUControlTableEntry> controls = this->controlTable;
    std::vector<std::vector<uint8_t>> originalControlData = readControlData();

    // --- Upscale window coordinates ---
    for (auto& win : windows) {
        Log(DEBUG, "CHU", "Upscaling window: x={}->{}, y={}->{}, w={}->{}, h={}->{}", 
            win.x, win.x * upscaleFactor, win.y, win.y * upscaleFactor, 
            win.width, win.width * upscaleFactor, win.height, win.height * upscaleFactor);
        win.x *= upscaleFactor;
        win.y *= upscaleFactor;
        win.width *= upscaleFactor;
        win.height *= upscaleFactor;
    }

    // --- Recalculate total control count ---
    uint32_t totalControlCount = 0;
    for (const auto& win : windows) {
        totalControlCount += win.controlCount;
    }
    if (totalControlCount != controls.size()) {
        Log(ERROR, "CHU", "Mismatch: window table controlCount sum {} != control table size {}", totalControlCount, controls.size());
        return false;
    }

    // --- Prepare new header preserving original layout ---
    CHUHeader newHeader = header;  // Keep original offsets
    newHeader.windowCount = windows.size();

    // --- Write new CHU file preserving original layout ---
    std::vector<uint8_t> out;
    
    // Determine the layout: window table first or control table first
    bool windowTableFirst = (header.windowOffset < header.controlTableOffset);
    
    if (windowTableFirst) {
        // Original layout: window table comes before control table
        size_t windowTableSize = windows.size() * sizeof(CHUWindow);
        size_t controlTableSize = controls.size() * sizeof(CHUControlTableEntry);
        
        // Calculate total size needed
        size_t totalSize = sizeof(CHUHeader) + windowTableSize + controlTableSize;
        
        // Add space for control data
        for (const auto& entry : controls) {
            totalSize += entry.controlLength;
        }
        
        out.resize(totalSize);
        
        // Write header
        memcpy(out.data(), &newHeader, sizeof(CHUHeader));
        
        // Write window table first
        for (size_t i = 0; i < windows.size(); ++i) {
            memcpy(out.data() + newHeader.windowOffset + i * sizeof(CHUWindow), &windows[i], sizeof(CHUWindow));
        }
        
        // Write control table after window table
        for (size_t i = 0; i < controls.size(); ++i) {
            memcpy(out.data() + newHeader.controlTableOffset + i * sizeof(CHUControlTableEntry), &controls[i], sizeof(CHUControlTableEntry));
        }
    } else {
        // Standard layout: control table comes before window table
        size_t controlTableSize = controls.size() * sizeof(CHUControlTableEntry);
        size_t windowTableSize = windows.size() * sizeof(CHUWindow);
        
        // Calculate total size needed
        size_t totalSize = sizeof(CHUHeader) + controlTableSize + windowTableSize;
        
        // Add space for control data
        for (const auto& entry : controls) {
            totalSize += entry.controlLength;
        }
        
        out.resize(totalSize);
        
        // Write header
        memcpy(out.data(), &newHeader, sizeof(CHUHeader));
        
        // Write control table first
        for (size_t i = 0; i < controls.size(); ++i) {
            memcpy(out.data() + newHeader.controlTableOffset + i * sizeof(CHUControlTableEntry), &controls[i], sizeof(CHUControlTableEntry));
        }
        
        // Write window table after control table
        for (size_t i = 0; i < windows.size(); ++i) {
            memcpy(out.data() + newHeader.windowOffset + i * sizeof(CHUWindow), &windows[i], sizeof(CHUWindow));
        }
    }
    
    // Write upscaled control data
    size_t controlsStart;
    if (windowTableFirst) {
        controlsStart = newHeader.controlTableOffset + controls.size() * sizeof(CHUControlTableEntry);
    } else {
        controlsStart = newHeader.windowOffset + windows.size() * sizeof(CHUWindow);
    }
    
    for (size_t i = 0; i < originalControlData.size(); ++i) {
        size_t offset = controlsStart;
        // Update control table entry offset
        CHUControlTableEntry& entry = controls[i];
        entry.controlOffset = offset;
        memcpy(out.data() + newHeader.controlTableOffset + i * sizeof(CHUControlTableEntry), &entry, sizeof(CHUControlTableEntry));
        
        // Upscale and append control data
        std::vector<uint8_t> upscaledControlData = upscaleControlData(originalControlData[i], upscaleFactor);
        out.resize(offset + upscaledControlData.size());
        memcpy(out.data() + offset, upscaledControlData.data(), upscaledControlData.size());
        controlsStart += upscaledControlData.size();
    }

    // --- Write to file ---
    std::string outPath = getUpscaledDir();
    std::string baseName = extractBaseName();
    std::string outputPath = outPath + "/" + baseName + originalExtension;
    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile) {
        Log(ERROR, "CHU", "Failed to open output file: {}", outputPath);
        return false;
    }
    outFile.write(reinterpret_cast<const char*>(out.data()), out.size());
    outFile.close();
    Log(MESSAGE, "CHU", "Upscaled CHU written to {} ({} bytes)", outputPath, out.size());
    return true;
}

bool CHU::loadFromData() {
    if (originalFileData.empty()) {
        Log(ERROR, "CHU", "No CHU data loaded");
        return false;
    }

    // Parse the CHU header
    if (!readHeader()) {
        Log(ERROR, "CHU", "Failed to parse CHU header");
        return false;
    }
    
    // Read windows and control table
    if (!readWindows()) {
        Log(ERROR, "CHU", "Failed to parse CHU windows");
        return false;
    }

    // Parse control table
    if (!readControlTable()) {
        Log(ERROR, "CHU", "Failed to parse CHU control table");
        return false;
    }

    Log(DEBUG, "CHU", "Successfully loaded CHU resource: {}", resourceName_);
    Log(DEBUG, "CHU", "  Windows: {}", windows.size());
    Log(DEBUG, "CHU", "  Controls: {}", controlTable.size());

    return true;
}

bool CHU::readWindows() {
    if (originalFileData.size() < header.windowOffset + sizeof(CHUWindow) * header.windowCount) {
        Log(ERROR, "CHU", "File too small for window table");
        return false;
    }
    
    windows.clear();
    for (uint32_t i = 0; i < header.windowCount; ++i) {
        const CHUWindow* win = reinterpret_cast<const CHUWindow*>(originalFileData.data() + header.windowOffset + i * sizeof(CHUWindow));
        windows.push_back(*win);
    }
    
    Log(DEBUG, "CHU", "Read {} windows", windows.size());
    return true;
}

bool CHU::readControlTable() {
    // Get expected control count from windows
    uint32_t expectedControls = 0;
    for (const auto& win : windows) {
        expectedControls += win.controlCount;
    }
    
    // Calculate control table size based on expected count
    size_t controlTableSize = expectedControls * sizeof(CHUControlTableEntry);
    
    // Verify we have enough space
    if (originalFileData.size() < header.controlTableOffset + controlTableSize) {
        Log(ERROR, "CHU", "File too small for control table: need {} bytes at offset {}", controlTableSize, header.controlTableOffset);
        return false;
    }
    
    controlTable.clear();
    for (size_t i = 0; i < expectedControls; ++i) {
        const CHUControlTableEntry* entry = reinterpret_cast<const CHUControlTableEntry*>(originalFileData.data() + header.controlTableOffset + i * sizeof(CHUControlTableEntry));
        controlTable.push_back(*entry);
    }
    
    Log(DEBUG, "CHU", "Read {} control table entries", controlTable.size());
    return true;
}

// Add this function to read control data from the original file
std::vector<std::vector<uint8_t>> CHU::readControlData() {
    std::vector<std::vector<uint8_t>> controlData;
    
    for (const auto& entry : controlTable) {
        if (entry.controlOffset + entry.controlLength > originalFileData.size()) {
            Log(ERROR, "CHU", "Control data out of bounds at offset 0x{:08x}", entry.controlOffset);
            controlData.push_back(std::vector<uint8_t>());
            continue;
        }
        
        std::vector<uint8_t> data(entry.controlLength);
        memcpy(data.data(), originalFileData.data() + entry.controlOffset, entry.controlLength);
        controlData.push_back(std::move(data));
    }
    
    Log(DEBUG, "CHU", "Read {} control data blocks", controlData.size());
    return controlData;
}

// Add this function to upscale control coordinates
std::vector<uint8_t> CHU::upscaleControlData(const std::vector<uint8_t>& originalData, int upscaleFactor) {
    if (originalData.size() < sizeof(CHUControlCommon)) {
        return originalData; // Too small to contain control common fields
    }
    
    std::vector<uint8_t> upscaledData = originalData;
    CHUControlCommon* ctrl = reinterpret_cast<CHUControlCommon*>(upscaledData.data());
    
    // Upscale coordinates
    ctrl->x *= upscaleFactor;
    ctrl->y *= upscaleFactor;
    ctrl->width *= upscaleFactor;
    ctrl->height *= upscaleFactor;
    
    return upscaledData;
}

bool CHU::compare(const std::string& compareType) {
    if (!valid_) {
        Log(ERROR, "CHU", "CHU file not loaded or invalid");
        return false;
    }
    
    Log(DEBUG, "CHU", "Starting CHU comparison for resource: {} with type: {}", resourceName_, compareType);
    
    // Determine the path to the file to compare against
    std::string comparePath;
    std::string baseName = extractBaseName();
    
    if (compareType == "extract") {
        comparePath = getOutputDir() + "/" + baseName + originalExtension;
    } else if (compareType == "upscale") {
        comparePath = getUpscaledDir() + "/" + baseName + originalExtension;
    } else {
        Log(ERROR, "CHU", "Invalid compare type: {}. Must be 'extract' or 'upscale'", compareType);
        return false;
    }
    
    // Check if the comparison file exists
    if (!std::filesystem::exists(comparePath)) {
        Log(ERROR, "CHU", "Comparison file not found: {}", comparePath);
        return false;
    }
    
    // Read the comparison file
    std::ifstream compareFile(comparePath, std::ios::binary);
    if (!compareFile.is_open()) {
        Log(ERROR, "CHU", "Failed to open comparison file: {}", comparePath);
        return false;
    }
    
    // Get file size
    compareFile.seekg(0, std::ios::end);
    size_t compareFileSize = compareFile.tellg();
    compareFile.seekg(0, std::ios::beg);
    
    // Read the comparison data
    std::vector<uint8_t> compareData(compareFileSize);
    compareFile.read(reinterpret_cast<char*>(compareData.data()), compareFileSize);
    compareFile.close();
    
    Log(DEBUG, "CHU", "=== CHU Comparison Report ===");
    Log(DEBUG, "CHU", "Original resource: {}", resourceName_);
    Log(DEBUG, "CHU", "Comparison file: {}", comparePath);
    Log(DEBUG, "CHU", "Original size: {} bytes", originalFileData.size());
    Log(DEBUG, "CHU", "Comparison size: {} bytes", compareData.size());
    
    // Compare file sizes
    if (originalFileData.size() != compareData.size()) {
        Log(WARNING, "CHU", "File sizes differ: original={}, comparison={}", 
            originalFileData.size(), compareData.size());
    } else {
        Log(DEBUG, "CHU", "File sizes match: {} bytes", originalFileData.size());
    }
    
    // Simple binary comparison
    size_t minSize = std::min(originalFileData.size(), compareData.size());
    size_t differences = 0;
    size_t firstDiff = 0;
    
    for (size_t i = 0; i < minSize; ++i) {
        if (originalFileData[i] != compareData[i]) {
            if (differences == 0) {
                firstDiff = i;
            }
            differences++;
        }
    }
    
    Log(DEBUG, "CHU", "--- Binary Comparison ---");
    Log(DEBUG, "CHU", "Bytes compared: {}", minSize);
    Log(DEBUG, "CHU", "Bytes different: {}", differences);
    
    if (differences == 0) {
        Log(DEBUG, "CHU", "Files are 100% identical");
    } else {
        if (minSize > 0) {
            double similarityPercent = 100.0 * (minSize - differences) / minSize;
            Log(MESSAGE, "CHU", "Similarity: {:.2f}%", similarityPercent);
        }
        Log(DEBUG, "CHU", "First difference at byte offset: 0x{:08x} ({})", firstDiff, firstDiff);
        
        // Show a few bytes around the first difference
        size_t start = (firstDiff > 16) ? firstDiff - 16 : 0;
        size_t end = std::min(firstDiff + 16, minSize);
        
        Log(DEBUG, "CHU", "--- Hex dump around first difference ---");
        for (size_t i = start; i < end; i += 16) {
            std::ostringstream line;
            line << std::hex << std::setfill('0') << std::setw(8) << i << ": ";
            
            for (size_t j = 0; j < 16 && i + j < end; ++j) {
                if (i + j < originalFileData.size()) {
                    line << std::hex << std::setfill('0') << std::setw(2) << (int)originalFileData[i + j] << " ";
                } else {
                    line << "   ";
                }
            }
            
            line << " | ";
            for (size_t j = 0; j < 16 && i + j < end; ++j) {
                if (i + j < compareData.size()) {
                    line << std::hex << std::setfill('0') << std::setw(2) << (int)compareData[i + j] << " ";
                } else {
                    line << "   ";
                }
            }
            
            Log(DEBUG, "CHU", "{}", line.str());
        }
    }
    
    Log(DEBUG, "CHU", "=== End CHU Comparison Report ===");
    return true;
}

bool CHU::readHeader() {
    if (originalFileData.size() < sizeof(CHUHeader)) {
        return false;
    }
    
    memcpy(&header, originalFileData.data(), sizeof(CHUHeader));
    Log(DEBUG, "CHU", "Header: controlTableOffset=0x{:08x} ({}), windowOffset=0x{:08x} ({}), windowCount={}", 
        header.controlTableOffset, header.controlTableOffset, header.windowOffset, header.windowOffset, header.windowCount);
    
    if (header.windowCount == 0 || header.windowCount > 256) {
        Log(ERROR, "CHU", "Unrealistic windowCount: {}", header.windowCount);
        return false;
    }
    return true;
}

bool CHU::writeHeader(std::ofstream& file) {
    Log(DEBUG, "CHU", "Writing header: controlTableOffset=0x{:08x} ({}), windowOffset=0x{:08x} ({}), windowCount={}", 
        header.controlTableOffset, header.controlTableOffset, header.windowOffset, header.windowOffset, header.windowCount);
    if (header.controlTableOffset != 20) {
        Log(ERROR, "CHU", "[WRITE] controlTableOffset is not 20! Found: {}", header.controlTableOffset);
        return false;
    }
    if (header.windowOffset <= header.controlTableOffset) {
        Log(ERROR, "CHU", "[WRITE] windowOffset ({}) is not after controlTableOffset ({})!", header.windowOffset, header.controlTableOffset);
        return false;
    }
    if (header.windowCount == 0 || header.windowCount > 256) {
        Log(ERROR, "CHU", "[WRITE] Unrealistic windowCount: {}", header.windowCount);
        return false;
    }
    file.write(reinterpret_cast<const char*>(&header), sizeof(CHUHeader));
    return !file.fail();
}

void CHU::registerCommands(CommandTable& commandTable) {
    commandTable["chu"] = {
        "CHU file operations",
        {
            {"extract", {"Extract CHU resource to file (e.g., chu extract mainmenu)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: chu extract <resource_name>" << std::endl;
                        return 1;
                    }
                    CHU chu(args[0]);
                    return chu.extract() ? 0 : 1;
                }
            }},
            {"upscale", {"Upscale CHU coordinates (e.g., chu upscale mainmenu)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: chu upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    CHU chu(args[0]);
                    return chu.upscale() ? 0 : 1;
                }
            }},
            {"assemble", {"Assemble CHU files (e.g., chu assemble mainmenu)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: chu assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    CHU chu(args[0]);
                    return chu.assemble() ? 0 : 1;
                }
            }},
            {"compare", {"Compare CHU with extracted/upscaled version (e.g., chu compare mainmenu extract)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.size() < 2) {
                        std::cerr << "Usage: chu compare <resource_name> <compare_type>" << std::endl;
                        std::cerr << "compare_type: extract or upscale" << std::endl;
                        return 1;
                    }
                    CHU chu(args[0]);
                    return chu.compare(args[1]) ? 0 : 1;
                }
            }}
        }
    };
}

// PluginBase interface implementations
bool CHU::extractAll() {
    return PluginManager::getInstance().extractAllResourcesOfType(IE_CHU_CLASS_ID);
}

bool CHU::upscaleAll() {
    return PluginManager::getInstance().upscaleAllResourcesOfType(IE_CHU_CLASS_ID);
}

bool CHU::assembleAll() {
    return PluginManager::getInstance().assembleAllResourcesOfType(IE_CHU_CLASS_ID);
}

bool CHU::cleanExtractDirectory() {
    return cleanDirectory(getOutputDir(false));
}

bool CHU::cleanUpscaleDirectory() {
    return cleanDirectory(getUpscaledDir(false));
}

bool CHU::cleanAssembleDirectory() {
    return cleanDirectory(getAssembleDir(false));
}

std::string CHU::getOutputDir(bool ensureDir) const {
    return constructPath("-chu", ensureDir);
}

std::string CHU::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-chu-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string CHU::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-chu-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string CHU::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-chu-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

bool CHU::cleanDirectory(const std::string& dir) {
    if (!std::filesystem::exists(dir)) {
        return true; // Directory doesn't exist, nothing to clean
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::filesystem::remove(entry.path());
                Log(DEBUG, "CHU", "Cleaned file: {}", entry.path().filename().string());
            }
        }
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        Log(ERROR, "CHU", "Failed to clean directory {}: {}", dir, e.what());
        return false;
    }
}

} // namespace ProjectIE4k
