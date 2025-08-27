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
 * @file ConsoleLogWriter.h
 * @author The GemRB Project
 * @note slightly modified by PIE4K
 */

#pragma once

#include "Logger.h"
#include <iostream>

namespace ProjectIE4k {

class ConsoleLogWriter : public LogWriter {
public:
    ConsoleLogWriter(LogLevel level = DEBUG);
    ~ConsoleLogWriter() override = default;

    void WriteLogMessage(const LogMessage& msg) override;
    void Flush() override;

private:
    std::string getLevelString(LogLevel level) const;
    std::ostream& getOutputStream(LogLevel level) const;
    const char* getLevelColor(LogLevel level) const;
    bool supportsColors() const;
    std::string getCurrentTimestamp() const;
    
    bool colorsEnabled;
};

} // namespace ProjectIE4k 