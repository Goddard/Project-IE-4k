#include "StatisticsService.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>

#include "core/Logging/Logging.h"
#include "services/ServiceBase.h"

namespace ProjectIE4k {

StatisticsService::StatisticsService() {
    Log(DEBUG, "StatisticsService", "StatisticsService constructor called");
}

void StatisticsService::initializeForResourceType(SClass_ID resourceType) {
    currentResourceType_ = resourceType;
    Log(DEBUG, "StatisticsService", "Initialized for resource type: {}", resourceType);
}

void StatisticsService::cleanup() {
    Log(DEBUG, "StatisticsService", "StatisticsService cleanup called");
    clear();
    initialized_ = false;
    currentResourceType_ = 0;
}

void StatisticsService::onLifecycleEvent(ServiceLifecycle event, const std::string& context) {
    switch (event) {
        case ServiceLifecycle::APPLICATION_START:
            Log(DEBUG, "StatisticsService", "Application start event received");
            initialized_ = true;
            break;
            
        case ServiceLifecycle::APPLICATION_SHUTDOWN:
            Log(DEBUG, "StatisticsService", "Application shutdown event received");
            generateSummary();
            cleanup();
            break;
            
        case ServiceLifecycle::BATCH_EXTRACT_START:
        case ServiceLifecycle::BATCH_UPSCALE_START:
        case ServiceLifecycle::BATCH_ASSEMBLE_START:
            Log(DEBUG, "StatisticsService", "Batch start event received: {}", static_cast<int>(event));
            break;
            
        case ServiceLifecycle::BATCH_EXTRACT_END:
        case ServiceLifecycle::BATCH_UPSCALE_END:
        case ServiceLifecycle::BATCH_ASSEMBLE_END:
            Log(DEBUG, "StatisticsService", "Batch end event received: {}", static_cast<int>(event));
            break;

        case ServiceLifecycle::BATCH_COMPLETE:
          Log(DEBUG, "StatisticsService",
              "Batch complete event received - generating summary");
          // generateSummary();
          break;

        default:
            // Ignore other events
            break;
    }
}

void StatisticsService::startProcess(const std::string& processName, const std::string& resourceType, int totalFiles) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    ProcessStats& stats = processes_[processName];
    stats.processName = processName;
    stats.resourceType = resourceType;
    stats.totalFiles = totalFiles;
    stats.processedFiles = 0;
    stats.successfulFiles = 0;
    stats.failedFiles = 0;
    stats.startTime = std::chrono::steady_clock::now();
    stats.errors.clear();
    stats.errorCounts.clear();
    
    std::cout << "Starting process: " << processName << " (" << resourceType << ")";
    if (totalFiles > 0) {
        std::cout << " - " << totalFiles << " files to process";
    }
    std::cout << std::endl;
}

void StatisticsService::incrementProcessed(const std::string& processName, bool success) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    auto it = processes_.find(processName);
    if (it != processes_.end()) {
        ProcessStats& stats = it->second;
        stats.processedFiles++;
        if (success) {
            stats.successfulFiles++;
        } else {
            stats.failedFiles++;
        }
        
        // Progress update every 100 files
        if (stats.processedFiles % 100 == 0) {
            std::cout << "Progress [" << processName << "]: " 
                      << stats.processedFiles << "/" << stats.totalFiles 
                      << " files processed (" << std::fixed << std::setprecision(1)
                      << (stats.processedFiles * 100.0 / stats.totalFiles) << "%)" << std::endl;
        }
    }
}

void StatisticsService::recordError(const std::string& processName, const std::string& error) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    auto it = processes_.find(processName);
    if (it != processes_.end()) {
        ProcessStats& stats = it->second;
        stats.errors.push_back(error);
        stats.errorCounts[error]++;
    }
}

void StatisticsService::endProcess(const std::string& processName) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    auto it = processes_.find(processName);
    if (it != processes_.end()) {
        ProcessStats& stats = it->second;
        stats.endTime = std::chrono::steady_clock::now();
        
        auto duration = stats.endTime - stats.startTime;
        std::cout << "Completed process: " << processName << " - "
                  << stats.processedFiles << " files processed in " 
                  << formatDuration(duration) << std::endl;
    }
}

StatisticsService::ProcessStats StatisticsService::getProcessStats(const std::string& processName) const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    auto it = processes_.find(processName);
    if (it != processes_.end()) {
        return it->second;
    }
    return ProcessStats();
}

std::vector<std::string> StatisticsService::getAllProcessNames() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    std::vector<std::string> names;
    for (const auto& pair : processes_) {
        names.push_back(pair.first);
    }
    return names;
}

