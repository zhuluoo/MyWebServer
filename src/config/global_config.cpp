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
#include <iostream>
#include <string_view>

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
        std::cerr << "No ip specified.\n";
        return false;
      }
      cfg.ip = argv[++i];
    } else if (para == "--port") {
      if (i + 1 >= argc) {
        std::cerr << "No port specified.\n";
        return false;
      }

      std::string_view port = argv[++i];
      if (port.size() < 4 || port.size() > 5) {
        std::cerr << "Port number must be between 1025 and 65535\n";
        return false;
      }

      int port_number = 0;
      for (char ch : port) {
        if (ch < '0' || ch > '9') {
          std::cerr << "Port number must be between 1025 and 65535\n";
          return false;
        }
        port_number = port_number * 10 + (ch - '0');
      }

      if (port_number <= 1024 || port_number >= 65536) {
        std::cerr << "Port number must be between 1025 and 65535\n";
        return false;
      }
      cfg.port = port_number;
    } else if (para == "--text") {
      if (i + 1 >= argc) {
        std::cerr << "No text specified.\n";
        return false;
      }
      cfg.custom_response_text = argv[++i];
    } else if (para == "--dir") {
      if (i + 1 >= argc) {
        std::cerr << "No directory specified.\n";
        return false;
      }
      std::string dir = argv[++i];
      if (!dir.empty() && dir[0] == '~') {
        auto home = std::getenv("HOME");
        if (home == nullptr) {
          std::cerr << "Fail to get home dir for '~'.\n";
          return false;
        }
        dir = std::string(home) + dir.substr(1);
      }
      std::error_code ec;
      cfg.server_working_dir = std::filesystem::canonical(dir, ec);
      if (ec) {
        std::cerr << std::format("Invalid directory \"{}\" : {}\n", dir,
                                 ec.message());
        return false;
      }
    } else {
      std::cerr << std::format("Invalid parameter: {}\n", argv[i]);
      return false;
    }
  }

  config_ = std::move(cfg);
  initialized_ = true;
  return true;
}

auto GlobalConfig::Get() const -> const ServerConfig& {
  if (!initialized_) {
    std::cerr << "GlobalConfig not initialized\n";
    std::exit(1);
  }
  return config_;
}

}  // namespace my_web_server
