#include "Logging.h"
#include "Logger.h"
#include "FileLogWriter.h"
#include "ConsoleLogWriter.h"
#include <memory>

namespace ProjectIE4k {

// Global logger instance
static std::unique_ptr<Logger> globalLogger;
static std::atomic<bool> loggingEnabled{true};

void ToggleLogging(bool enabled) {
    loggingEnabled = enabled;
}

void AddLogWriter(std::shared_ptr<LogWriter> writer) {
    if (globalLogger) {
        globalLogger->AddLogWriter(std::move(writer));
    }
}

void SetConsoleWindowLogLevel(LogLevel level) {
    // This could be implemented to set console output level
    // For now, we'll just set the file writer level
    if (globalLogger) {
        // Note: This is a simplified approach. In a full implementation,
        // you'd want to track individual writers and their levels
    }
}

void LogMsg(LogLevel level, const char* owner, const char* message) {
    if (!loggingEnabled || !globalLogger) {
        return;
    }
    
    globalLogger->LogMsg(level, owner, message);
}

void FlushLogs() {
    if (globalLogger) {
        globalLogger->Flush();
    }
}

// Initialize the logging system
void InitializeLogging() {
    if (globalLogger) {
        return; // Already initialized
    }
    
    // Create file and console log writers
    auto fileWriter = std::make_shared<FileLogWriter>(DEBUG);
    // Only show MESSAGE and above in console
    auto consoleWriter = std::make_shared<ConsoleLogWriter>(FATAL);

    // Create the logger with both writers
    std::deque<Logger::WriterPtr> writers;
    writers.push_back(std::move(fileWriter));
    writers.push_back(std::move(consoleWriter));
    
    globalLogger = std::make_unique<Logger>(std::move(writers));
}

// Cleanup the logging system
void ShutdownLogging() {
    if (globalLogger) {
        FlushLogs();
        globalLogger.reset();
    }
}

} // namespace ProjectIE4k 