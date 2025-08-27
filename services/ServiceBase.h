#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "core/SClassID.h"

namespace ProjectIE4k {

// Optimal tile size calculation result
struct TileSize {
  int tileSize;
  int maxConcurrentTiles;
  uint64_t vramPerTile;
  uint64_t availableVRAM;
  bool isSafe;
  std::string reasoning;
};

// Service lifecycle phases
enum class ServiceLifecycle {
  // Application-level lifecycles
  APPLICATION_START,    // When app starts (Statistics, ResourceCoordinator)
  APPLICATION_SHUTDOWN, // When app shuts down

  // Batch operation lifecycles
  BATCH_EXTRACT_START,  // When batch extraction begins
  BATCH_EXTRACT_END,    // When batch extraction ends
  BATCH_UPSCALE_START,  // When batch upscaling begins (Upscaler)
  BATCH_UPSCALE_END,    // When batch upscaling ends
  BATCH_ASSEMBLE_START, // When batch assembly begins
  BATCH_ASSEMBLE_END,   // When batch assembly ends
  BATCH_COMPLETE,       // When complete pipeline (extract->upscale->assemble)
                        // finishes

  // Resource type lifecycles
  RESOURCE_TYPE_START, // When processing a new resource type
  RESOURCE_TYPE_END,   // When finished with a resource type

  // Individual resource lifecycles
  RESOURCE_START, // When processing individual resource
  RESOURCE_END,   // When finished with individual resource

  // On-demand lifecycles
  ON_DEMAND,   // Only when explicitly requested
  ON_FIRST_USE // Lazy initialization on first access
};

// Service scoping
enum class ServiceScope {
    SINGLETON,                   // One instance for entire application
    BATCH_SCOPED,               // One instance per batch operation
    RESOURCE_TYPE_SCOPED,        // One instance per resource type
    RESOURCE_SCOPED,             // One instance per resource
    OPERATION_SCOPED             // One instance per operation (extract/upscale/assemble)
};

/**
 * @brief Base interface for all services managed by PluginManager
 * 
 * Services provide shared functionality across multiple plugins
 * and are managed with resource type-specific lifecycles.
 */
class ServiceBase {
public:
    virtual ~ServiceBase() = default;
    
    /**
     * @brief Initialize the service for a specific resource type
     * @param resourceType The resource type this service will handle
     */
    virtual void initializeForResourceType(SClass_ID resourceType) = 0;
    
    /**
     * @brief Clean up the service and release resources
     */
    virtual void cleanup() = 0;
    
    /**
     * @brief Check if the service is currently initialized
     * @return true if initialized, false otherwise
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * @brief Get the current resource type this service is handling
     * @return The resource type, or 0 if not initialized
     */
    virtual SClass_ID getCurrentResourceType() const = 0;
    
    // Lifecycle management methods
    /**
     * @brief Get the lifecycle phase when this service should be initialized
     * @return The lifecycle phase
     */
    virtual ServiceLifecycle getLifecycle() const = 0;
    
    /**
     * @brief Get the scope of this service instance
     * @return The service scope
     */
    virtual ServiceScope getScope() const = 0;
    
    /**
     * @brief Check if this service should be auto-initialized
     * @return true if auto-initialize, false otherwise
     */
    virtual bool shouldAutoInitialize() const = 0;
    
    /**
     * @brief Handle lifecycle events
     * @param event The lifecycle event
     * @param context Additional context data (resource type, name, etc.)
     */
    virtual void onLifecycleEvent(ServiceLifecycle event, const std::string& context = "") = 0;
    
    /**
     * @brief Upscale a directory of images (optional - services can override if they support upscaling)
     * @param inputDir Directory containing images to upscale
     * @param outputDir Directory to save upscaled images
     * @return true if successful, false if not supported or failed
     */
    virtual bool upscaleDirectory(const std::string& inputDir, const std::string& outputDir) {
        return false; // Default implementation - not supported
    }

    /**
     * @brief Register a service factory with the service manager
     * @param serviceName Name of the service
     * @param factory Factory function to create the service
     * @return true if registered successfully
     */
    static bool registerServiceFactory(const std::string& serviceName, 
                                      std::function<std::unique_ptr<ServiceBase>()> factory);
};

// Macro for easy service registration
#define REGISTER_SERVICE(ServiceClass) \
    namespace { \
        static bool registered_##ServiceClass = ProjectIE4k::ServiceBase::registerServiceFactory( \
            #ServiceClass, \
            []() -> std::unique_ptr<ProjectIE4k::ServiceBase> { \
                return std::make_unique<ProjectIE4k::ServiceClass>(); \
            } \
        ); \
    }

} // namespace ProjectIE4k