#include "KEY.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <map>

#include "core/SClassID.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

KEY::KEY(const std::string& filePath) 
    : filePath(filePath), valid(false) {
}

KEY::~KEY() { }

bool KEY::load() {
    // Debug: verify struct sizes match IESDP specification
    std::cout << "KEY struct sizes - Header: " << sizeof(KEYHeader) 
              << " bytes, BIF Entry: " << sizeof(KEYBIFEntry) 
              << " bytes, RES Entry: " << sizeof(KEYRESEntry) << " bytes" << std::endl;
    
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open KEY file: " << filePath << std::endl;
        return false;
    }
    
    // Read header
    if (!readHeader()) {
        std::cerr << "Failed to read KEY header" << std::endl;
        return false;
    }
    
    // Verify signature and version
    if (memcmp(header.signature, "KEY ", 4) != 0) {
        std::cerr << "Invalid KEY signature" << std::endl;
        return false;
    }
    
    if (memcmp(header.version, "V1  ", 4) != 0) {
        std::cerr << "Unsupported KEY version: " << std::string(header.version, 4) << std::endl;
        return false;
    }
    
    // Read BIF entries
    if (!readBIFs()) {
        std::cerr << "Failed to read BIF entries" << std::endl;
        return false;
    }
    
    // Read RES entries
    if (!readResources()) {
        std::cerr << "Failed to read RES entries" << std::endl;
        return false;
    }
    
    // Read BIF filenames
    if (!readBIFNames()) {
        std::cerr << "Failed to read BIF names" << std::endl;
        return false;
    }
    
    valid = true;
    std::cout << "Successfully loaded KEY file: " << filePath << std::endl;
    std::cout << "  BIF files: " << bifs.size() << std::endl;
    std::cout << "  Resources: " << resources.size() << std::endl;
    
    if (!resources.empty()) {
        const auto& last = resources.back();
        std::cout << "Last resource: name='" << std::string(last.name, 8) << "' type=" << last.type << " locator=0x" << std::hex << last.locator << std::dec << std::endl;
    }

    return true;
}

bool KEY::save(const std::string& outputPath) {
    // Ensure header offsets are up-to-date
    calculateOffsets();
    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << outputPath << std::endl;
        return false;
    }
    
    // Write header
    if (!writeHeader(file)) {
        std::cerr << "Failed to write header" << std::endl;
        return false;
    }
    
    // Write BIF entries
    if (!writeBIFs(file)) {
        std::cerr << "Failed to write BIF entries" << std::endl;
        return false;
    }
    
    // Write RES entries
    if (!writeResources(file)) {
        std::cerr << "Failed to write RES entries" << std::endl;
        return false;
    }
    
    // Write BIF names
    if (!writeBIFNames(file)) {
        std::cerr << "Failed to write BIF names" << std::endl;
        return false;
    }
    
    std::cout << "Successfully saved KEY file: " << outputPath << std::endl;
    return true;
}

void KEY::printInfo() const {
    std::cout << "KEY File: " << filePath << std::endl;
    std::cout << "  BIF files: " << bifs.size() << std::endl;
    std::cout << "  Resources: " << resources.size() << std::endl;
    
    if (!bifs.empty()) {
        std::cout << "  BIF files:" << std::endl;
        for (size_t i = 0; i < bifs.size(); ++i) {
            std::cout << "    " << i << ": " << bifs[i].filename 
                      << " (size: " << bifs[i].fileSize << ")" << std::endl;
        }
    }
    
    if (!resources.empty()) {
        std::cout << "  Resources:" << std::endl;
        for (size_t i = 0; i < std::min<size_t>(10, resources.size()); ++i) {
            std::cout << "    " << i << ": " << std::string(resources[i].name, 8) 
                      << " (type: 0x" << std::hex << resources[i].type << std::dec << ")" << std::endl;
        }
        if (resources.size() > 10) {
            std::cout << "    ... and " << (resources.size() - 10) << " more" << std::endl;
        }
    }
}

bool KEY::addResource(const std::string& resourceName, SClass_ID resourceType) {
    // Extract just the name part (without extension) and ensure it's 8 characters max
    std::string name = resourceName;
    size_t dotPos = name.find_last_of('.');
    if (dotPos != std::string::npos) {
        name = name.substr(0, dotPos);
    }
    
    if (name.length() > 8) {
        std::cerr << "Resource name too long: " << name << " (max 8 characters)" << std::endl;
        return false;
    }
    
    // For now, add with a dummy locator (BIF index 0, file index 0)
    // In a real implementation, you'd need to determine the actual BIF and file index
    addResource(name, static_cast<uint16_t>(resourceType), 0, 0);
    return true;
}

KEYBIFEntry& KEY::getBIF(size_t index) {
    if (index >= bifs.size()) {
        throw std::out_of_range("BIF index out of range");
    }
    return bifs[index];
}

KEYRESEntry& KEY::getResource(size_t index) {
    if (index >= resources.size()) {
        throw std::out_of_range("Resource index out of range");
    }
    return resources[index];
}

