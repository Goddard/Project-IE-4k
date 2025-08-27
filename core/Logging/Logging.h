/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 */

/**
 * @file Logging.h
 * @author The GemRB Project
 * @note slightly modified by PIE4K
 */

#pragma once

#include <string>
#include <memory>
#include <format>

namespace ProjectIE4k {

#if defined(ERROR)
#undef ERROR
#endif

enum LogLevel {
    INTERNAL = 255,
    FATAL = 0,
    ERROR = 1,
    WARNING = 2,
    MESSAGE = 3,
    DEBUG = 4,
    count
};

class LogWriter;
class Logger;

struct LogMessage {
    LogLevel level = DEBUG;
    std::string owner;
    std::string message;

    LogMessage(LogLevel level, std::string owner, std::string message)
        : level(level), owner(std::move(owner)), message(std::move(message)) {}
};

void ToggleLogging(bool enabled);
void AddLogWriter(std::shared_ptr<LogWriter> writer);
void SetConsoleWindowLogLevel(LogLevel level);
void LogMsg(LogLevel level, const char* owner, const char* message);
void FlushLogs();

void InitializeLogging();
void ShutdownLogging();

template<typename... ARGS>
void Log(LogLevel level, const char* owner, const char* message, ARGS&&... args)
{
    auto formattedMsg = std::vformat(message, std::make_format_args(args...));
    LogMsg(level, owner, formattedMsg.c_str());
}

template<typename... ARGS>
[[noreturn]]
void error(const char* owner, const char* format, ARGS&&... args)
{
    auto formattedMsg = std::vformat(format, std::make_format_args(args...));
    LogMsg(FATAL, owner, formattedMsg.c_str());
    exit(1);
}

} // namespace ProjectIE4k