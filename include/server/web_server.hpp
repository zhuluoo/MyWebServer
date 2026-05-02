#pragma once
// filename: web_server.hpp

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
