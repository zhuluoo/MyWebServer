// filename: web_server.cpp
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "server/web_server.hpp"
#include "pool/thread_pool.hpp"

// Constructor
WebServer::WebServer(const char *ip, int port, std::size_t max_conn,
                     std::size_t thread_num)
    : ip_(strdup(ip)), port_(port), max_conn_(max_conn) {
  // Initialize listening socket, epoll instance, and thread pool here
  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ == -1) {
    perror("Socket creation error");
    exit(EXIT_FAILURE);
  }
  epoll_fd_ = epoll_create(5);
  if (epoll_fd_ == -1) {
    perror("Epoll creation error");
    exit(EXIT_FAILURE);
  }
  thread_pool_ = std::make_unique<ThreadPool>(thread_num);
}

// Destructor
WebServer::~WebServer() {
  close(listen_fd_);
  close(epoll_fd_);
  free(ip_);
  users_.clear();
  thread_pool_.reset();
}

// Set a file descriptor to non-blocking mode
auto WebServer::set_nonblocking(int interest_fd) -> int {
  int old_option = fcntl(interest_fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(interest_fd, F_SETFL, new_option);
  return old_option;
}

// Add a file descriptor to the epoll instance
auto WebServer::add_fd(int interest_fd, bool one_shot) -> void {
  epoll_event event;
  event.data.fd = interest_fd;
  event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
  if (one_shot) {
    event.events |= EPOLLONESHOT;
  }
  epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, interest_fd, &event);
  set_nonblocking(interest_fd);
}

// Remove a file descriptor from the epoll instance
auto WebServer::remove_fd(int interest_fd) -> void {
  epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, interest_fd, nullptr);
  close(interest_fd);
}

// Modify the events associated with a file descriptor in the epoll instance
auto WebServer::mod_fd(int interest_fd, int ev) -> void {
  epoll_event event;
  event.data.fd = interest_fd;
  event.events = ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
  epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, interest_fd, &event);
}

void WebServer::start_listening() {
  sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = inet_addr(ip_);
  address.sin_port = htons(port_);

  int ret = 0;
  ret = bind(listen_fd_, (sockaddr *)&address, sizeof(address));
  if (ret == -1) {
    perror("Bind error");
    exit(EXIT_FAILURE);
  }

  ret = listen(listen_fd_, 5);
  if (ret == -1) {
    perror("Listen error");
    exit(EXIT_FAILURE);
  }

  add_fd(listen_fd_, false);
}

// Start the web server
auto WebServer::run() -> void {
  std::size_t max_events = 10000;
  epoll_event events[max_events];
  start_listening();
  // Main event loop would go here
  while (true) {
    int num_events = epoll_wait(epoll_fd_, events, max_events, -1);
    // error and not interrupted by signal
    if (num_events < 0 && errno != EINTR) {
      perror("Epoll wait error");
      break;
    }

    // Handle events here
    for (int i = 0; i < num_events; ++i) {
      int sockfd = events[i].data.fd;
      // New connection
      if (sockfd == listen_fd_) {
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int conn_fd =
            accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr),
                   &client_addr_len);
        if (conn_fd < 0) {
          perror("Accept error");
          continue;
        }
        if (users_.size() >= max_conn_) {
          close(conn_fd);
          continue;
        }
        users_[conn_fd] = std::make_shared<HttpConn>();
        users_[conn_fd]->init(conn_fd, client_addr, epoll_fd_);
        add_fd(conn_fd, true);
      } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        // Connection closed or error
        users_.erase(sockfd);
        remove_fd(sockfd);
      } else if (events[i].events & EPOLLIN) {
        // Read event: fill buffer, then dispatch to thread pool for parsing
        auto &conn = users_[sockfd];
        if (!conn -> read()) {
          // Read error or connection closed by client
          users_.erase(sockfd);
          remove_fd(sockfd);
          continue;
        }
        thread_pool_->add_task([&conn]() { conn->process(); });
      } else if (events[i].events & EPOLLOUT) {
        // Write event: attempt to send pending data
        if (!users_[sockfd]->write()) {
          // write() closes the connection on failure
          users_.erase(sockfd);
          remove_fd(sockfd);
        }
      }
    }
  }
}
