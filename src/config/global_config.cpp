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

// File overview: Implements global, read-only server configuration.

#include "config/global_config.hpp"

#include <cstdlib>
#include <format>
#include <string_view>

#include "logger/logger.hpp"

namespace my_web_server {

auto GlobalConfig::Instance() -> GlobalConfig& {
  static GlobalConfig instance;
  return instance;
}

auto GlobalConfig::InitFromArgs(int argc, char* argv[]) -> bool {
  if (initialized_) {
    return true;
  }

  ServerConfig cfg;
  for (int i = 1; i < argc; ++i) {
    std::string_view para = argv[i];
    if (para == "--ip") {
      if (i + 1 >= argc) {
        LOG_ERROR("No ip specified.");
        return false;
      }
      cfg.ip = argv[++i];
    } else if (para == "--port") {
      if (i + 1 >= argc) {
        LOG_ERROR("No port specified.");
        return false;
      }

      std::string_view port = argv[++i];
      if (port.size() < 4 || port.size() > 5) {
        LOG_ERROR("Port number must be between 1025 and 65535");
        return false;
      }

      int port_number = 0;
      for (char ch : port) {
        if (ch < '0' || ch > '9') {
          LOG_ERROR("Port number must be between 1025 and 65535");
          return false;
        }
        port_number = port_number * 10 + (ch - '0');
      }

      if (port_number <= 1024 || port_number >= 65536) {
        LOG_ERROR("Port number must be between 1025 and 65535");
        return false;
      }
      cfg.port = port_number;
    } else if (para == "--text") {
      if (i + 1 >= argc) {
        LOG_ERROR("No text specified.");
        return false;
      }
      cfg.custom_response_text = argv[++i];
    } else if (para == "--dir") {
      if (i + 1 >= argc) {
        LOG_ERROR("No directory specified.");
        return false;
      }
      std::string dir = argv[++i];
      if (!dir.empty() && dir[0] == '~') {
        auto home = std::getenv("HOME");
        if (home == nullptr) {
          LOG_ERROR("Fail to get home dir for '~'.");
          return false;
        }
        dir = std::string(home) + dir.substr(1);
      }
      std::error_code ec;
      cfg.server_working_dir = std::filesystem::canonical(dir, ec);
      if (ec) {
        LOG_ERROR(
            std::format("Invalid directory \"{}\" : {}", dir, ec.message()));
        return false;
      }
    } else {
      LOG_ERROR(std::format("Invalid parameter: {}", argv[i]));
      return false;
    }
  }

  config_ = std::move(cfg);
  initialized_ = true;
  return true;
}

auto GlobalConfig::Get() const -> const ServerConfig& {
  if (!initialized_) {
    LOG_ERROR("GlobalConfig not initialized");
    std::exit(1);
  }
  return config_;
}

}  // namespace my_web_server
