#include "ServiceManager.h"

#include <mutex>

#include "core/Logging/Logging.h"

namespace ProjectIE4k {

std::map<std::string, ServiceFactory>& ServiceManager::getRegistry() {
    static std::map<std::string, ServiceFactory> registry;
    return registry;
}

bool ServiceManager::registerService(const std::string& serviceName, ServiceFactory factory) {
    static std::mutex registryMutex;
    std::lock_guard<std::mutex> lock(registryMutex);
    
    auto& registry = getRegistry();
    
    if (registry.find(serviceName) != registry.end()) {
        Log(WARNING, "ServiceManager", "Service {} already registered, skipping", serviceName);
        return false;
    }
    
    registry[serviceName] = factory;
    Log(DEBUG, "ServiceManager", "Dynamically registered service: {}", serviceName);
    return true;
}

std::unique_ptr<ServiceBase> ServiceManager::createService(const std::string& serviceName) {
    static std::mutex registryMutex;
    std::lock_guard<std::mutex> lock(registryMutex);
    
    auto& registry = getRegistry();
    auto it = registry.find(serviceName);
    if (it == registry.end()) {
        Log(ERROR, "ServiceManager", "Service not found: {}", serviceName);
        return nullptr;
    }
    
    try {
        return it->second();
    } catch (const std::exception& e) {
        Log(ERROR, "ServiceManager", "Failed to create service {}: {}", serviceName, e.what());
        return nullptr;
    }
}

std::vector<std::string> ServiceManager::getAvailableServices() {
    static std::mutex registryMutex;
    std::lock_guard<std::mutex> lock(registryMutex);
    
    auto& registry = getRegistry();
    std::vector<std::string> services;
    services.reserve(registry.size());
    
    for (const auto& [name, factory] : registry) {
        services.push_back(name);
    }
    
    return services;
}

bool ServiceManager::isServiceRegistered(const std::string& serviceName) {
    static std::mutex registryMutex;
    std::lock_guard<std::mutex> lock(registryMutex);
    auto& registry = getRegistry();
    return registry.find(serviceName) != registry.end();
}

void ServiceManager::clear() {
    static std::mutex registryMutex;
    std::lock_guard<std::mutex> lock(registryMutex);
    auto& registry = getRegistry();
    registry.clear();
    Log(DEBUG, "ServiceManager", "Cleared all registered services");
}

std::map<std::string, std::unique_ptr<ServiceBase>>& ServiceManager::getInstances() {
    static std::map<std::string, std::unique_ptr<ServiceBase>> instances;
    return instances;
}

ServiceBase* ServiceManager::getService(const std::string& serviceName) {
    static std::mutex registryMutex;
    std::lock_guard<std::mutex> lock(registryMutex);
    
    auto& instances = getInstances();
    
    // Check if service instance already exists
    auto it = instances.find(serviceName);
    if (it != instances.end()) {
        return it->second.get();
    }
    
    // Try to create a new service instance
    auto newService = createService(serviceName);
    if (newService) {
        auto* servicePtr = newService.get();
        instances[serviceName] = std::move(newService);
        Log(DEBUG, "ServiceManager", "Created singleton service instance: {}", serviceName);
        return servicePtr;
    }
    
    Log(ERROR, "ServiceManager", "Failed to create service instance: {}", serviceName);
    return nullptr;
}

void ServiceManager::registerServiceInstance(const std::string& serviceName, std::unique_ptr<ServiceBase> service) {
    static std::mutex registryMutex;
    std::lock_guard<std::mutex> lock(registryMutex);
    
    auto& instances = getInstances();
    
    if (instances.find(serviceName) != instances.end()) {
        Log(WARNING, "ServiceManager", "Service instance {} already registered, replacing", serviceName);
    }
    
    instances[serviceName] = std::move(service);
    Log(DEBUG, "ServiceManager", "Registered service instance: {}", serviceName);
}

// Lifecycle management methods
void ServiceManager::onApplicationStart() {
    Log(DEBUG, "ServiceManager", "Application start lifecycle triggered");
    initializeServicesByLifecycle(ServiceLifecycle::APPLICATION_START);
    notifyServicesOfEvent(ServiceLifecycle::APPLICATION_START);
}

void ServiceManager::onApplicationShutdown() {
    Log(DEBUG, "ServiceManager", "Application shutdown lifecycle triggered");
    notifyServicesOfEvent(ServiceLifecycle::APPLICATION_SHUTDOWN);
    cleanupServicesByScope(ServiceScope::SINGLETON);
}

void ServiceManager::onBatchExtractStart() {
    Log(DEBUG, "ServiceManager", "Batch extract start lifecycle triggered");
    initializeServicesByLifecycle(ServiceLifecycle::BATCH_EXTRACT_START);
    notifyServicesOfEvent(ServiceLifecycle::BATCH_EXTRACT_START);
}

void ServiceManager::onBatchExtractEnd() {
    Log(DEBUG, "ServiceManager", "Batch extract end lifecycle triggered");
    notifyServicesOfEvent(ServiceLifecycle::BATCH_EXTRACT_END);
    cleanupServicesByScope(ServiceScope::BATCH_SCOPED);
}

void ServiceManager::onBatchUpscaleStart() {
    Log(DEBUG, "ServiceManager", "Batch upscale start lifecycle triggered");
    initializeServicesByLifecycle(ServiceLifecycle::BATCH_UPSCALE_START);
    notifyServicesOfEvent(ServiceLifecycle::BATCH_UPSCALE_START);
}

void ServiceManager::onBatchUpscaleEnd() {
    Log(DEBUG, "ServiceManager", "Batch upscale end lifecycle triggered");
    notifyServicesOfEvent(ServiceLifecycle::BATCH_UPSCALE_END);
    cleanupServicesByScope(ServiceScope::BATCH_SCOPED);
}

void ServiceManager::onBatchAssembleStart() {
    Log(DEBUG, "ServiceManager", "Batch assemble start lifecycle triggered");
    initializeServicesByLifecycle(ServiceLifecycle::BATCH_ASSEMBLE_START);
    notifyServicesOfEvent(ServiceLifecycle::BATCH_ASSEMBLE_START);
}

void ServiceManager::onBatchAssembleEnd() {
    Log(DEBUG, "ServiceManager", "Batch assemble end lifecycle triggered");
    notifyServicesOfEvent(ServiceLifecycle::BATCH_ASSEMBLE_END);
    cleanupServicesByScope(ServiceScope::BATCH_SCOPED);
}

void ServiceManager::onBatchComplete() {
  Log(DEBUG, "ServiceManager", "Batch complete lifecycle triggered");
  notifyServicesOfEvent(ServiceLifecycle::BATCH_COMPLETE);
}

void ServiceManager::onResourceTypeStart(SClass_ID resourceType) {
    Log(DEBUG, "ServiceManager", "Resource type start lifecycle triggered for type: {}", resourceType);
    notifyServicesOfEvent(ServiceLifecycle::RESOURCE_TYPE_START, std::to_string(resourceType));
}

void ServiceManager::onResourceTypeEnd(SClass_ID resourceType) {
    Log(DEBUG, "ServiceManager", "Resource type end lifecycle triggered for type: {}", resourceType);
    notifyServicesOfEvent(ServiceLifecycle::RESOURCE_TYPE_END, std::to_string(resourceType));
    cleanupServicesByScope(ServiceScope::RESOURCE_TYPE_SCOPED);
}

void ServiceManager::onResourceStart(const std::string& resourceName, SClass_ID resourceType) {
    Log(DEBUG, "ServiceManager", "Resource start lifecycle triggered for: {} (type: {})", resourceName, resourceType);
    notifyServicesOfEvent(ServiceLifecycle::RESOURCE_START, resourceName + ":" + std::to_string(resourceType));
}

void ServiceManager::onResourceEnd(const std::string& resourceName, SClass_ID resourceType) {
    Log(DEBUG, "ServiceManager", "Resource end lifecycle triggered for: {} (type: {})", resourceName, resourceType);
    notifyServicesOfEvent(ServiceLifecycle::RESOURCE_END, resourceName + ":" + std::to_string(resourceType));
    cleanupServicesByScope(ServiceScope::RESOURCE_SCOPED);
}

// Private lifecycle management helpers
void ServiceManager::initializeServicesByLifecycle(ServiceLifecycle lifecycle) {
    static std::mutex registryMutex;
    std::lock_guard<std::mutex> lock(registryMutex);
    
    auto& registry = getRegistry();
    auto& instances = getInstances();
    
    for (const auto& [serviceName, factory] : registry) {
        // Create a temporary instance to check its lifecycle
        auto tempService = factory();
        if (tempService && tempService->getLifecycle() == lifecycle && tempService->shouldAutoInitialize()) {
            // Check if we already have an instance
            if (instances.find(serviceName) == instances.end()) {
                instances[serviceName] = factory();
                Log(DEBUG, "ServiceManager", "Auto-initialized service: {} for lifecycle: {}", serviceName, static_cast<int>(lifecycle));
            }
        }
    }
}

void ServiceManager::cleanupServicesByLifecycle(ServiceLifecycle lifecycle) {
    static std::mutex registryMutex;
    std::lock_guard<std::mutex> lock(registryMutex);
    
    auto& instances = getInstances();
    
    for (auto it = instances.begin(); it != instances.end();) {
        if (it->second && it->second->getLifecycle() == lifecycle) {
            Log(DEBUG, "ServiceManager", "Cleaning up service: {} for lifecycle: {}", it->first, static_cast<int>(lifecycle));
            it->second->cleanup();
            it = instances.erase(it);
        } else {
            ++it;
        }
    }
}

void ServiceManager::cleanupServicesByScope(ServiceScope scope) {
    static std::mutex registryMutex;
    std::lock_guard<std::mutex> lock(registryMutex);
    
    auto& instances = getInstances();
    
    for (auto it = instances.begin(); it != instances.end();) {
        if (it->second && it->second->getScope() == scope) {
            Log(DEBUG, "ServiceManager", "Cleaning up service: {} for scope: {}", it->first, static_cast<int>(scope));
            it->second->cleanup();
            it = instances.erase(it);
        } else {
            ++it;
        }
    }
}

void ServiceManager::notifyServicesOfEvent(ServiceLifecycle event, const std::string& context) {
    static std::mutex registryMutex;
    std::lock_guard<std::mutex> lock(registryMutex);
    
    auto& instances = getInstances();
    
    for (auto& [serviceName, service] : instances) {
        if (service) {
            try {
                service->onLifecycleEvent(event, context);
            } catch (const std::exception& e) {
                Log(ERROR, "ServiceManager", "Error in service {} during lifecycle event {}: {}", 
                    serviceName, static_cast<int>(event), e.what());
            }
        }
    }
}

} // namespace ProjectIE4k 