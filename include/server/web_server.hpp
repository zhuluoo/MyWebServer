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

// File overview: Defines WebServer for accepting and dispatching HTTP
// connections.

#pragma once

#include <memory>
#include <unordered_map>

#include "http/http_conn.hpp"

namespace my_web_server {

constexpr int kMaxEvents = 10000;
constexpr int kDefaultMaxConns = 1000;

class ThreadPool;

class WebServer {
 public:
  WebServer(const char* ip, int port, std::size_t max_conn = kDefaultMaxConns,
            std::size_t thread_num = 8);
  ~WebServer();
  WebServer(const WebServer&) = delete;
  auto operator=(const WebServer&) -> WebServer& = delete;
  WebServer(WebServer&&) = delete;
  auto operator=(WebServer&&) -> WebServer& = delete;

  // Start the web server
  void Run();

 private:
  // Utilities for managing file descriptors
  auto SetNonblocking(int interest_fd) -> int;
  void AddFd(int interest_fd, bool one_shot);
  void RemoveFd(int interest_fd);

  void StartListening();
  void SetupSignalHandling();

  void CleanUp();

  bool running_ = true;
  char* ip_;              // Server IP address
  int port_;              // Server port
  std::size_t max_conn_;  // Maximum number of connections

  int listen_fd_;         // Listening socket file descriptor
  int mux_fd_;            // epoll/kqueue file descriptor
  std::unordered_map<int, std::shared_ptr<HttpConn>>
      users_;  // Map of active HTTP connections
  std::unique_ptr<ThreadPool> thread_pool_;
};

}  // namespace my_web_server
