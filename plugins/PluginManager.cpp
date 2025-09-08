#include "PluginManager.h"

#include <chrono>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>

#include "PluginBase.h"
#include "core/CFG.h"
#include "core/GlobalContext.h"
#include "core/Logging/Logging.h"
#include "core/OperationsMonitor/OperationsMonitor.h"
#include "core/SClassID.h"
#include "services/ResourceService/ResourceCoordinatorService.h"
#include "services/ResourceService/ResourceTypes.h"
#include "services/ResourceService/KEYService.h"
#include "services/StatisticsService/StatisticsService.h"
#include "services/ServiceManager.h"
#include "services/OperationsTrackerService/OperationsTrackerService.h"
#include "core/Rules/RulesEngine.h"

namespace ProjectIE4k {

    namespace fs = std::filesystem;

PluginManager::PluginManager() : currentBatchResourceType_(0) { }

PluginManager::~PluginManager() {}

bool PluginManager::extractAllResources() {
    Log(DEBUG, "PluginManager", "Starting extraction of all resources across all types");
    resetBatchStats();
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (const auto& [resourceType, factory] : pluginFactories_) {
        if (!extractAllResourcesOfType(resourceType)) {
            Log(WARNING, "PluginManager", "Some extractions failed for resource type: {}", resourceType);
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    lastBatchStats_.totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    logBatchResults("extraction", lastBatchStats_);
    return lastBatchStats_.successfulOperations > 0;
}

bool PluginManager::upscaleAllResources() {
    Log(DEBUG, "PluginManager", "Starting upscaling of all resources across all types");
    resetBatchStats();
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (const auto& [resourceType, factory] : pluginFactories_) {
        if (!upscaleAllResourcesOfType(resourceType)) {
            Log(WARNING, "PluginManager", "Some upscaling failed for resource type: {}", resourceType);
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    lastBatchStats_.totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    logBatchResults("upscaling", lastBatchStats_);
    return lastBatchStats_.successfulOperations > 0;
}

bool PluginManager::assembleAllResources() {
    Log(DEBUG, "PluginManager", "Starting assembly of all resources across all types");
    resetBatchStats();
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (const auto& [resourceType, factory] : pluginFactories_) {
        if (!assembleAllResourcesOfType(resourceType)) {
            Log(WARNING, "PluginManager", "Some assembly failed for resource type: {}", resourceType);
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    lastBatchStats_.totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    logBatchResults("assembly", lastBatchStats_);
    return lastBatchStats_.successfulOperations > 0;
}

bool PluginManager::completeAllResources() {
  Log(DEBUG, "PluginManager",
      "Starting complete pipeline: sync -> extract -> upscale -> assemble -> "
      "transfer for all resources");
  resetBatchStats();

  auto startTime = std::chrono::high_resolution_clock::now();
  // Step 0: Sync override
  Log(MESSAGE, "PluginManager", "=== Step 0: Sync Override ===");
  if (!syncOverrideAssets()) {
    Log(WARNING, "PluginManager", "Sync Override failed, ensure your paths are correct");
  }

    // Step 1: Extract all resources
    Log(MESSAGE, "PluginManager", "=== Step 1: Extracting all resources ===");
    bool extractSuccess = extractAllResources();
    if (!extractSuccess) {
        Log(WARNING, "PluginManager", "Some extractions failed, but continuing with upscaling and assembly");
    }
    
    // Step 2: Upscale all resources
    Log(MESSAGE, "PluginManager", "=== Step 2: Upscaling all resources ===");
    bool upscaleSuccess = upscaleAllResources();
    if (!upscaleSuccess) {
        Log(WARNING, "PluginManager", "Some upscaling failed, but continuing with assembly");
    }
    
    // Step 3: Assemble all resources
    Log(MESSAGE, "PluginManager", "=== Step 3: Assembling all resources ===");
    bool assembleSuccess = assembleAllResources();
    if (!assembleSuccess) {
        Log(WARNING, "PluginManager", "Some assembly failed");
    }

    // Step Z: transfer override
    Log(MESSAGE, "PluginManager",
        "=== Step Z: Transfer assembled to override ===");
    if (!transferAssembledAssetsToOverride()) {
      Log(WARNING, "PluginManager",
          "Transfer failed, ensure your paths are correct");
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    lastBatchStats_.totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    logBatchResults("complete pipeline", lastBatchStats_);
    
    // Flush logging system to ensure all messages are written before statistics
    FlushLogs();

    // Trigger batch complete lifecycle event (StatisticsService will generate
    // summary)
    ServiceManager::onBatchComplete();

    // Return true if at least one operation succeeded in each phase
    return extractSuccess || upscaleSuccess || assembleSuccess;
}

bool PluginManager::completeAllResourcesOfType(SClass_ID resourceType) {
    std::string typeName = SClass::getExtension(resourceType);
    Log(DEBUG, "PluginManager",
        "Starting complete pipeline for type {}: extract -> upscale -> assemble -> transfer", typeName);
    resetBatchStats();

    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Step 1: Extract all resources of this type
    Log(MESSAGE, "PluginManager", "=== Step 1: Extracting all {} resources ===", typeName);
    bool extractSuccess = extractAllResourcesOfType(resourceType);
    if (!extractSuccess) {
        Log(WARNING, "PluginManager", "Some {} extractions failed, but continuing with upscaling and assembly", typeName);
    }
    
    // Step 2: Upscale all resources of this type
    Log(MESSAGE, "PluginManager", "=== Step 2: Upscaling all {} resources ===", typeName);
    bool upscaleSuccess = upscaleAllResourcesOfType(resourceType);
    if (!upscaleSuccess) {
        Log(WARNING, "PluginManager", "Some {} upscaling failed, but continuing with assembly", typeName);
    }
    
    // Step 3: Assemble all resources of this type
    Log(MESSAGE, "PluginManager", "=== Step 3: Assembling all {} resources ===", typeName);
    bool assembleSuccess = assembleAllResourcesOfType(resourceType);
    if (!assembleSuccess) {
        Log(WARNING, "PluginManager", "Some {} assembly failed, but continuing with transfer", typeName);
    }

    // Step 4: Transfer assembled assets of this type to override
    Log(MESSAGE, "PluginManager", "=== Step 4: Transferring {} assembled assets to override ===", typeName);
    bool transferSuccess = transferByResourceType(resourceType);
    if (!transferSuccess) {
        Log(WARNING, "PluginManager", "Some {} transfer failed", typeName);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    lastBatchStats_.totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    logBatchResults("complete pipeline for " + typeName, lastBatchStats_);
    
    // Flush logging system to ensure all messages are written before statistics
    FlushLogs();

    // Trigger batch complete lifecycle event (StatisticsService will generate summary)
    ServiceManager::onBatchComplete();

    // Return true if at least one operation succeeded in each phase
    return extractSuccess || upscaleSuccess || assembleSuccess || transferSuccess;
}

bool PluginManager::extractAllResourcesOfType(SClass_ID resourceType) {
    // Ensure plugin is registered before proceeding
    ensurePluginRegistered(resourceType);
    
    auto it = pluginFactories_.find(resourceType);
    if (it == pluginFactories_.end()) {
        Log(ERROR, "PluginManager", "No plugin registered for resource type: {}", resourceType);
        return false;
    }
    
    // Initialize shared resources for this resource type before batch processing
    ensureSharedResourcesInitialized(resourceType);
    
    // Reset thread count to starting value for extraction operations
    auto &monitor = OperationsMonitor::getInstance();
    // Thread scaling is now handled automatically by OperationsMonitor

    // Trigger batch extract start lifecycle
    ServiceManager::onBatchExtractStart();
    
    // Start batch lifecycle for this resource type
    onBatchTypeStart(resourceType);
    
    // Skip entire phase if .done marker exists and not forcing
    {
        auto* opsTracker = dynamic_cast<OperationsTrackerService*>(ServiceManager::getService("OperationsTrackerService"));
        if (opsTracker && !opsTracker->shouldProcessPhase("extract", SClass::getExtension(resourceType))) {
            ServiceManager::onBatchExtractEnd();
            onBatchTypeEnd(resourceType);
            return true;
        }
    }

    // Get all resources of this type from our resource service
    auto allResources = listResourcesByType(resourceType);
    Log(DEBUG, "PluginManager", "Found {} resources of type {}", allResources.size(), resourceType);
    
    // Load rules once (fail-open if missing)
    RulesEngine::getInstance().load();
    
    // Filter resources based on rules before counting for statistics
    std::vector<std::string> resources;
    for (const auto& resource : allResources) {
        std::string resourceName = resource.c_str();
        const char* typeStr = SClass::getExtension(resourceType);
        if (RulesEngine::getInstance().shouldProcess("extract", typeStr, resourceName)) {
            resources.push_back(resource);
        } else {
            Log(DEBUG, "PluginManager", "Rules: skipping extract for {} ({})", resourceName, typeStr);
        }
    }
    
    Log(MESSAGE, "PluginManager", "After rules filtering: {} {} resources to extract", resources.size(), SClass::getExtension(resourceType));
    
    if (resources.empty()) {
        Log(WARNING, "PluginManager", "No resources to extract after filtering for type {}", resourceType);
        onBatchTypeEnd(resourceType);
        ServiceManager::onBatchExtractEnd();
        return true; // Not an error, just no resources
    }
    
    // Start statistics tracking with filtered count
    std::string processName = std::string("extract_") + SClass::getExtension(resourceType);
    auto* statsService = dynamic_cast<StatisticsService*>(ServiceManager::getService("StatisticsService"));
    if (statsService) {
        statsService->startProcess(processName, SClass::getExtension(resourceType), resources.size());
    }
    // Operations tracker
    auto* opsTracker = dynamic_cast<OperationsTrackerService*>(ServiceManager::getService("OperationsTrackerService"));
    if (opsTracker) {
        opsTracker->startPhase("extract", SClass::getExtension(resourceType), resources.size());
    }
    
    // Initialize OperationsMonitor if needed
    if (!monitor.isInitialized()) {
        monitor.initialize();
    }
    
    // Submit all tasks to the thread pool
    std::vector<std::future<bool>> futures;
    std::atomic<bool> overallSuccess{true};
    
    // Load rules once (fail-open if missing)
    RulesEngine::getInstance().load();

    for (const auto& resourceName : resources) {
        std::string resourceNameStr = resourceName.c_str();
        const char* typeStr = SClass::getExtension(resourceType);
        if (!RulesEngine::getInstance().shouldProcess("extract", typeStr, resourceNameStr)) {
            Log(DEBUG, "PluginManager", "Rules: skipping extract for {} ({})", resourceNameStr, typeStr);
            if (statsService) { statsService->incrementProcessed(processName, true); }
            continue;
        }
        
        // Build fingerprint and skip if already processed
        OperationsTrackerService::InputFingerprint fp = makeInputFingerprint(resourceNameStr, resourceType, "extract_v1");
        if (opsTracker && !opsTracker->shouldProcess("extract", SClass::getExtension(resourceType), resourceNameStr, fp, false)) {
            if (statsService) {
                statsService->incrementProcessed(processName, true);
            }
            continue;
        }
        
        if (opsTracker) {
            opsTracker->markStarted("extract", SClass::getExtension(resourceType), resourceNameStr, fp);
        }

        // Create operation requirements for extraction
        OperationRequirements requirements;
        requirements.operationType = "extract";
        requirements.resourceName = resourceNameStr;
        requirements.startingThreadCount = std::thread::hardware_concurrency();

        // Submit task to thread pool
        auto future = monitor.submitTaskWithRequirements(
            [this, resourceNameStr, resourceType, statsService, processName, &overallSuccess, opsTracker]() -> bool {
                Log(DEBUG, "PluginManager", "Processing resource: {}", resourceNameStr);
                
                bool resourceSuccess = extractResource(resourceNameStr, resourceType, false); // Disable stats in batch mode
                if (statsService) {
                    statsService->incrementProcessed(processName, resourceSuccess);
                }
                
                if (!resourceSuccess) {
                    Log(ERROR, "PluginManager", "Failed to extract {}", resourceNameStr);
                    if (statsService) {
                        statsService->recordError(processName, resourceNameStr);
                    }
                    overallSuccess = false;
                }
                if (opsTracker) {
                    opsTracker->markCompleted("extract", SClass::getExtension(resourceType), resourceNameStr, resourceSuccess, {});
                }
                
                return resourceSuccess;
            },
            requirements,
            "extract_" + resourceNameStr
        );
        
        futures.push_back(std::move(future));
    }
    
    // Wait for all tasks to complete
    for (auto& future : futures) {
        try {
            future.get(); // This will throw if the task failed
        } catch (const std::exception& e) {
            Log(ERROR, "PluginManager", "Task failed with exception: {}", e.what());
            overallSuccess = false;
        }
    }
    
    bool success = overallSuccess.load();
    
    // End statistics tracking
    if (statsService) {
        statsService->endProcess(processName);
    }
    if (opsTracker) {
        opsTracker->endPhase("extract", SClass::getExtension(resourceType), success);
    }
    
    // End batch lifecycle for this resource type
    onBatchTypeEnd(resourceType);
    
    // Trigger batch extract end lifecycle
    ServiceManager::onBatchExtractEnd();
    
    // Flush logging system to ensure all messages are written
    FlushLogs();
    
    return success;
}

bool PluginManager::upscaleAllResourcesOfType(SClass_ID resourceType) {
    // Ensure plugin is registered before proceeding
    ensurePluginRegistered(resourceType);
    
    auto it = pluginFactories_.find(resourceType);
    if (it == pluginFactories_.end()) {
        Log(ERROR, "PluginManager", "No plugin registered for resource type: {}", resourceType);
        return false;
    }
    
    // Initialize shared resources for this resource type before batch processing
    ensureSharedResourcesInitialized(resourceType);
    
    // Trigger batch upscale start lifecycle
    ServiceManager::onBatchUpscaleStart();
    
    // Start batch lifecycle for this resource type
    onBatchTypeStart(resourceType);
    
    // Skip entire phase if .done marker exists and not forcing
    {
        auto* opsTracker = dynamic_cast<OperationsTrackerService*>(ServiceManager::getService("OperationsTrackerService"));
        if (opsTracker && !opsTracker->shouldProcessPhase("upscale", SClass::getExtension(resourceType))) {
            ServiceManager::onBatchUpscaleEnd();
            onBatchTypeEnd(resourceType);
            return true;
        }
    }

    // Get all resources of this type from our resource service
    auto allResources = listResourcesByType(resourceType);
    Log(DEBUG, "PluginManager", "Found {} resources of type {}", allResources.size(), resourceType);
    
    // Load rules once (fail-open if missing)
    RulesEngine::getInstance().load();
    
    // Filter resources based on rules before counting for statistics
    std::vector<std::string> resources;
    for (const auto& resource : allResources) {
        std::string resourceName = resource.c_str();
        const char* typeStr = SClass::getExtension(resourceType);
        if (RulesEngine::getInstance().shouldProcess("upscale", typeStr, resourceName)) {
            resources.push_back(resource);
        } else {
            Log(DEBUG, "PluginManager", "Rules: skipping upscale for {} ({})", resourceName, typeStr);
        }
    }
    
    Log(MESSAGE, "PluginManager", "After rules filtering: {} {} resources to upscale", resources.size(), SClass::getExtension(resourceType));
    
    if (resources.empty()) {
        Log(WARNING, "PluginManager", "No resources to upscale after filtering for type {}", resourceType);
        onBatchTypeEnd(resourceType);
        ServiceManager::onBatchUpscaleEnd();
        return true; // Not an error, just no resources
    }
    
    // Start statistics tracking with filtered count
    std::string processName = std::string("upscale_") + SClass::getExtension(resourceType);
    auto* statsService = dynamic_cast<StatisticsService*>(ServiceManager::getService("StatisticsService"));
    if (statsService) {
        statsService->startProcess(processName, SClass::getExtension(resourceType), resources.size());
    }

    auto* opsTracker = dynamic_cast<OperationsTrackerService*>(ServiceManager::getService("OperationsTrackerService"));
    if (opsTracker) {
        opsTracker->startPhase("upscale", SClass::getExtension(resourceType), resources.size());
    }
    
    bool success = true;

    for (const auto& resourceName : resources) {
        std::string resourceNameStr = resourceName.c_str();
        Log(DEBUG, "PluginManager", "Processing resource: {}", resourceNameStr);
        
        OperationsTrackerService::InputFingerprint fp = makeInputFingerprint(resourceNameStr, resourceType, "upscale_v1");
        if (opsTracker && !opsTracker->shouldProcess("upscale", SClass::getExtension(resourceType), resourceNameStr, fp, false)) {
            if (statsService) { statsService->incrementProcessed(processName, true); }
            continue;
        }
        
        if (opsTracker) {
            opsTracker->markStarted("upscale", SClass::getExtension(resourceType), resourceNameStr, fp);
        }

        bool resourceSuccess = upscaleResource(resourceNameStr, resourceType, false); // Disable stats in batch mode
        if (statsService) {
            statsService->incrementProcessed(processName, resourceSuccess);
        }
        
        if (!resourceSuccess) {
            Log(ERROR, "PluginManager", "Failed to upscale {}", resourceNameStr);
            if (statsService) {
                statsService->recordError(processName, resourceNameStr);
            }
            success = false;
        }
        if (opsTracker) {
            opsTracker->markCompleted("upscale", SClass::getExtension(resourceType), resourceNameStr, resourceSuccess, {});
        }
    }
    
    // End statistics tracking
    if (statsService) {
        statsService->endProcess(processName);
    }
    
    if (opsTracker) {
        opsTracker->endPhase("upscale", SClass::getExtension(resourceType), success);
    }
    
    // End batch lifecycle for this resource type
    onBatchTypeEnd(resourceType);
    
    // Trigger batch upscale end lifecycle
    ServiceManager::onBatchUpscaleEnd();
    
    // Flush logging system to ensure all messages are written
    FlushLogs();
    
    return success;
}

bool PluginManager::assembleAllResourcesOfType(SClass_ID resourceType) {
    // Ensure plugin is registered before proceeding
    ensurePluginRegistered(resourceType);
    
    auto it = pluginFactories_.find(resourceType);
    if (it == pluginFactories_.end()) {
        Log(ERROR, "PluginManager", "No plugin registered for resource type: {}", resourceType);
        return false;
    }
    
    // Initialize shared resources for this resource type before batch processing
    ensureSharedResourcesInitialized(resourceType);
    
    // Trigger batch assemble start lifecycle
    ServiceManager::onBatchAssembleStart();
    
    // Start batch lifecycle for this resource type
    onBatchTypeStart(resourceType);
    
    // Skip entire phase if .done marker exists and not forcing
    {
        auto* opsTracker = dynamic_cast<OperationsTrackerService*>(ServiceManager::getService("OperationsTrackerService"));
        if (opsTracker && !opsTracker->shouldProcessPhase("assemble", SClass::getExtension(resourceType))) {
            ServiceManager::onBatchAssembleEnd();
            onBatchTypeEnd(resourceType);
            return true;
        }
    }

    // Get all resources of this type from our resource service
    auto allResources = listResourcesByType(resourceType);
    Log(DEBUG, "PluginManager", "Found {} resources of type {}", allResources.size(), resourceType);
    
    // Load rules once (fail-open if missing)
    RulesEngine::getInstance().load();
    
    // Filter resources based on rules before counting for statistics
    std::vector<std::string> resources;
    for (const auto& resource : allResources) {
        std::string resourceName = resource.c_str();
        const char* typeStr = SClass::getExtension(resourceType);
        if (RulesEngine::getInstance().shouldProcess("assemble", typeStr, resourceName)) {
            resources.push_back(resource);
        } else {
            Log(DEBUG, "PluginManager", "Rules: skipping assemble for {} ({})", resourceName, typeStr);
        }
    }
    
    Log(MESSAGE, "PluginManager", "After rules filtering: {} {} resources to assemble", resources.size(), SClass::getExtension(resourceType));
    
    if (resources.empty()) {
        Log(WARNING, "PluginManager", "No resources to assemble after filtering for type {}", resourceType);
        onBatchTypeEnd(resourceType);
        ServiceManager::onBatchAssembleEnd();
        return true; // Not an error, just no resources
    }
    
    // Start statistics tracking with filtered count
    std::string processName = std::string("assemble_") + SClass::getExtension(resourceType);
    auto* statsService = dynamic_cast<StatisticsService*>(ServiceManager::getService("StatisticsService"));
    if (statsService) {
        statsService->startProcess(processName, SClass::getExtension(resourceType), resources.size());
    }
    auto* opsTracker = dynamic_cast<OperationsTrackerService*>(ServiceManager::getService("OperationsTrackerService"));
    if (opsTracker) {
        opsTracker->startPhase("assemble", SClass::getExtension(resourceType), resources.size());
    }

    // Initialize OperationsMonitor if needed
    auto &monitor = OperationsMonitor::getInstance();
    if (!monitor.isInitialized()) {
      monitor.initialize();
    }

    // Submit all assembly tasks to thread pool
    std::vector<std::future<bool>> futures;
    std::atomic<bool> overallSuccess{true};

    // Load rules once (fail-open if missing)
    RulesEngine::getInstance().load();

    for (const auto& resourceName : resources) {
        std::string resourceNameStr = resourceName.c_str();
        const char* typeStr = SClass::getExtension(resourceType);
        if (!RulesEngine::getInstance().shouldProcess("assemble", typeStr, resourceNameStr)) {
            Log(DEBUG, "PluginManager", "Rules: skipping assemble for {} ({})", resourceNameStr, typeStr);
            if (statsService) { statsService->incrementProcessed(processName, true); }
            continue;
        }

        OperationsTrackerService::InputFingerprint fp = makeInputFingerprint(resourceNameStr, resourceType, "assemble_v1");
        if (opsTracker && !opsTracker->shouldProcess("assemble", SClass::getExtension(resourceType), resourceNameStr, fp, false)) {
            if (statsService) { statsService->incrementProcessed(processName, true); }
            continue;
        }
        
        if (opsTracker) {
            opsTracker->markStarted("assemble", SClass::getExtension(resourceType), resourceNameStr, fp);
        }

        OperationRequirements requirements;
        requirements.operationType = "assemble";
        requirements.resourceName = resourceNameStr;
        requirements.startingThreadCount = std::thread::hardware_concurrency();
        requirements.domain = ExecutionDomain::CPU;
        requirements.resourceAccess = ResourceAccess::SHARED;

        auto future = monitor.submitTaskWithRequirements(
            [this, resourceNameStr, resourceType, statsService, processName,
             &overallSuccess, opsTracker]() -> bool {
              Log(DEBUG, "PluginManager", "Processing resource: {}",
                  resourceNameStr);

              bool resourceSuccess =
                  assembleResource(resourceNameStr, resourceType, false);
              if (statsService) {
                statsService->incrementProcessed(processName, resourceSuccess);
              }

              if (!resourceSuccess) {
                Log(ERROR, "PluginManager", "Failed to assemble {}",
                    resourceNameStr);
                if (statsService) {
                  statsService->recordError(processName, resourceNameStr);
                }
                overallSuccess = false;
              }

              if (opsTracker) {
                opsTracker->markCompleted("assemble", SClass::getExtension(resourceType), resourceNameStr, resourceSuccess, {});
              }

              return resourceSuccess;
            },
            requirements, "assemble_" + resourceNameStr);

        futures.push_back(std::move(future));
    }

    // Wait for all assembly tasks to finish
    for (auto &f : futures) {
      try {
        f.get();
      } catch (const std::exception &e) {
        Log(ERROR, "PluginManager", "Assembly task failed with exception: {}",
            e.what());
        overallSuccess = false;
      }
    }

    bool success = overallSuccess.load();

    // End statistics tracking
    if (statsService) {
        statsService->endProcess(processName);
    }
    
    // End batch lifecycle for this resource type
    onBatchTypeEnd(resourceType);
    
    // Trigger batch assemble end lifecycle
    ServiceManager::onBatchAssembleEnd();
    
    // Flush logging system to ensure all messages are written
    FlushLogs();
    
    return success;
}

bool PluginManager::extractResource(const std::string& resourceName, SClass_ID resourceType, bool enableStats) {
    Log(DEBUG, "PluginManager", "extractResource called with resourceName: '{}', resourceType: {}", resourceName, resourceType);
    
    // Initialize shared resources for this resource type (for individual operations)
    ensureSharedResourcesInitialized(resourceType);
    
    // Start statistics tracking for single resource extraction (only if enabled)
    std::string processName = "extract_single_" + resourceName;
    auto* statsService = dynamic_cast<StatisticsService*>(ServiceManager::getService("StatisticsService"));
    if (statsService && enableStats) {
        statsService->startProcess(processName, SClass::getExtension(resourceType), 1);
    }
    
    auto plugin = createPlugin(resourceName, resourceType);
    if (!plugin) {
        Log(ERROR, "PluginManager", "Failed to create plugin for {}", resourceName);
        if (statsService && enableStats) {
            statsService->recordError(processName, "Failed to create plugin");
            statsService->incrementProcessed(processName, false);
            statsService->endProcess(processName);
        }
        return false;
    }
    
    if (!plugin->isValid()) {
        Log(ERROR, "PluginManager", "Plugin is not valid for {}", resourceName);
        if (statsService && enableStats) {
            statsService->recordError(processName, "Plugin is not valid");
            statsService->incrementProcessed(processName, false);
            statsService->endProcess(processName);
        }
        return false;
    }
    
    // Clean directories before extraction
    plugin->cleanExtractDirectory();
    
    bool success = plugin->extract();
    
    // Record statistics
    if (statsService && enableStats) {
        statsService->incrementProcessed(processName, success);
        if (!success) {
            statsService->recordError(processName, "Extraction failed");
        }
        statsService->endProcess(processName);
    }
    
    return success;
}

bool PluginManager::upscaleResource(const std::string& resourceName, SClass_ID resourceType, bool enableStats) {
    // Initialize shared resources for this resource type (for individual operations)
    ensureSharedResourcesInitialized(resourceType);
    
    // Start Services
    onBatchTypeStart(resourceType);
    
    // Start statistics tracking for single resource upscaling (only if enabled)
    std::string processName = "upscale_single_" + resourceName;
    auto* statsService = dynamic_cast<StatisticsService*>(ServiceManager::getService("StatisticsService"));
    if (statsService && enableStats) {
        statsService->startProcess(processName, SClass::getExtension(resourceType), 1);
    }
    
    auto plugin = createPlugin(resourceName, resourceType);
    if (!plugin) {
        Log(ERROR, "PluginManager", "Failed to create plugin for {}", resourceName);
        if (statsService && enableStats) {
            statsService->recordError(processName, "Failed to create plugin");
            statsService->incrementProcessed(processName, false);
            statsService->endProcess(processName);
        }
        onBatchTypeEnd(resourceType);
        return false;
    }
    
    if (!plugin->isValid()) {
        Log(ERROR, "PluginManager", "Plugin is not valid for {}", resourceName);
        if (statsService && enableStats) {
            statsService->recordError(processName, "Plugin is not valid");
            statsService->incrementProcessed(processName, false);
            statsService->endProcess(processName);
        }
        onBatchTypeEnd(resourceType);
        return false;
    }
    
    // Clean directories before upscaling
    plugin->cleanUpscaleDirectory();

    bool success = plugin->upscale();
    
    // Record statistics
    if (statsService && enableStats) {
        statsService->incrementProcessed(processName, success);
        if (!success) {
            statsService->recordError(processName, "Upscaling failed");
        }
        statsService->endProcess(processName);
    }
    
    // Notify services end
    onBatchTypeEnd(resourceType);
    
    return success;
}

bool PluginManager::assembleResource(const std::string& resourceName, SClass_ID resourceType, bool enableStats) {
    // Initialize shared resources for this resource type (for individual operations)
    ensureSharedResourcesInitialized(resourceType);
    
    // Start statistics tracking for single resource assembly (only if enabled)
    std::string processName = "assemble_single_" + resourceName;
    auto* statsService = dynamic_cast<StatisticsService*>(ServiceManager::getService("StatisticsService"));
    if (statsService && enableStats) {
        statsService->startProcess(processName, SClass::getExtension(resourceType), 1);
    }
    
    auto plugin = createPlugin(resourceName, resourceType);
    if (!plugin) {
        Log(ERROR, "PluginManager", "Failed to create plugin for {}", resourceName);
        if (statsService && enableStats) {
            statsService->recordError(processName, "Failed to create plugin");
            statsService->incrementProcessed(processName, false);
            statsService->endProcess(processName);
        }
        return false;
    }
    
    if (!plugin->isValid()) {
        Log(ERROR, "PluginManager", "Plugin is not valid for {}", resourceName);
        if (statsService && enableStats) {
            statsService->recordError(processName, "Plugin is not valid");
            statsService->endProcess(processName);
        }
        return false;
    }
    
    // Clean directories before assembly
    plugin->cleanAssembleDirectory();
    
    bool success = plugin->assemble();
    
    // Record statistics
    if (statsService && enableStats) {
        statsService->incrementProcessed(processName, success);
        if (!success) {
            statsService->recordError(processName, "Assembly failed");
        }
        statsService->endProcess(processName);
    }
    
    return success;
}

std::unique_ptr<PluginBase> PluginManager::createPlugin(const std::string& resourceName, SClass_ID resourceType) {
    auto it = pluginFactories_.find(resourceType);
    if (it == pluginFactories_.end()) {
        return nullptr;
    }
    
    return it->second(resourceName);
}

void PluginManager::registerAllCommands(CommandTable& commandTable) {
    // Register global batch commands
    commandTable["batch"] = {
        "Batch operations across all resource types",
        {
            {"extractAll", {
                "Extract all resources of all types (e.g., batch extractAll)",
                [this](const std::vector<std::string>&) -> int {
                    return extractAllResources() ? 0 : 1;
                }
            }},
            {"upscaleAll", {
                "Upscale all resources of all types (e.g., batch upscaleAll)",
                [this](const std::vector<std::string>&) -> int {
                    return upscaleAllResources() ? 0 : 1;
                }
            }},
            {"assembleAll", {
                "Assemble all resources of all types (e.g., batch assembleAll)",
                [this](const std::vector<std::string>&) -> int {
                    return assembleAllResources() ? 0 : 1;
                }
            }},
            {"complete", {
                "Run complete pipeline: extract -> upscale -> assemble for all resources (e.g., batch complete)",
                [this](const std::vector<std::string>&) -> int {
                    return completeAllResources() ? 0 : 1;
                }
            }},
            {"extractType", {
                "Extract all resources of a specific type (e.g., batch extractType bam)",
                [this](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: batch extractType <resource_type>" << std::endl;
                        return 1;
                    }
                    SClass_ID resourceType = getResourceTypeFromString(args[0]);
                    return extractAllResourcesOfType(resourceType) ? 0 : 1;
                }
            }},
            {"upscaleType", {
                "Upscale all resources of a specific type (e.g., batch upscaleType bam)",
                [this](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: batch upscaleType <resource_type>" << std::endl;
                        return 1;
                    }
                    SClass_ID resourceType = getResourceTypeFromString(args[0]);
                    return upscaleAllResourcesOfType(resourceType) ? 0 : 1;
                }
            }},
            {"assembleType", {
                "Assemble all resources of a specific type (e.g., batch assembleType bam)",
                [this](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: batch assembleType <resource_type>" << std::endl;
                        return 1;
                    }
                    SClass_ID resourceType = getResourceTypeFromString(args[0]);
                    return assembleAllResourcesOfType(resourceType) ? 0 : 1;
                }
            }},
            {"completeType", {
                "Complete pipeline for specific type: extract -> upscale -> assemble -> transfer (e.g., batch completeType mos)",
                [this](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: batch completeType <resource_type>" << std::endl;
                        return 1;
                    }
                    SClass_ID resourceType = getResourceTypeFromString(args[0]);
                    return completeAllResourcesOfType(resourceType) ? 0 : 1;
                }
            }}
        }
    };

    // Register transfer command
    commandTable["transfer"] = {
        "Transfer assembled assets to new override directory",
        {
            {"all", {
                "Transfer all assembled assets to directory based on GameType & UpScaleFactor e.g. demo-overrideX4",
                [this](const std::vector<std::string>& args) -> int {
                    return transferAssembledAssetsToOverride() ? 0 : 1;
                }
            }}
            ,
            {"type", {
                "Transfer assembled assets of a specific type (e.g., transfer type bcs)",
                [this](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: transfer type <resource_type>" << std::endl;
                        return 1;
                    }
                    SClass_ID resourceType = getResourceTypeFromString(args[0]);
                    if (resourceType == 0) return 1;
                    return transferByResourceType(resourceType) ? 0 : 1;
                }
            }}
        }
    };

    // Register Sync Unsupported command
    commandTable["sync"] = {
        "Sync unsupported assets to new override directory",
        {
            {"override", {
                "Transfer all assets override to directory based on GameType & UpScaleFactor",
                [this](const std::vector<std::string>& args) -> int {
                    return syncOverrideAssets() ? 0 : 1;
                }
            }}
        }
    };

    // Copy commands from the static registry (auto-discovered during plugin registration)
    commandTable.insert(PluginBase::getCommandRegistry().begin(), PluginBase::getCommandRegistry().end());
}

std::vector<SClass_ID> PluginManager::getSupportedResourceTypes() const {
    std::vector<SClass_ID> types;
    for (const auto& [resourceType, factory] : pluginFactories_) {
        types.push_back(resourceType);
    }
    return types;
}

bool PluginManager::isResourceTypeSupported(SClass_ID resourceType) const {
    return pluginFactories_.find(resourceType) != pluginFactories_.end();
}

void PluginManager::registerPlugin(SClass_ID resourceType, std::function<std::unique_ptr<PluginBase>(const std::string&)> factory) {
    pluginFactories_[resourceType] = factory;
    Log(DEBUG, "PluginManager", "Registered plugin for resource type: {}", SClass::getExtension(resourceType));
}

void PluginManager::ensurePluginRegistered(SClass_ID resourceType) {
    // If plugin is already registered, nothing to do
    if (pluginFactories_.find(resourceType) != pluginFactories_.end()) {
        return;
    }

    Log(WARNING, "PluginManager", "Plugin auto-discovery not yet implemented for resource type: {}", resourceType);
}

void PluginManager::resetBatchStats() {
    lastBatchStats_ = BatchStats{};
}

void PluginManager::logBatchResults(const std::string& operation, const BatchStats& stats) {
    Log(MESSAGE, "PluginManager", "{} complete: {} successful, {} failed, {} total resources, {}ms", 
        operation, stats.successfulOperations, stats.failedOperations, 
        stats.totalResources, stats.totalTime.count());
}

SClass_ID PluginManager::getResourceTypeFromString(const std::string& typeString) const {
    std::string lowerType = typeString;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), ::tolower);
    
    SClass_ID resourceType = SClass::getResourceTypeFromExtension(lowerType);
    if (resourceType != 0) {
        return resourceType;
    }
    
    Log(ERROR, "PluginManager", "Unknown resource type: {}", typeString);
    return 0;
}

bool PluginManager::transferAssembledAssetsToOverride() {
    // Create directory name based on GameType & UpScaleFactor
    std::string dirName = PIE4K_CFG.GameType + "-overrideX" + std::to_string(PIE4K_CFG.UpScaleFactor);
    
    // Create target directory path in the same directory as the binary
    fs::path binaryPath = fs::current_path();
    fs::path targetDir = binaryPath / dirName;
    
    Log(MESSAGE, "PluginManager", "Starting transfer of assembled assets to: {}", targetDir.string());
    
    // Start statistics tracking
    std::string processName = "transfer_all_assembled";
    auto* statsService = dynamic_cast<StatisticsService*>(ServiceManager::getService("StatisticsService"));
    
    // Count total resources across all types for statistics
    int totalResourceCount = 0;
    auto supportedTypesForCount = getSupportedResourceTypes();
    for (SClass_ID resourceType : supportedTypesForCount) {
        auto resources = listResourcesByType(resourceType);
        totalResourceCount += resources.size();
    }
    
    if (statsService) {
        statsService->startProcess(processName, "ALL", totalResourceCount);
    }
    
    // Load rules once (fail-open if missing)
    RulesEngine::getInstance().load();
    
    // Create target directory if it doesn't exist
    if (!fs::exists(targetDir)) {
        Log(MESSAGE, "PluginManager", "Creating target directory: {}", targetDir.string());
        try {
            fs::create_directories(targetDir);
        } catch (const fs::filesystem_error& e) {
            Log(ERROR, "PluginManager", "Failed to create target directory: {}", e.what());
            if (statsService) {
                statsService->recordError(processName, "Failed to create target directory: " + std::string(e.what()));
                statsService->endProcess(processName);
            }
            return false;
        }
    }
    
    int totalTransferred = 0;
    int totalErrors = 0;
    int totalOverwrites = 0;
    
    // Get all supported resource types from registered plugins
    auto supportedTypes = getSupportedResourceTypes();
    
    // Process each supported asset type
    for (SClass_ID resourceType : supportedTypes) {
        std::string typeName = SClass::getExtension(resourceType);
        
        Log(MESSAGE, "PluginManager", "Processing {} assets...", typeName);
        
        // Get all resources of this type from our resource service
        auto resources = listResourcesByType(resourceType);
        int totalResources = resources.size();
        Log(MESSAGE, "PluginManager", "Found {} {} resources in the game", totalResources, typeName);
        
        if (resources.empty()) {
            Log(WARNING, "PluginManager", "No {} resources found in the game", typeName);
            continue;
        }
        
        // Track statistics for this resource type
        int foundAssembled = 0;
        int notFoundAssembled = 0;
        int typeTransferred = 0;
        int typeErrors = 0;
        int typeFilesTransferred = 0;
        int typeOverwrites = 0;
        
        // Process each resource
        for (const auto& resource : resources) {
            std::string resourceName = resource.c_str();
            
            // Check rules - skip if this resource should not be transferred
            if (!RulesEngine::getInstance().shouldProcess("transfer", typeName.c_str(), resourceName)) {
                Log(DEBUG, "PluginManager", "Rules: skipping transfer for {} ({})", resourceName, typeName);
                if (statsService) { 
                    statsService->incrementProcessed(processName, true); 
                }
                continue;
            }
            
            // Create a temporary plugin instance to get the assemble directory
            auto plugin = createPlugin(resourceName, resourceType);
            if (!plugin) {
                Log(ERROR, "PluginManager", "Failed to create plugin for {}: {}", resourceName, typeName);
                if (statsService) {
                    statsService->recordError(processName, resourceName);
                }
                typeErrors++;
                totalErrors++;
                continue;
            }
            
            // Get the assemble directory from the plugin (don't ensure it exists since we're just reading)
            std::string assembleDirPath = plugin->getAssembleDir(false);
            
            // Look for assembled files in this directory
            int filesInDirectory = 0;
            bool resourceTransferred = false;
            
            try {
                for (const auto& fileEntry : fs::directory_iterator(assembleDirPath)) {
                    if (!fileEntry.is_regular_file()) continue;
                    
                    filesInDirectory++;
                    
                    std::string fileName = fileEntry.path().filename().string();
                    std::string sourcePath = fileEntry.path().string();
                    std::string targetFileName = fileName;
                    std::string targetPath = (targetDir / targetFileName).string();
                    
                    // Check if target file already exists
                    bool fileExists = fs::exists(targetPath);
                    
                    // Copy the file
                    try {
                        fs::copy_file(sourcePath, targetPath, fs::copy_options::overwrite_existing);
                        
                        if (fileExists) {
                            Log(MESSAGE, "PluginManager", "Overwrote existing file: {} -> {}", fileName, targetFileName);
                            typeOverwrites++;
                            totalOverwrites++;
                        } else {
                            Log(MESSAGE, "PluginManager", "Transferred {} -> {}", fileName, targetFileName);
                        }
                        
                        typeFilesTransferred++;
                        totalTransferred++;
                        resourceTransferred = true;
                    } catch (const fs::filesystem_error& e) {
                        Log(ERROR, "PluginManager", "Failed to transfer {}: {}", fileName, e.what());
                        if (statsService) {
                            statsService->recordError(processName, resourceName);
                        }
                        typeErrors++;
                        totalErrors++;
                    }
                }
                
                // Update statistics based on file count
                if (filesInDirectory == 0) {
                    Log(DEBUG, "PluginManager", "No assembled files found for {}: {}", resourceName, assembleDirPath);
                    notFoundAssembled++;
                } else {
                    Log(MESSAGE, "PluginManager", "Found {} assembled files for {}: {}", filesInDirectory, resourceName, assembleDirPath);
                    foundAssembled++;
                    if (resourceTransferred) {
                        typeTransferred++;
                    }
                }
                
            } catch (const fs::filesystem_error& e) {
                Log(ERROR, "PluginManager", "Error scanning assembled directory for {}: {}", resourceName, e.what());
                if (statsService) {
                    statsService->recordError(processName, resourceName);
                }
                typeErrors++;
                totalErrors++;
            }
            
            // Update progress for this resource
            if (statsService) {
                statsService->incrementProcessed(processName, resourceTransferred);
            }
        }
        
        // Report statistics for this resource type
        Log(MESSAGE, "PluginManager", "{} Statistics:", typeName);
        Log(MESSAGE, "PluginManager", "  Total resources found in game: {}", totalResources);
        Log(MESSAGE, "PluginManager", "  Found assembled: {}", foundAssembled);
        Log(MESSAGE, "PluginManager", "  Not found assembled: {}", notFoundAssembled);
        Log(MESSAGE, "PluginManager", "  Successfully transferred: {}", typeTransferred);
        Log(MESSAGE, "PluginManager", "  Total files transferred: {}", typeFilesTransferred);
        Log(MESSAGE, "PluginManager", "  Files overwritten: {}", typeOverwrites);
        Log(MESSAGE, "PluginManager", "  Transfer errors: {}", typeErrors);
    }
    
    Log(MESSAGE, "PluginManager", "Transfer complete. Total transferred: {}, Total overwrites: {}, Total errors: {}", 
        totalTransferred, totalOverwrites, totalErrors);
    
    // Complete statistics tracking
    if (statsService) {
        statsService->endProcess(processName);
    }
    
    return totalErrors == 0;
}

bool PluginManager::transferByResourceType(SClass_ID resourceType) {
    std::string dirName = PIE4K_CFG.GameType + "-overrideX" + std::to_string(PIE4K_CFG.UpScaleFactor);
    fs::path binaryPath = fs::current_path();
    fs::path targetDir = binaryPath / dirName;

    std::string typeName = SClass::getExtension(resourceType);
    Log(MESSAGE, "PluginManager", "Transferring assembled {} assets to: {}", typeName, targetDir.string());

    if (!fs::exists(targetDir)) {
        try {
            fs::create_directories(targetDir);
        } catch (const fs::filesystem_error& e) {
            Log(ERROR, "PluginManager", "Failed creating target directory: {}", e.what());
            return false;
        }
    }

    // List resources of this specific type
    auto allResources = listResourcesByType(resourceType);
    Log(MESSAGE, "PluginManager", "Found {} {} resources", allResources.size(), typeName);

    // Load rules once (fail-open if missing)
    RulesEngine::getInstance().load();
    
    // Filter resources based on rules before counting for statistics
    std::vector<std::string> resources;
    for (const auto& resource : allResources) {
        std::string resourceName = resource.c_str();
        if (RulesEngine::getInstance().shouldProcess("transfer", typeName.c_str(), resourceName)) {
            resources.push_back(resource);
        } else {
            Log(DEBUG, "PluginManager", "Rules: skipping transfer for {} ({})", resourceName, typeName);
        }
    }
    
    Log(MESSAGE, "PluginManager", "After rules filtering: {} {} resources to transfer", resources.size(), typeName);

    // Start statistics tracking with filtered count
    std::string processName = std::string("transfer_") + typeName;
    auto* statsService = dynamic_cast<StatisticsService*>(ServiceManager::getService("StatisticsService"));
    if (statsService) {
        statsService->startProcess(processName, typeName, resources.size());
    }

    int transferred = 0, overwrites = 0, errors = 0, foundAssembled = 0;

    for (const auto &resource : resources) {
        std::string resourceName = resource.c_str();
        
        auto plugin = createPlugin(resourceName, resourceType);
        if (!plugin) {
            Log(ERROR, "PluginManager", "Failed to create plugin for {} ({})", resourceName, typeName);
            if (statsService) {
                statsService->recordError(processName, resourceName + ": Failed to create plugin");
                statsService->incrementProcessed(processName, false);
            }
            errors++;
            continue;
        }

        std::string assembleDirPath = plugin->getAssembleDir(false);
        int filesInDirectory = 0;
        bool resourceTransferred = false;
        
        // Check if assembled directory exists
        if (!fs::exists(assembleDirPath)) {
            Log(DEBUG, "PluginManager", "No assembled output for {} ({}): {}", resourceName, typeName, assembleDirPath);
            if (statsService) {
                statsService->recordError(processName, resourceName);
                statsService->incrementProcessed(processName, false);
            }
            errors++;
            continue;
        }
        
        try {
            for (const auto &fileEntry : fs::directory_iterator(assembleDirPath)) {
                if (!fileEntry.is_regular_file()) continue;
                filesInDirectory++;
                std::string fileName = fileEntry.path().filename().string();
                std::string sourcePath = fileEntry.path().string();
                std::string targetPath = (targetDir / fileName).string();

                bool exists = fs::exists(targetPath);
                try {
                    fs::copy_file(sourcePath, targetPath, fs::copy_options::overwrite_existing);
                    if (exists) { overwrites++; Log(MESSAGE, "PluginManager", "Overwrote {} -> {}", fileName, targetPath); }
                    else { Log(MESSAGE, "PluginManager", "Transferred {} -> {}", fileName, targetPath); }
                    transferred++; resourceTransferred = true;
                } catch (const fs::filesystem_error& e) {
                    Log(ERROR, "PluginManager", "Copy failed for {} ({}): {}", resourceName, fileName, e.what());
                    if (statsService) {
                        statsService->recordError(processName, resourceName + ": " + e.what());
                    }
                    errors++;
                }
            }
            if (filesInDirectory > 0) {
                foundAssembled++;
                Log(DEBUG, "PluginManager", "Successfully transferred {} ({}) - {} files", resourceName, typeName, filesInDirectory);
            } else {
                Log(WARNING, "PluginManager", "Empty assembled directory for {} ({}): {}", resourceName, typeName, assembleDirPath);
            }
        } catch (const fs::filesystem_error& e) {
            Log(ERROR, "PluginManager", "Error scanning assembled dir for {} ({}): {}", resourceName, typeName, e.what());
            if (statsService) {
                statsService->recordError(processName, resourceName + ": " + e.what());
            }
            errors++;
        }
        
        // Update progress for this resource
        if (statsService) {
            statsService->incrementProcessed(processName, resourceTransferred);
        }
    }

    // Complete statistics tracking
    if (statsService) {
        statsService->endProcess(processName);
    }

    Log(MESSAGE, "PluginManager", "{} transfer summary:", typeName);
    Log(MESSAGE, "PluginManager", "  Resources with assembled output: {}", foundAssembled);
    Log(MESSAGE, "PluginManager", "  Files transferred: {} ({} overwrites)", transferred, overwrites);
    Log(MESSAGE, "PluginManager", "  Errors: {}", errors);

    return errors == 0;
}

void PluginManager::registerService(const std::string& serviceName, std::unique_ptr<ServiceBase> service) {
    services_[serviceName] = std::move(service);
    Log(MESSAGE, "PluginManager", "Registered service: {}", serviceName);
}

ServiceBase* PluginManager::getService(const std::string& serviceName) {
    // First check if service is already loaded
    auto it = services_.find(serviceName);
    if (it != services_.end()) {
        return it->second.get();
    }
    
    // Try to load the service dynamically via ServiceManager
    auto dynamicService = ServiceManager::createService(serviceName);
    if (dynamicService) {
        // Cache the service instance
        auto* servicePtr = dynamicService.get();
        services_[serviceName] = std::move(dynamicService);
        Log(MESSAGE, "PluginManager", "Dynamically loaded service: {}", serviceName);
        return servicePtr;
    }
    
    Log(ERROR, "PluginManager", "Service not found: {}", serviceName);
    return nullptr;
}

void PluginManager::onBatchTypeStart(SClass_ID resourceType) {
    Log(DEBUG, "PluginManager", "Starting batch for resource type: {}", SClass::getExtension(resourceType));
    
    currentBatchResourceType_ = resourceType;
    
    // Trigger lifecycle events
    ServiceManager::onResourceTypeStart(resourceType);
}

void PluginManager::onBatchTypeEnd(SClass_ID resourceType) {
    Log(DEBUG, "PluginManager", "Ending batch for resource type: {}", SClass::getExtension(resourceType));
    
    // Trigger lifecycle events
    ServiceManager::onResourceTypeEnd(resourceType);
    
    currentBatchResourceType_ = 0;
}

ResourceCoordinatorService* PluginManager::getResourceCoordinator() {
    auto* service = dynamic_cast<ResourceCoordinatorService*>(ServiceManager::getService("ResourceCoordinatorService"));
    return service;
}

std::vector<std::string> PluginManager::listResourcesByType(SClass_ID resourceType) {
    auto* resourceCoordinator = getResourceCoordinator();
    if (!resourceCoordinator) {
        Log(ERROR, "PluginManager", "Failed to get ResourceCoordinatorService");
        return {};
    }
    
    // Get all resources of this type from the service
    return resourceCoordinator->listResourcesByType(resourceType);
}

std::vector<std::string> PluginManager::getAvailableServices() const {
    std::vector<std::string> services;
    
    // Add loaded services
    for (const auto& [name, service] : services_) {
        services.push_back(name);
    }
    
    // Add discoverable services from ServiceManager
    auto discoverableServices = ServiceManager::getAvailableServices();
    services.insert(services.end(), discoverableServices.begin(), discoverableServices.end());
    
    return services;
}

std::pair<std::string, int> PluginManager::generatePVRZName(const std::string& resourceName, SClass_ID resourceType) {
    int pageNumber = 0;
    
    if (resourceType == IE_TIS_CLASS_ID) {
        pageNumber = areaPVRZCounter_.fetch_add(1);
    } else if (resourceType == IE_BAM_CLASS_ID || resourceType == IE_MOS_CLASS_ID) {
        pageNumber = mosPVRZCounter_.fetch_add(1);
    }
    
    return { generatePVRZNameInternal(resourceName, pageNumber, resourceType), pageNumber };
}

// only used in single instances, or when trying to get a PVRZ name for reference purposes, otherwise should use generatePVRZName
// TODO : fix usage so it is more clear what to use
std::string PluginManager::generatePVRZNameInternal(const std::string& resourceName, int pageNumber, SClass_ID resourceType) {
    // PVRZ naming convention depends on the resource type
    if (resourceType == IE_TIS_CLASS_ID) {
        // TIS PVRZ naming: {first_char_of_tis}{area_code}{optional_N}{page_number}
        // Example: AR0011.TIS -> A0011xx (where xx is the page number)
        
        std::string baseName = resourceName;
        
        if (baseName.length() >= 6) {
            char firstChar = baseName[0];
            std::string areaCode;
            
            // Extract the numeric part (e.g., 0011 from AR0011)
            areaCode = baseName.substr(2, 4);
            
            std::ostringstream oss;
            oss << firstChar << areaCode;
            oss << std::setfill('0') << std::setw(2) << pageNumber; // no extension
            
            return oss.str();
        } else {
            // Fallback for shorter names
            std::ostringstream oss;
            oss << baseName << std::setfill('0') << std::setw(2) << pageNumber;
            return oss.str();
        }
    } else if (resourceType == IE_BAM_CLASS_ID || resourceType == IE_MOS_CLASS_ID) {
        // BAM/MOS PVRZ naming: MOS{page_number}
        // Example: MOS0000, MOS0001, etc.
        std::ostringstream oss;
        oss << "MOS" << std::setfill('0') << std::setw(4) << pageNumber;
        return oss.str();
    }
    
    // Fallback for other types (can be expanded)
    Log(WARNING, "PluginManager", "PVRZ name generation not implemented for this resource type, using default.");
    std::ostringstream oss;
    oss << resourceName << "_" << pageNumber << ".PVRZ";
    return oss.str();
}

bool PluginManager::syncOverrideAssets() {
    // Get the override directory from PIE4K config
    std::string overrideDir = PIE4K_CFG.getGameOverridePath();
    if (overrideDir.empty()) {
        Log(ERROR, "PluginManager", "GameOverridePath is not configured");
        return false;
    }
    
    fs::path overridePath(overrideDir);
    if (!fs::exists(overridePath) || !fs::is_directory(overridePath)) {
        Log(ERROR, "PluginManager", "Override directory does not exist: {}", overridePath.string());
        return false;
    }
    
    // Get target directory
    std::string dirName = PIE4K_CFG.GameType + "-overrideX" + std::to_string(PIE4K_CFG.UpScaleFactor);
    fs::path binaryPath = fs::current_path();
    fs::path targetDir = binaryPath / dirName;
    
    Log(MESSAGE, "PluginManager", "Starting override assets sync...");
    Log(MESSAGE, "PluginManager", "Source: {}", overridePath.string());
    Log(MESSAGE, "PluginManager", "Target: {}", targetDir.string());
    
    int totalFiles = 0;
    int copiedFiles = 0;
    int errors = 0;
    
    try {
        // Recursively copy all files from override to target
        for (const auto& entry : fs::recursive_directory_iterator(overridePath)) {
            if (!entry.is_regular_file()) continue;
            
            fs::path filePath = entry.path();
            fs::path relativePath = fs::relative(filePath, overridePath);
            fs::path targetPath = targetDir / relativePath;
            
            // Ensure target subdirectory exists
            fs::path targetFileDir = targetPath.parent_path();
            if (!fs::exists(targetFileDir)) {
                fs::create_directories(targetFileDir);
            }
            
            totalFiles++;
            try {
                fs::copy_file(filePath, targetPath, fs::copy_options::overwrite_existing);
                Log(DEBUG, "PluginManager", "Copied override asset: {} -> {}", relativePath.string(), targetPath.string());
                copiedFiles++;
            } catch (const fs::filesystem_error& e) {
                Log(ERROR, "PluginManager", "Failed to copy override asset {}: {}", relativePath.string(), e.what());
                errors++;
            }
        }
    } catch (const fs::filesystem_error& e) {
        Log(ERROR, "PluginManager", "Error scanning override directory: {}", e.what());
        return false;
    }
    
    Log(MESSAGE, "PluginManager", "Override assets sync complete:");
    Log(MESSAGE, "PluginManager", "  Total files found: {}", totalFiles);
    Log(MESSAGE, "PluginManager", "  Files copied: {}", copiedFiles);
    Log(MESSAGE, "PluginManager", "  Errors: {}", errors);
    
    if (errors > 0) {
        Log(WARNING, "PluginManager", "Override assets sync completed with {} errors", errors);
        return false;
    }
    
    Log(MESSAGE, "PluginManager", "Successfully synced {} override assets to: {}", 
        copiedFiles, targetDir.string());
    return true;
}

OperationsTrackerService::InputFingerprint PluginManager::makeInputFingerprint(const std::string& resourceName,
                                                                       SClass_ID resourceType,
                                                                       const std::string& opVersion) {
    OperationsTrackerService::InputFingerprint fp;
    // For extract, UpScaleFactor is irrelevant; omit it from the config hash
    if (opVersion.rfind("extract", 0) == 0) {
        fp.configHash = PIE4K_CFG.GameType;
    } else {
        fp.configHash = PIE4K_CFG.GameType + ":" + std::to_string(PIE4K_CFG.UpScaleFactor);
    }
    fp.opVersion = opVersion;
    std::string extDot = SClass::getExtensionWithDot(resourceType);
    std::string overrideDir = PIE4K_CFG.getGameOverridePath();
    if (!overrideDir.empty()) {
        fs::path p = fs::path(overrideDir) / (resourceName + extDot);
        if (fs::exists(p) && fs::is_regular_file(p)) {
            fp.sourcePath = p.string();
            fp.overrideSize = static_cast<uint64_t>(fs::file_size(p));
            // capture filesystem mtime for more robust fingerprinting
            try {
                auto ftime = fs::last_write_time(p);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
                fp.mtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
            } catch (...) {
                fp.mtime = 0;
            }
        }
    }
    // Prefer size via ResourceCoordinatorService which consults override and BIF indices
    if (auto* rc = dynamic_cast<ResourceCoordinatorService*>(ServiceManager::getService("ResourceCoordinatorService"))) {
        uint64_t sz = rc->getResourceSize(resourceName, resourceType);
        if (sz > 0) {
            fp.size = static_cast<uint32_t>(sz);
        }
        // If override info not set above (path empty) but override exists, RC will still have size; path stays empty which is fine
    } else if (fp.sourcePath.empty()) {
        // Fallback to KEY metadata if coordinator not available
        if (auto* keySvc = dynamic_cast<KEYService*>(ServiceManager::getService("KEYService"))) {
            auto info = keySvc->getResourceInfo(resourceName, resourceType);
            if (info.isValid) {
                fp.bifIndex = static_cast<int>(info.bifIndex);
                fp.keyLocator = info.locator;
                fp.size = info.size;
            }
        }
    }
    return fp;
}

void PluginManager::ensureSharedResourcesInitialized(SClass_ID resourceType) {
    std::lock_guard<std::mutex> lk(sharedResourcesMutex_);
    // Check if already initialized
    if (sharedResourcesInitialized_[resourceType]) return;

    Log(DEBUG, "PluginManager", "Initializing shared resources for resource type: {}", resourceType);

    // Create a temporary plugin instance to initialize shared resources
    auto tempPlugin = createPlugin("__shared_init__", resourceType);
    if (!tempPlugin) {
        Log(WARNING, "PluginManager", "Failed to create plugin for shared resource initialization: {}", resourceType);
        return;
    }

    // Check if this plugin has shared resources
    if (!tempPlugin->hasSharedResources()) {
        Log(DEBUG, "PluginManager", "Resource type {} has no shared resources", resourceType);
        sharedResourcesInitialized_[resourceType] = true;
        return;
    }

    // Initialize shared resources
    if (tempPlugin->initializeSharedResources()) {
        Log(MESSAGE, "PluginManager", "Successfully initialized shared resources for resource type: {}", resourceType);
        sharedResourcesInitialized_[resourceType] = true;
    } else {
        Log(ERROR, "PluginManager", "Failed to initialize shared resources for resource type: {}", resourceType);
    }
}

} // namespace ProjectIE4k 