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

// File overview: Defines WebServer for accepting and dispatching HTTP connections.

#pragma once

#include <memory>
#include <unordered_map>

#include "http/http_conn.hpp"

constexpr int MAX_EVENTS = 10000;

class ThreadPool;

class WebServer {
 public:
  WebServer(const char* ip, int port, std::size_t max_conn = 1000,
            std::size_t thread_num = 8);
  ~WebServer();
  WebServer(const WebServer&) = delete;
  auto operator=(const WebServer&) -> WebServer& = delete;

  // Start the web server
  void run();

 private:
  // Utilities for managing file descriptors
  auto set_nonblocking(int interest_fd) -> int;
  void add_fd(int interest_fd, bool one_shot);
  void remove_fd(int interest_fd);

  void start_listening();

  char* ip_;       // Server IP address
  int port_;       // Server port
  int listen_fd_;  // Listening socket file descriptor
#if defined(__linux__)
  int epoll_fd_;  // epoll file descriptor
#elif defined(__APPLE__)
  int kq_fd_;  // kqueue file descriptor
#endif
  std::size_t max_conn_;  // Maximum number of connections
  std::unordered_map<int, std::shared_ptr<HttpConn>>
      users_;  // Map of active HTTP connections

  std::unique_ptr<ThreadPool> thread_pool_;
};
