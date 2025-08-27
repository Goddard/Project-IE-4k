#include "FileLogWriter.h"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace ProjectIE4k {

FileLogWriter::FileLogWriter(LogLevel level)
    : LogWriter(level)
{
    auto logPath = getLogFilePath();
    logFile.open(logPath, std::ios::trunc); // Clear the file on startup
    
    if (logFile.is_open()) {
        logFile << "=== Project IE 4k Log Started ===" << std::endl;
        logFile.flush();
    }
}

FileLogWriter::~FileLogWriter() {
    if (logFile.is_open()) {
        logFile << "=== Project IE 4k Log Ended ===" << std::endl;
        logFile.close();
    }
}

void FileLogWriter::WriteLogMessage(const LogMessage& msg) {
    if (!logFile.is_open()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(fileMutex);
    
    logFile << getCurrentTimestamp() << " [" << getLevelString(msg.level) << "]["
            << msg.owner << "] "
            << msg.message << std::endl;
}

void FileLogWriter::Flush() {
    std::lock_guard<std::mutex> lock(fileMutex);
    if (logFile.is_open()) {
        logFile.flush();
    }
}

std::string FileLogWriter::getLevelString(LogLevel level) const {
    switch (level) {
        case INTERNAL: return "INTERNAL";
        case FATAL: return "FATAL";
        case ERROR: return "ERROR";
        case WARNING: return "WARNING";
        case MESSAGE: return "MESSAGE";
        case DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}

std::string FileLogWriter::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

std::filesystem::path FileLogWriter::getLogFilePath() const {
    // Get the directory where the executable is located
    std::filesystem::path exePath = std::filesystem::current_path();
    
    // Try to find the actual executable path
    try {
        // This is a simplified approach - in a real implementation you might want
        // to use platform-specific methods to get the actual executable path
        exePath = std::filesystem::current_path();
    } catch (...) {
        // Fallback to current directory
    }
    
    return exePath / "pie4k.log";
}

} // namespace ProjectIE4k 