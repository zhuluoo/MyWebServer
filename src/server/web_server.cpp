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

// File overview: Implements WebServer socket setup and select()-based event
// loop.

#define WIN32_LEAN_AND_MEAN
#define FD_SETSIZE 1024
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <cstring>
#include <iostream>

#include "pool/thread_pool.hpp"
#include "server/web_server.hpp"
#include "server/windows_poller.hpp"

namespace my_web_server {

WebServer::WebServer(const char* ip, int port, std::size_t max_conn,
                     std::size_t thread_num)
    : ip_(strdup(ip)), port_(port), max_conn_(max_conn) {
  WSADATA wsaData;
  int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (result != 0) {
    std::cerr << "WSAStartup failed: " << result << "\n";
    exit(EXIT_FAILURE);
  }

  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ == INVALID_SOCKET) {
    std::cerr << "socket error: " << WSAGetLastError() << "\n";
    exit(EXIT_FAILURE);
  }
  thread_pool_ = std::make_unique<ThreadPool>(thread_num);
}

WebServer::~WebServer() {
  closesocket(listen_fd_);
  free(ip_);
  users_.clear();
  thread_pool_.reset();
  WSACleanup();
}

void WebServer::start_listening() {
  int opt = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char*>(&opt), sizeof(opt));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = inet_addr(ip_);
  address.sin_port = htons(port_);

  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address),
           sizeof(address)) == SOCKET_ERROR) {
    std::cerr << "bind error: " << WSAGetLastError() << "\n";
    exit(EXIT_FAILURE);
  }

  if (listen(listen_fd_, SOMAXCONN) == SOCKET_ERROR) {
    std::cerr << "listen error: " << WSAGetLastError() << "\n";
    exit(EXIT_FAILURE);
  }

  add_fd(listen_fd_, false);
}

void WebServer::run() {
  start_listening();
  std::cout << std::format("Server listening on {}:{}\n", ip_, port_);

  std::vector<SelectPoller::Event> events;
  while (true) {
    events.clear();
    int num = poller_.wait(events, -1);
    if (num < 0 && WSAGetLastError() != WSAEINTR) {
      std::cerr << "select error: " << WSAGetLastError() << "\n";
      break;
    }

    for (const auto& ev : events) {
      SOCKET fd = ev.fd;

      if (fd == listen_fd_) {
        while (true) {
          sockaddr_in client_addr{};
          socklen_t len = sizeof(client_addr);
          SOCKET conn_fd = accept(
              listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
          if (conn_fd == INVALID_SOCKET) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
              break;
            }
            std::cerr << "accept error: " << WSAGetLastError() << "\n";
            break;
          }
          if (users_.size() >= max_conn_) {
            closesocket(conn_fd);
            continue;
          }
          users_[conn_fd] = std::make_shared<HttpConn>();
          users_[conn_fd]->init(conn_fd, client_addr, &poller_);
          add_fd(conn_fd, true);
        }
      } else if (ev.type == SelectPoller::EventType::kError) {
        users_.erase(fd);
        remove_fd(fd);
      } else if (ev.type == SelectPoller::EventType::kRead) {
        auto conn = users_[fd];
        if (!conn->read()) {
          users_.erase(fd);
          remove_fd(fd);
          continue;
        }
        thread_pool_->add_task([conn]() { conn->process(); });
      } else if (ev.type == SelectPoller::EventType::kWrite) {
        if (!users_[fd]->write()) {
          users_.erase(fd);
          remove_fd(fd);
        }
      }
    }
  }
}

void WebServer::add_fd(SOCKET fd, bool one_shot) {
  poller_.add(fd, one_shot);
  set_nonblocking(fd);
}

void WebServer::remove_fd(SOCKET fd) {
  poller_.del(fd);
  closesocket(fd);
}

auto WebServer::set_nonblocking(SOCKET fd) -> int {
  u_long mode = 1;
  return ioctlsocket(fd, FIONBIO, &mode);
}

}  // namespace my_web_server
