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

// File overview: Defines global, read-only server configuration.

#pragma once

#include <optional>
#include <string>

namespace my_web_server {

struct ServerConfig {
  std::string ip{"127.0.0.1"};
  int port{8001};
  std::optional<std::string> http_200_return_text{};
};

class GlobalConfig {
 public:
  static auto Instance() -> GlobalConfig&;
  auto InitFromArgs(int argc, char* argv[]) -> bool;
  auto Get() const -> const ServerConfig&;

 private:
  GlobalConfig() = default;

  ServerConfig config_{};
  bool initialized_ = false;
};

}  // namespace my_web_server