void StatisticsService::generateSummary() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    if (processes_.empty()) {
        // no need to log
        // std::cout << "No processes to summarize." << std::endl;
        return;
    }
    
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "                    PROCESSING SUMMARY" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    // Calculate overall statistics
    int totalProcesses = processes_.size();
    int totalFiles = 0;
    int totalSuccessful = 0;
    int totalFailed = 0;
    std::chrono::steady_clock::duration totalTime{0};
    
    for (const auto& pair : processes_) {
        const ProcessStats& stats = pair.second;
        totalFiles += stats.processedFiles;
        totalSuccessful += stats.successfulFiles;
        totalFailed += stats.failedFiles;
        if (stats.endTime > stats.startTime) {
            totalTime += (stats.endTime - stats.startTime);
        }
    }
    
    std::cout << "Total Processes: " << totalProcesses << std::endl;
    std::cout << "Total Files: " << totalFiles << std::endl;
    std::cout << "Total Time: " << formatDuration(totalTime) << std::endl;
    std::cout << "Overall Success Rate: " << std::fixed << std::setprecision(2)
              << (totalFiles > 0 ? (totalSuccessful * 100.0 / totalFiles) : 0.0) << "%" << std::endl;
    std::cout << std::endl;
    
    // Individual process details
    for (const auto& pair : processes_) {
        const ProcessStats& stats = pair.second;
        std::cout << std::string(50, '-') << std::endl;
        std::cout << stats.processName << " (" << stats.resourceType << "):" << std::endl;
        std::cout << "  Files: " << stats.processedFiles << " processed";
        if (stats.totalFiles > 0) {
            std::cout << " (" << stats.successfulFiles << " success, " << stats.failedFiles << " failed)";
        }
        std::cout << std::endl;
        
        if (stats.endTime > stats.startTime) {
            auto duration = stats.endTime - stats.startTime;
            std::cout << "  Time: " << formatDuration(duration) << std::endl;
            if (stats.processedFiles > 0) {
                auto avgTime = duration / stats.processedFiles;
                std::cout << "  Avg: " << formatDuration(avgTime) << " per file" << std::endl;
            }
        }
        
        if (stats.processedFiles > 0) {
            double successRate = (stats.successfulFiles * 100.0 / stats.processedFiles);
            std::cout << "  Success Rate: " << std::fixed << std::setprecision(2) << successRate << "%" << std::endl;
        }
        
        // Show all errors
        if (!stats.errorCounts.empty()) {
            std::cout << "  Errors: ";
            std::vector<std::string> errorNames;
            for (const auto& error : stats.errorCounts) {
                errorNames.push_back(error.first);
            }
            
            // Sort alphabetically for consistent output
            std::sort(errorNames.begin(), errorNames.end());
            
            // Print all error names in a comma-separated list
            for (size_t i = 0; i < errorNames.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << errorNames[i];
            }
            std::cout << std::endl;
        }
    }
    
    std::cout << std::string(60, '=') << std::endl << std::endl;
}

void StatisticsService::saveSummaryToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        return;
    }
    
    // Redirect cout to file temporarily
    std::streambuf* coutBuffer = std::cout.rdbuf();
    std::cout.rdbuf(file.rdbuf());
    
    generateSummary();
    
    // Restore cout
    std::cout.rdbuf(coutBuffer);
    
    std::cout << "Summary saved to: " << filename << std::endl;
}

void StatisticsService::clear() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    processes_.clear();
}

bool StatisticsService::hasProcess(const std::string& processName) const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return processes_.find(processName) != processes_.end();
}

std::string StatisticsService::formatDuration(const std::chrono::steady_clock::duration& duration) const {
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration - hours);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration - hours - minutes);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration - hours - minutes - seconds);
    
    std::ostringstream oss;
    if (hours.count() > 0) {
        oss << hours.count() << "h ";
    }
    if (minutes.count() > 0 || hours.count() > 0) {
        oss << minutes.count() << "m ";
    }
    if (seconds.count() > 0 || minutes.count() > 0 || hours.count() > 0) {
        oss << seconds.count() << "s";
        if (milliseconds.count() > 0) {
            oss << " " << milliseconds.count() << "ms";
        }
    } else {
        // Less than 1 second, show milliseconds
        oss << milliseconds.count() << "ms";
    }
    
    return oss.str();
}

std::string StatisticsService::formatFileSize(size_t bytes) const {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unitIndex < 3) {
        size /= 1024.0;
        unitIndex++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << size << " " << units[unitIndex];
    return oss.str();
}



} // namespace ProjectIE4k

REGISTER_SERVICE(StatisticsService) 