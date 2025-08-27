#include "ConsoleLogWriter.h"
#include <cstdlib>

namespace ProjectIE4k {

// ANSI color codes
namespace Colors {
    const char* RESET = "\033[0m";
    const char* RED = "\033[31m";
    const char* GREEN = "\033[32m";
    const char* YELLOW = "\033[33m";
    const char* BLUE = "\033[34m";
    const char* MAGENTA = "\033[35m";
    const char* CYAN = "\033[36m";
    const char* WHITE = "\033[37m";
    const char* BOLD = "\033[1m";
    const char* DIM = "\033[2m";
    const char* BOLD_RED = "\033[1;31m";
}

ConsoleLogWriter::ConsoleLogWriter(LogLevel level)
    : LogWriter(level), colorsEnabled(supportsColors())
{
}

void ConsoleLogWriter::WriteLogMessage(const LogMessage& msg) {
    std::ostream& output = getOutputStream(msg.level);
    
    if (colorsEnabled) {
        // Get color for the log level
        const char* levelColor = getLevelColor(msg.level);
        const char* ownerColor = Colors::CYAN;
        
        output << levelColor << "[" << getLevelString(msg.level) << "]" << Colors::RESET
               << ownerColor << "[" << msg.owner << "]" << Colors::RESET
               << " " << msg.message << std::endl;
    } else {
        // Fallback to no colors
        output << " [" << getLevelString(msg.level) << "]["
               << msg.owner << "] "
               << msg.message << std::endl;
    }
}

void ConsoleLogWriter::Flush() {
    std::cout.flush();
    std::cerr.flush();
}

std::string ConsoleLogWriter::getLevelString(LogLevel level) const {
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

std::ostream& ConsoleLogWriter::getOutputStream(LogLevel level) const {
    // Use stderr for errors and warnings, stdout for everything else
    switch (level) {
        case FATAL:
        case ERROR:
        case WARNING:
            return std::cerr;
        default:
            return std::cout;
    }
}

const char* ConsoleLogWriter::getLevelColor(LogLevel level) const {
    switch (level) {
        case INTERNAL: return Colors::DIM;
        case FATAL: return Colors::BOLD_RED;
        case ERROR: return Colors::RED;
        case WARNING: return Colors::YELLOW;
        case MESSAGE: return Colors::GREEN;
        case DEBUG: return Colors::BLUE;
        default: return Colors::WHITE;
    }
}

bool ConsoleLogWriter::supportsColors() const {
    // Check if we're in a terminal that supports colors
    const char* term = std::getenv("TERM");
    if (!term) return false;
    
    // Check for common terminal types that support colors
    std::string terminal(term);
    return (terminal.find("xterm") != std::string::npos ||
            terminal.find("linux") != std::string::npos ||
            terminal.find("screen") != std::string::npos ||
            terminal.find("tmux") != std::string::npos ||
            terminal.find("color") != std::string::npos);
}

} // namespace ProjectIE4k
