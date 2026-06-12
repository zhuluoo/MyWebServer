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

#define WIN32_LEAN_AND_MEAN
#define FD_SETSIZE 1024
#include <winsock2.h>
#include <ws2tcpip.h>

#include <memory>
#include <unordered_map>

#include "http/http_conn.hpp"
#include "server/windows_poller.hpp"

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

  void run();

 private:
  auto set_nonblocking(SOCKET fd) -> int;
  void add_fd(SOCKET fd, bool one_shot);
  void remove_fd(SOCKET fd);

  void start_listening();

  char* ip_;
  int port_;
  SOCKET listen_fd_;
  SelectPoller poller_;
  std::size_t max_conn_;
  std::unordered_map<SOCKET, std::shared_ptr<HttpConn>> users_;

  std::unique_ptr<ThreadPool> thread_pool_;
};

}  // namespace my_web_server
