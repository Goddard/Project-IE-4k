#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <functional>

#include <png.h>

#include "CommandRegistry.h"
#include "PluginManager.h"
#include "core/SClassID.h"

namespace ProjectIE4k {

// Macro for auto-registering plugins
#define REGISTER_PLUGIN(PluginClass, ResourceType) \
    namespace { \
        struct PluginClass##_registrar { \
            PluginClass##_registrar() { \
                Log(MESSAGE, "PluginBase", "Auto-registering plugin: " #PluginClass " for resource type: " #ResourceType); \
                PluginBase::registerPluginFactory(ResourceType, \
                    [](const std::string& resourceName) -> std::unique_ptr<PluginBase> { \
                        return std::make_unique<PluginClass>(resourceName); \
                    }); \
                /* Register commands statically */ \
                PluginClass::registerCommands(PluginBase::getCommandRegistry()); \
            } \
        }; \
        static PluginClass##_registrar PluginClass##_instance; \
    } \
    /* Force the static variable to be used to prevent optimization */ \
    static int PluginClass##_force_init = (PluginClass##_instance, 0)

/**
 * @brief Abstract base class for all ProjectIE4k plugins
 * 
 * This provides a consistent interface for all resource processing plugins.
 * Each plugin handles a specific resource type and manages its own paths.
 */
class PluginBase {
public:
    PluginBase(const std::string& resourceName, SClass_ID resourceType);
    virtual ~PluginBase();
    
    // Core operations that all plugins must implement
    virtual bool extract() = 0;
    virtual bool upscale();  // Default implementation uses UpscalerService
    virtual bool assemble() = 0;
    
    // Shared resource management for batch operations
    virtual bool initializeSharedResources() { return true; }
    virtual void cleanupSharedResources() {}
    virtual bool hasSharedResources() const { return false; }
    
    // Static command registry
    static CommandTable& getCommandRegistry();
    
    // Register plugin commands (static method to avoid instantiation)
    static void registerCommands(CommandTable& commandTable) {}
    
    // Common getters
    virtual std::string getResourceName() const = 0;
    virtual bool isValid() const = 0;
    
    // Batch operations (implemented by PluginManager)
    virtual bool extractAll() = 0;
    virtual bool upscaleAll() = 0;
    virtual bool assembleAll() = 0;
    
    // Clean directories before operations - operation-specific
    virtual bool cleanExtractDirectory() = 0;
    virtual bool cleanUpscaleDirectory() = 0;
    virtual bool cleanAssembleDirectory() = 0;
    
    // Plugin metadata
    virtual std::string getPluginName() const = 0;
    virtual SClass_ID getResourceType() const { return 0; }  // Default implementation to avoid pure virtual call in constructor
    
    // Path management - each plugin manages its own paths
    virtual std::string getOutputDir(bool ensureDir = true) const = 0;
    virtual std::string getExtractDir(bool ensureDir = true) const = 0;
    virtual std::string getUpscaledDir(bool ensureDir = true) const = 0;
    virtual std::string getAssembleDir(bool ensureDir = true) const = 0;
    
    // Static registration helper - plugins can use this in their ctor
    static void registerPluginFactory(SClass_ID resourceType, 
                                    std::function<std::unique_ptr<PluginBase>(const std::string&)> factory);
    
protected:
    // these variables need to be set during plugin construction
    std::string resourceName_;
    bool valid_ = false;
    // filename includes extension
    std::string originalFileName = "";
    std::string originalExtension = "";
    std::vector<uint8_t> originalFileData;
    std::vector<uint8_t> colorPaletteData;
    ////////////////////////

    // Overloaded version to load specific resource types (for plugins that need multiple resources)
    std::vector<uint8_t> loadResourceFromService(const std::string& resourceName, SClass_ID resourceType);
    
    // Helper methods that plugins can use
    bool validateResourceName() const;
    void logOperation(const std::string& operation, bool success) const;
    
    // Common path construction helpers
    std::string constructPath(const std::string& suffix, bool ensureDir = true) const;
    std::string constructExtractPath(const std::string& suffix, bool ensureDir) const;
    std::string extractBaseName() const;
    std::string getOriginalExtension(std::string &filename);

    // Common directory creation
    void ensureDirectoryExists(const std::string& path) const;
    bool createDirectory(const std::string& path) const;
    
    // PNG loading and saving functions
    bool loadPNG(const std::string& filename, std::vector<uint32_t>& pixels, int& width, int& height) const;
    bool savePNG(const std::string& filename, const std::vector<uint32_t>& pixels, int width, int height) const;

    // Stream PNG rows with minimal memory usage. Calls onRow for each row (ARGB per pixel)
    bool loadPNGRows(const std::string &filename,
                     const std::function<bool(int width, int height, int rowIndex, const std::vector<uint32_t> &argbRow)>
                         &onRow) const;
};

} // namespace ProjectIE4k 