std::vector<size_t> KEY::findResourcesByType(uint16_t type) const {
    std::vector<size_t> results;
    for (size_t i = 0; i < resources.size(); ++i) {
        if (resources[i].type == type) {
            results.push_back(i);
        }
    }
    return results;
}

std::vector<size_t> KEY::findResourcesByName(const std::string& name) const {
    std::vector<size_t> results;
    for (size_t i = 0; i < resources.size(); ++i) {
        if (strncmp(resources[i].name, name.c_str(), 8) == 0) {
            results.push_back(i);
        }
    }
    return results;
}

size_t KEY::findBIFByName(const std::string& name) const {
    for (size_t i = 0; i < bifs.size(); ++i) {
        if (bifs[i].filename == name) {
            return i;
        }
    }
    return SIZE_MAX;
}

bool KEY::readHeader() {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;
    
    file.read(reinterpret_cast<char*>(&header), sizeof(KEYHeader));
    return file.good();
}

bool KEY::readBIFs() {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // If no BIF files, skip reading BIF entries
    if (header.bifCount == 0) {
        bifs.clear();
        return true;
    }
    
    file.seekg(header.bifOffset);
    bifs.resize(header.bifCount);
    
    for (uint32_t i = 0; i < header.bifCount; ++i) {
        file.read(reinterpret_cast<char*>(&bifs[i].fileSize), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&bifs[i].fileOffset), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&bifs[i].filenameLength), sizeof(uint16_t));
        file.read(reinterpret_cast<char*>(&bifs[i].filenameOffset), sizeof(uint16_t));
    }
    
    return file.good();
}

bool KEY::readResources() {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;
    
    std::cout << "Reading resources: offset=" << header.resOffset 
              << ", count=" << header.resCount << std::endl;
    
    file.seekg(header.resOffset);
    resources.resize(header.resCount);
    
    for (uint32_t i = 0; i < header.resCount; ++i) {
        // Read resource name (8 bytes)
        file.read(resources[i].name, 8);
        // Read resource type (2 bytes)
        file.read(reinterpret_cast<char*>(&resources[i].type), sizeof(uint16_t));
        // Read resource locator (4 bytes)
        file.read(reinterpret_cast<char*>(&resources[i].locator), sizeof(uint32_t));
        if (!file.good()) {
            std::cerr << "Failed to read resource " << i << " at position " << file.tellg() 
                      << ", eof=" << file.eof() << ", fail=" << file.fail() << std::endl;
            return false;
        }
    }
    
    return file.good();
}

bool KEY::readBIFNames() {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // If no BIF files, skip reading BIF names
    if (bifs.empty()) {
        return true;
    }
    
    // Read BIF filenames from their respective offsets
    for (auto& bif : bifs) {
        file.seekg(bif.filenameOffset);
        std::vector<char> buffer(bif.filenameLength);
        file.read(buffer.data(), bif.filenameLength);
        bif.filename = std::string(buffer.data(), bif.filenameLength);
    }
    
    return file.good();
}

bool KEY::writeHeader(std::ofstream& file) {
    file.write(reinterpret_cast<const char*>(&header), sizeof(KEYHeader));
    return file.good();
}

bool KEY::writeBIFs(std::ofstream& file) {
    for (const auto& bif : bifs) {
        file.write(reinterpret_cast<const char*>(&bif.fileSize), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&bif.fileOffset), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&bif.filenameLength), sizeof(uint16_t));
        file.write(reinterpret_cast<const char*>(&bif.filenameOffset), sizeof(uint16_t));
    }
    return file.good();
}

bool KEY::writeResources(std::ofstream& file) {
    for (const auto& res : resources) {
        file.write(reinterpret_cast<const char*>(&res), sizeof(KEYRESEntry));
    }
    return file.good();
}

bool KEY::writeBIFNames(std::ofstream& file) {
    for (const auto& bif : bifs) {
        file.write(bif.filename.c_str(), bif.filenameLength);
    }
    return file.good();
}

uint32_t KEY::calculateOffsets() {
    uint32_t currentOffset = sizeof(KEYHeader);
    
    // Calculate BIF offset
    header.bifOffset = currentOffset;
    currentOffset += bifs.size() * (sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t));
    
    // Calculate RES offset
    header.resOffset = currentOffset;
    currentOffset += resources.size() * sizeof(KEYRESEntry);
    
    // Calculate BIF name offsets
    for (auto& bif : bifs) {
        bif.filenameOffset = currentOffset;
        bif.filenameLength = static_cast<uint16_t>(bif.filename.length());
        currentOffset += bif.filenameLength;
    }
    
    return currentOffset;
}

void KEY::addResource(const std::string& name, uint16_t type, uint32_t locator) {
    KEYRESEntry entry{};
    std::memset(entry.name, 0, 8);
    std::memcpy(entry.name, name.c_str(), std::min<size_t>(8, name.size()));
    entry.type = type;
    entry.locator = locator;
    resources.push_back(entry);
    header.resCount = resources.size();
    
    // Recalculate offsets after adding resource
    calculateOffsets();
}

