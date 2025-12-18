#pragma once
// filename: web_server.hpp

#include "http/http_conn.hpp"
#include <vector>
#include <memory>

class ThreadPool;

class WebServer {
public:
    WebServer(const char *ip, int port, std::size_t max_conn = 1000, std::size_t thread_num = 8);
    ~WebServer();
    WebServer(const WebServer &) = delete;
    auto operator=(const WebServer &) -> WebServer & = delete;
    
    // Start the web server
    auto run() -> void;
private:
  // Set a file descriptor to non-blocking mode
  auto set_nonblocking(int interest_fd) -> int;
  // Add a file descriptor to the epoll instance
  auto add_fd(int interest_fd, bool one_shot) -> void;
  // Remove a file descriptor from the epoll instance
  auto remove_fd(int interest_fd) -> void;
  // Modify the events associated with a file descriptor in the epoll instance
  auto mod_fd(int interest_fd, int ev) -> void;
  
  void start_listening();

  char *ip_;                    // Server IP address
  int port_;                    // Server port
  int listen_fd_;               // Listening socket file descriptor
  int epoll_fd_;                // epoll file descriptor
  std::size_t max_conn_;        // Maximum number of connections
  std::vector<HttpConn> users_; // Array of HTTP connection objects

  std::unique_ptr<ThreadPool> thread_pool_; // Thread pool for handling requests
};
