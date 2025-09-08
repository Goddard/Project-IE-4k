#pragma once

#include "ServiceBase.h"

#include <string>
#include <functional>
#include <memory>
#include <map>
#include <vector>

#include "core/SClassID.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

/**
 * @brief Service factory function type
 */
using ServiceFactory = std::function<std::unique_ptr<ServiceBase>()>;

/**
 * @brief Unified service manager for dynamic service registration and loading
 * 
 * This class combines service registration and loading functionality.
 * Services register themselves using static initialization, making the system
 * truly dynamic without hardcoding.
 */
class ServiceManager {
public:
    /**
     * @brief Register a service factory
     * @param serviceName Name of the service
     * @param factory Factory function to create the service
     * @return true if registered successfully
     */
    static bool registerService(const std::string& serviceName, ServiceFactory factory);
    
    /**
     * @brief Create a service instance by name
     * @param serviceName Name of the service to create
     * @return Unique pointer to the service instance, or nullptr if not found
     */
    static std::unique_ptr<ServiceBase> createService(const std::string& serviceName);
    
    /**
     * @brief Get all registered service names
     * @return Vector of service names
     */
    static std::vector<std::string> getAvailableServices();
    
    /**
     * @brief Check if a service is registered
     * @param serviceName Name of the service
     * @return true if registered
     */
    static bool isServiceRegistered(const std::string& serviceName);
    
    /**
     * @brief Clear all registered services (for testing)
     */
    static void clear();
    
    /**
     * @brief Get or create a singleton service instance
     * @param serviceName Name of the service
     * @return Pointer to the service instance, or nullptr if not found
     */
    static ServiceBase* getService(const std::string& serviceName);
    
    /**
     * @brief Register a singleton service instance
     * @param serviceName Name of the service
     * @param service Unique pointer to the service instance
     */
    static void registerServiceInstance(const std::string& serviceName, std::unique_ptr<ServiceBase> service);
    
    // Lifecycle management methods
    /**
     * @brief Handle application start lifecycle
     */
    static void onApplicationStart();
    
    /**
     * @brief Handle application shutdown lifecycle
     */
    static void onApplicationShutdown();
    
    /**
     * @brief Handle batch extract start lifecycle
     */
    static void onBatchExtractStart();
    
    /**
     * @brief Handle batch extract end lifecycle
     */
    static void onBatchExtractEnd();
    
    /**
     * @brief Handle batch upscale start lifecycle
     */
    static void onBatchUpscaleStart();
    
    /**
     * @brief Handle batch upscale end lifecycle
     */
    static void onBatchUpscaleEnd();
    
    /**
     * @brief Handle batch assemble start lifecycle
     */
    static void onBatchAssembleStart();
    
    /**
     * @brief Handle batch assemble end lifecycle
     */
    static void onBatchAssembleEnd();

    /**
     * @brief Handle batch complete lifecycle
     */
    static void onBatchComplete();

    /**
     * @brief Handle resource type start lifecycle
     * @param resourceType The resource type being processed
     */
    static void onResourceTypeStart(SClass_ID resourceType);
    
    /**
     * @brief Handle resource type end lifecycle
     * @param resourceType The resource type being processed
     */
    static void onResourceTypeEnd(SClass_ID resourceType);
    
    /**
     * @brief Handle resource start lifecycle
     * @param resourceName The resource name being processed
     * @param resourceType The resource type being processed
     */
    static void onResourceStart(const std::string& resourceName, SClass_ID resourceType);
    
    /**
     * @brief Handle resource end lifecycle
     * @param resourceName The resource name being processed
     * @param resourceType The resource type being processed
     */
    static void onResourceEnd(const std::string& resourceName, SClass_ID resourceType);

    /**
     * @brief Register service commands to the command table
     * @param commandTable The command table to register commands to
     */
    static void registerCommands(CommandTable& commandTable);

    /**
     * @brief List all resources in the index
     * @return true if successful
     */
    static bool listAllResources();

private:
    static std::map<std::string, ServiceFactory>& getRegistry();
    static std::map<std::string, std::unique_ptr<ServiceBase>>& getInstances();
    
    // Lifecycle management helpers
    static void initializeServicesByLifecycle(ServiceLifecycle lifecycle);
    static void cleanupServicesByLifecycle(ServiceLifecycle lifecycle);
    static void cleanupServicesByScope(ServiceScope scope);
    static void notifyServicesOfEvent(ServiceLifecycle event, const std::string& context = "");
};

} // namespace ProjectIE4k