void KEY::addResource(const std::string& name, uint16_t type, uint32_t bifIndex, uint32_t fileIndex) {
    // Construct locator from BIF index and file index
    // Bits 31-20: BIF index, Bits 13-0: file index
    uint32_t locator = ((bifIndex & 0xFFF) << 20) | (fileIndex & 0x3FFF);
    addResource(name, type, locator);
}

bool KEY::removeResource(const std::string& name) {
    for (size_t i = 0; i < resources.size(); ++i) {
        if (strncmp(resources[i].name, name.c_str(), 8) == 0) {
            return removeResource(i);
        }
    }
    return false;
}

bool KEY::removeResource(size_t index) {
    if (index >= resources.size()) {
        return false;
    }
    
    resources.erase(resources.begin() + index);
    header.resCount = resources.size();
    
    // Recalculate offsets after removing resource
    calculateOffsets();
    return true;
}

void KEY::registerCommands(CommandTable& commandTable) {
    Command keyCommand;
    keyCommand.help = "KEY file operations";
    
    // Load command
    keyCommand.actions["load"] = {
        "Load and display KEY file information (e.g., key load chitin.key)",
        [](const std::vector<std::string>& args) -> int {
            if (args.size() < 2) {
                std::cerr << "Usage: " << args[0] << " key load <keyfile>" << std::endl;
                return 1;
            }
            std::string keyFile = args[1];
            KEY key(keyFile);
            if (!key.load()) {
                std::cerr << "Failed to load KEY file: " << keyFile << std::endl;
                return 1;
            }
            key.printInfo();
            return 0;
        }
    };
    
    // Save command
    keyCommand.actions["save"] = {
        "Save KEY file to output path (e.g., key save chitin.key)",
        [](const std::vector<std::string>& args) -> int {
            if (args.size() < 2) {
                std::cerr << "Usage: " << args[0] << " key save <keyfile> [output_path]" << std::endl;
                return 1;
            }
            std::string keyFile = args[1];
            std::string outputPath = (args.size() > 2) ? args[2] : keyFile + ".modified";
            
            KEY key(keyFile);
            if (!key.load()) {
                std::cerr << "Failed to load KEY file: " << keyFile << std::endl;
                return 1;
            }
            
            if (!key.save(outputPath)) {
                std::cerr << "Failed to save KEY file: " << outputPath << std::endl;
                return 1;
            }
            
            std::cout << "Successfully saved " << outputPath << std::endl;
            return 0;
        }
    };
    
    // Add command
    keyCommand.actions["add"] = {
        "Add a resource to KEY file using filename with extension (e.g., key add chitin.key AR0011.TIS)",
        [](const std::vector<std::string>& args) -> int {
            if (args.size() < 3) {
                std::cerr << "Usage: " << args[0] << " key add <keyfile> <filename>" << std::endl;
                std::cerr << "  keyfile: Path to the KEY file to modify" << std::endl;
                std::cerr << "  filename: Resource filename with extension (e.g., AR0011.TIS)" << std::endl;
                std::cerr << "Examples:" << std::endl;
                std::cerr << "  " << args[0] << " key add chitin.key AR0011.TIS" << std::endl;
                std::cerr << "  " << args[0] << " key add chitin.key BTNHOR.BAM" << std::endl;
                return 1;
            }
            
            std::string keyFile = args[1];
            std::string filename = args[2];
            
            // Get resource type from file extension
            size_t dotPos = filename.find_last_of('.');
            if (dotPos == std::string::npos) {
                std::cerr << "No file extension found in: " << filename << std::endl;
                return 1;
            }
            std::string extension = filename.substr(dotPos + 1);
            SClass_ID resourceType = SClass::getResourceTypeFromExtension(extension);
            if (resourceType == 0) {
                std::cerr << "Unknown or unsupported file extension in: " << filename << std::endl;
                std::cerr << "Supported extensions: 2da, acm, are, bam, bcs, bs, bif, bmp, png, chr, chu, cre, dlg, eff, gam, ids, ini, itm, mos, mus, mve, ogg, plt, pro, pvrz, sav, spl, src, sto, tis, tlk, toh, tot, ttf, var, vef, vvc, wav, wed, wfx, wmp" << std::endl;
                return 1;
            }
            
            // Load KEY file
            KEY key(keyFile);
            if (!key.load()) {
                std::cerr << "Failed to load KEY file: " << keyFile << std::endl;
                return 1;
            }
            
            std::cout << "Loaded " << keyFile << " with " << key.getResources().size() << " resources" << std::endl;
            
            // Add resource using the new method
            if (!key.addResource(filename, resourceType)) {
                std::cerr << "Failed to add resource: " << filename << std::endl;
                return 1;
            }
            
            std::cout << "Added resource: " << filename << " (type: 0x" << std::hex << resourceType << ")" << std::endl;
            
            // Save back to file
            if (!key.save(keyFile)) {
                std::cerr << "Failed to save KEY file: " << keyFile << std::endl;
                return 1;
            }
            
            std::cout << "Successfully saved " << keyFile << " with " << key.getResources().size() << " resources" << std::endl;
            return 0;
        }
    };
    
    commandTable["key"] = keyCommand;
}


// REGISTER_PLUGIN(KEY, IE_KEY_CLASS_ID);

} // namespace ProjectIE4k
