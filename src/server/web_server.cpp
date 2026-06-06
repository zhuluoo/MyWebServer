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

// File overview: Implements WebServer socket setup and event loop.

#include <arpa/inet.h>
#include <fcntl.h>

#include <cstring>
#include <format>
#if defined(__linux__)
#include <sys/epoll.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#endif
#include <sys/socket.h>
#include <unistd.h>

#include "logger/logger.hpp"
#include "pool/thread_pool.hpp"
#include "server/web_server.hpp"

namespace my_web_server {

WebServer::WebServer(const char* ip, int port, std::size_t max_conn,
                     std::size_t thread_num)
    : ip_(strdup(ip)), port_(port), max_conn_(max_conn) {
  // Initialize listening socket, epoll instance, and thread pool here
  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ == -1) {
    LOG_ERROR(std::format("Socket creation error: {}", strerror(errno)));
    exit(EXIT_FAILURE);
  }
#if defined(__linux__)
  mux_fd_ = epoll_create(5);
#elif defined(__APPLE__)
  mux_fd_ = kqueue();
#endif
  if (mux_fd_ == -1) {
    LOG_ERROR("Multiplex creation error.");
    exit(EXIT_FAILURE);
  }
  thread_pool_ = std::make_unique<ThreadPool>(thread_num);
}

WebServer::~WebServer() {
  close(listen_fd_);
  close(mux_fd_);
  free(ip_);
  users_.clear();
  thread_pool_.reset();
}

void WebServer::StartListening() {
  // Enable address reuse to avoid "Address already in use" errors
  int opt = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = inet_addr(ip_);
  address.sin_port = htons(port_);

  int ret = 0;
  ret =
      bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (ret == -1) {
    LOG_ERROR(std::format("Bind error: {}", strerror(errno)));
    exit(EXIT_FAILURE);
  }

  ret = listen(listen_fd_, SOMAXCONN);
  if (ret == -1) {
    LOG_ERROR(std::format("Listen error: {}", strerror(errno)));
    exit(EXIT_FAILURE);
  }

  AddFd(listen_fd_, false);
  LOG_INFO("Start listening successfully.");
}

#if defined(__linux__)
void WebServer::Run() {
  epoll_event events[kMaxEvents];
  StartListening();
  // Main event loop would go here
  while (true) {
    int num_events = epoll_wait(mux_fd_, events, kMaxEvents, -1);
    // error and not interrupted by signal
    if (num_events < 0 && errno != EINTR) {
      LOG_ERROR(std::format("Epoll wait error: {}", strerror(errno)));
      break;
    }

    for (int i = 0; i < num_events; ++i) {
      int sockfd = events[i].data.fd;
      // New connection
      if (sockfd == listen_fd_) {
        // In ET mode, must accept ALL pending connections in a loop
        while (true) {
          sockaddr_in client_addr;
          socklen_t client_addr_len = sizeof(client_addr);
          int conn_fd =
              accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr),
                     &client_addr_len);
          if (conn_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // No more pending connections
              break;
            }
            LOG_ERROR(std::format("Accept error: {}", strerror(errno)));
            break;
          }
          if (users_.size() >= max_conn_) {
            LOG_WARN("Exceeds the maximum connections.");
            close(conn_fd);
            continue;
          }
          users_[conn_fd] = std::make_shared<HttpConn>();
          users_[conn_fd]->Init(conn_fd, client_addr, mux_fd_);
          AddFd(conn_fd, true);
          LOG_INFO(std::format("New connection fd={} ip={} port={}", conn_fd,
                               ntohl(client_addr.sin_addr.s_addr),
                               ntohs(client_addr.sin_port)));
        }  // end of accept loop
      } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        // Connection closed or error
        users_.erase(sockfd);
        RemoveFd(sockfd);
      } else if (events[i].events & EPOLLIN) {
        // Read event: fill buffer, then dispatch to thread pool for parsing
        auto conn = users_[sockfd];  // Copy shared_ptr, not reference!
        if (!conn->Read()) {
          // Read error or connection closed by client
          users_.erase(sockfd);
          RemoveFd(sockfd);
          continue;
        }
        thread_pool_->AddTask(
            [conn]() { conn->Process(); });  // Capture by value!
      } else if (events[i].events & EPOLLOUT) {
        // Write event: attempt to send pending data
        if (!users_[sockfd]->Write()) {
          // write() closes the connection on failure
          users_.erase(sockfd);
          RemoveFd(sockfd);
        }
      }
    }
  }
}

