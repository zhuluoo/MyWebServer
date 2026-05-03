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

#include <iostream>

#include "server/web_server.hpp"

auto main(int argc, char* argv[]) -> int {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " ip_address port_number\n";
    return 1;
  }
  int port = std::stoi(argv[2]);
  if (port <= 1024 || port >= 65536) {
    std::cerr << "Port number must be between 1025 and 65535\n";
    return 1;
  }

  WebServer server(argv[1], port);
  server.run();
  return 0;
}