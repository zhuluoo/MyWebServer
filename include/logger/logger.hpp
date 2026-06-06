/*
 * Copyright (C) 2026 nate <176468367+zhuluoo@users.noreply.github.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// File overview: Thread-safe in-memory logger with INFO/WARN/ERROR levels.

#pragma once

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace my_web_server {

enum class LogLevel { kInfo, kWarn, kError };

struct LogEntry {
  std::chrono::system_clock::time_point timestamp;
  LogLevel level;
  std::string file;
  int line;
  size_t thread_id;
  std::string message;
};

class Logger {
 public:
  static auto Instance() -> Logger&;

  void Log(LogLevel level, const char* file, int line, std::string message);
  void Flush();

  auto entries() const -> const std::vector<LogEntry>&;

 private:
  Logger() = default;

  void FlushLocked();

  static constexpr size_t kFlushThreshold = 8;

  std::mutex mutex_;
  std::vector<LogEntry> entries_;
};

}  // namespace my_web_server

#define LOG_INFO(msg)                                                   \
  my_web_server::Logger::Instance().Log(my_web_server::LogLevel::kInfo, \
                                        __FILE__, __LINE__, msg)

#define LOG_WARN(msg)                                                   \
  my_web_server::Logger::Instance().Log(my_web_server::LogLevel::kWarn, \
                                        __FILE__, __LINE__, msg)

#define LOG_ERROR(msg)                                                   \
  my_web_server::Logger::Instance().Log(my_web_server::LogLevel::kError, \
                                        __FILE__, __LINE__, msg)