void WebServer::AddFd(int interest_fd, bool one_shot) {
  epoll_event event;
  event.data.fd = interest_fd;
  event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
  if (one_shot) {
    event.events |= EPOLLONESHOT;
  }
  epoll_ctl(mux_fd_, EPOLL_CTL_ADD, interest_fd, &event);
  SetNonblocking(interest_fd);
}

void WebServer::RemoveFd(int interest_fd) {
  epoll_ctl(mux_fd_, EPOLL_CTL_DEL, interest_fd, nullptr);
  close(interest_fd);
}
#elif defined(__APPLE__)
void WebServer::Run() {
  struct kevent events[kMaxEvents];
  StartListening();
  while (true) {
    int num_events = kevent(mux_fd_, nullptr, 0, events, kMaxEvents, nullptr);
    // Error and not interrupted by signal
    if (num_events < 0 && errno != EINTR) {
      LOG_ERROR(std::format("Kqueue wait error: {}", strerror(errno)));
      break;
    }

    for (int i = 0; i < num_events; ++i) {
      int sockfd = static_cast<int>(events[i].ident);
      uint16_t flags = events[i].flags;
      int16_t filter = events[i].filter;

      if (flags & (EV_ERROR | EV_EOF)) {
        users_.erase(sockfd);
        RemoveFd(sockfd);
        continue;
      }

      if (sockfd == listen_fd_) {
        // In ET mode, must handle all pending connections
        while (true) {
          sockaddr_in client_addr;
          socklen_t client_addr_len = sizeof(client_addr);
          int conn_fd =
              accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr),
                     &client_addr_len);
          if (conn_fd < 0) {
            // No more
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              break;
            }
            LOG_ERROR(
                std::format("Accept connection error: {}", strerror(errno)));
            break;
          }

          if (users_.size() >= max_conn_) {
            LOG_WARN("Exceeds the maximum connections.");
            close(conn_fd);
            continue;
          }

          users_[conn_fd] = std::make_shared<HttpConn>();
          users_[conn_fd]->Init(conn_fd, client_addr, mux_fd_);
          AddFd(conn_fd, true);
          LOG_INFO(std::format("New connection fd={} ip={} port={}", conn_fd,
                               ntohl(client_addr.sin_addr.s_addr),
                               ntohs(client_addr.sin_port)));
        }
        continue;
      }

      if (filter == EVFILT_READ) {
        auto conn = users_[sockfd];
        if (!conn || !conn->Read()) {
          users_.erase(sockfd);
          RemoveFd(sockfd);
          continue;
        }
        thread_pool_->AddTask([conn]() { conn->Process(); });
      }

      if (filter == EVFILT_WRITE) {
        auto conn = users_[sockfd];
        if (!conn || !conn->Write()) {
          users_.erase(sockfd);
          RemoveFd(sockfd);
        }
      }
    }
  }
}

void WebServer::AddFd(int interest_fd, bool one_shot) {
  struct kevent event;
  uint16_t flags = EV_ADD | EV_ENABLE | EV_CLEAR;
  if (one_shot) {
    flags |= EV_ONESHOT;
  }
  EV_SET(&event, interest_fd, EVFILT_READ, flags, 0, 0,
         (void*)(intptr_t)interest_fd);
  if (kevent(mux_fd_, &event, 1, nullptr, 0, nullptr) == -1) {
    LOG_WARN(std::format("Kqueue add failed: {}", strerror(errno)));
  }
  SetNonblocking(interest_fd);
}

void WebServer::RemoveFd(int interest_fd) {
  struct kevent event;
  EV_SET(&event, interest_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
  kevent(mux_fd_, &event, 1, nullptr, 0, nullptr);
  close(interest_fd);
}
#endif

auto WebServer::SetNonblocking(int interest_fd) -> int {
  int old_option = fcntl(interest_fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(interest_fd, F_SETFL, new_option);
  return old_option;
}

}  // namespace my_web_server