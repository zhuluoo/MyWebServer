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

// File overview: Entry point that validates args and runs WebServer.

#include <format>

#include "config/global_config.hpp"
#include "logger/logger.hpp"
#include "server/web_server.hpp"

auto main(int argc, char* argv[]) -> int {
  auto& config = my_web_server::GlobalConfig::Instance();
  if (!config.InitFromArgs(argc, argv)) {
    return 1;
  }

  const auto& cfg = config.Get();
  LOG_INFO(std::format("Initializing web server at ip {} port {} dir {}.",
                       cfg.ip, cfg.port, cfg.server_working_dir.string()));

  my_web_server::WebServer server(cfg.ip.c_str(), cfg.port);
  server.Run();
  return 0;
}