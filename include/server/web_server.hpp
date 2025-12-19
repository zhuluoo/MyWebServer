#pragma once
// filename: web_server.hpp

#include "http/http_conn.hpp"
#include <memory>
#include <unordered_map>

class ThreadPool;

class WebServer {
public:
  WebServer(const char *ip, int port, std::size_t max_conn = 1000,
            std::size_t thread_num = 8);
  ~WebServer();
  WebServer(const WebServer &) = delete;
  auto operator=(const WebServer &) -> WebServer & = delete;

  // Start the web server
  auto run() -> void;

private:
  // Utilities for managing file descriptors
  auto set_nonblocking(int interest_fd) -> int;
  auto add_fd(int interest_fd, bool one_shot) -> void;
  auto remove_fd(int interest_fd) -> void;
  auto mod_fd(int interest_fd, int ev) -> void;

  void start_listening();

  char *ip_;             // Server IP address
  int port_;             // Server port
  int listen_fd_;        // Listening socket file descriptor
  int epoll_fd_;         // epoll file descriptor
  std::size_t max_conn_; // Maximum number of connections
  std::unordered_map<int, std::shared_ptr<HttpConn>>
      users_; // Map of active HTTP connections

  std::unique_ptr<ThreadPool> thread_pool_; // Thread pool for handling requests
};
