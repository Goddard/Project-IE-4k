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
 * @file FileLogWriter.h
 * @author The GemRB Project
 * @note slightly modified by PIE4K
 */

#pragma once

#include "Logger.h"
#include <fstream>
#include <filesystem>

namespace ProjectIE4k {

class FileLogWriter : public LogWriter {
private:
    std::ofstream logFile;
    std::mutex fileMutex;

public:
    FileLogWriter(LogLevel level = DEBUG);
    ~FileLogWriter() override;

    void WriteLogMessage(const LogMessage& msg) override;
    void Flush() override;

private:
    std::string getLevelString(LogLevel level) const;
    std::string getCurrentTimestamp() const;
    std::filesystem::path getLogFilePath() const;
};

} // namespace ProjectIE4k 