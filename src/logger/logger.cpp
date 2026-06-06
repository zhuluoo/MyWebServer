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

// File overview: Implements thread-safe in-memory logger with terminal output.

#include "logger/logger.hpp"

#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <thread>

namespace my_web_server {

namespace {

constexpr auto LevelToString(LogLevel level) -> std::string_view {
  switch (level) {
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarn:
      return "WARN";
    case LogLevel::kError:
      return "ERROR";
  }
  return "???";
}

auto FormatTimestamp(const std::chrono::system_clock::time_point& timestamp)
    -> std::string {
  return std::format("{:%F %T}", timestamp);
}

}  // namespace

auto Logger::Instance() -> Logger& {
  static Logger instance;
  return instance;
}

void Logger::Log(LogLevel level, const char* file, int line,
                 std::string message) {
  auto thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());

  std::lock_guard<std::mutex> lock(mutex_);
  entries_.push_back({std::chrono::system_clock::now(), level, file, line,
                      thread_id, std::move(message)});

  if (entries_.size() >= kFlushThreshold) {
    FlushLocked();
  }
}

void Logger::Flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  FlushLocked();
}

void Logger::FlushLocked() {
  for (const auto& entry : entries_) {
    auto line =
        std::format("[{}] [{}] [{}] {}\n", FormatTimestamp(entry.timestamp),
                    LevelToString(entry.level), entry.thread_id, entry.message);
    std::fputs(line.c_str(), stderr);
  }
  entries_.clear();
}

auto Logger::entries() const -> const std::vector<LogEntry>& {
  return entries_;
}

}  // namespace my_web_server
