// filename: web_server.cpp
#include <arpa/inet.h>
#include <fcntl.h>

#include <cstring>
#if defined(__linux__)
#include <sys/epoll.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#endif
#include <sys/socket.h>
#include <unistd.h>

#include "pool/thread_pool.hpp"
#include "server/web_server.hpp"

WebServer::WebServer(const char* ip, int port, std::size_t max_conn,
                     std::size_t thread_num)
    : ip_(strdup(ip)), port_(port), max_conn_(max_conn) {
  // Initialize listening socket, epoll instance, and thread pool here
  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ == -1) {
    perror("Socket creation error");
    exit(EXIT_FAILURE);
  }
#if defined(__linux__)
  epoll_fd_ = epoll_create(5);
  if (epoll_fd_ == -1) {
    perror("Epoll creation error.");
    exit(EXIT_FAILURE);
  }
#elif defined(__APPLE__)
  kq_fd_ = kqueue();
  if (kq_fd_ == -1) {
    perror("Kqueue creation error.");
    exit(EXIT_FAILURE);
  }
#endif
  thread_pool_ = std::make_unique<ThreadPool>(thread_num);
}

WebServer::~WebServer() {
  close(listen_fd_);
#if defined(__linux__)
  close(epoll_fd_);
#elif defined(__APPLE__)
  close(kq_fd_);
#endif
  free(ip_);
  users_.clear();
  thread_pool_.reset();
}

void WebServer::start_listening() {
  // Enable address reuse to avoid "Address already in use" errors
  int opt = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = inet_addr(ip_);
  address.sin_port = htons(port_);

  int ret = 0;
  ret = bind(listen_fd_, (sockaddr*)&address, sizeof(address));
  if (ret == -1) {
    perror("Bind error");
    exit(EXIT_FAILURE);
  }

  ret = listen(listen_fd_, SOMAXCONN);
  if (ret == -1) {
    perror("Listen error");
    exit(EXIT_FAILURE);
  }

  add_fd(listen_fd_, false);
}

#if defined(__linux__)
void WebServer::run() {
  epoll_event events[MAX_EVENTS];
  start_listening();
  // Main event loop would go here
  while (true) {
    int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
    // error and not interrupted by signal
    if (num_events < 0 && errno != EINTR) {
      perror("Epoll wait error");
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
            perror("Accept error");
            break;
          }
          if (users_.size() >= max_conn_) {
            close(conn_fd);
            continue;
          }
          users_[conn_fd] = std::make_shared<HttpConn>();
          users_[conn_fd]->init(conn_fd, client_addr, epoll_fd_);
          add_fd(conn_fd, true);
        }  // end of accept loop
      } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        // Connection closed or error
        users_.erase(sockfd);
        remove_fd(sockfd);
      } else if (events[i].events & EPOLLIN) {
        // Read event: fill buffer, then dispatch to thread pool for parsing
        auto conn = users_[sockfd];  // Copy shared_ptr, not reference!
        if (!conn->read()) {
          // Read error or connection closed by client
          users_.erase(sockfd);
          remove_fd(sockfd);
          continue;
        }
        thread_pool_->add_task(
            [conn]() { conn->process(); });  // Capture by value!
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

void WebServer::add_fd(int interest_fd, bool one_shot) {
  epoll_event event;
  event.data.fd = interest_fd;
  event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
  if (one_shot) {
    event.events |= EPOLLONESHOT;
  }
  epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, interest_fd, &event);
  set_nonblocking(interest_fd);
}

void WebServer::remove_fd(int interest_fd) {
  epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, interest_fd, nullptr);
  close(interest_fd);
}
#elif defined(__APPLE__)
void WebServer::run() {
  struct kevent events[MAX_EVENTS];
  start_listening();
  while (true) {
    int num_events = kevent(kq_fd_, nullptr, 0, events, MAX_EVENTS, nullptr);
    // Error and not interrupted by signal
    if (num_events < 0 && errno != EINTR) {
      perror("Kqueue wait error");
      break;
    }

    for (int i = 0; i < num_events; ++i) {
      int sockfd = static_cast<int>(events[i].ident);
      uint16_t flags = events[i].flags;
      int16_t filter = events[i].filter;

      if (flags & (EV_ERROR | EV_EOF)) {
        users_.erase(sockfd);
        remove_fd(sockfd);
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
            perror("Accept connection error.");
            break;
          }

          if (users_.size() >= max_conn_) {
            close(conn_fd);
            continue;
          }

          users_[conn_fd] = std::make_shared<HttpConn>();
          users_[conn_fd]->init(conn_fd, client_addr, kq_fd_);
          add_fd(conn_fd, true);
        }
        continue;
      }

      if (filter == EVFILT_READ) {
        auto conn = users_[sockfd];
        if (!conn || !conn->read()) {
          users_.erase(sockfd);
          remove_fd(sockfd);
          continue;
        }
        thread_pool_->add_task([conn]() { conn->process(); });
      }

      if (filter == EVFILT_WRITE) {
        auto conn = users_[sockfd];
        if (!conn || !conn->write()) {
          users_.erase(sockfd);
          remove_fd(sockfd);
        }
      }
    }
  }
}

void WebServer::add_fd(int interest_fd, bool one_shot) {
  struct kevent event;
  uint16_t flags = EV_ADD | EV_ENABLE | EV_CLEAR;
  if (one_shot) {
    flags |= EV_ONESHOT;
  }
  EV_SET(&event, interest_fd, EVFILT_READ, flags, 0, 0,
         (void*)(intptr_t)interest_fd);
  if (kevent(kq_fd_, &event, 1, nullptr, 0, nullptr) == -1) {
    perror("Kqueue add failed.");
  }
  set_nonblocking(interest_fd);
}

void WebServer::remove_fd(int interest_fd) {
  struct kevent event;
  EV_SET(&event, interest_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
  kevent(kq_fd_, &event, 1, nullptr, 0, nullptr);
  close(interest_fd);
}
#endif

auto WebServer::set_nonblocking(int interest_fd) -> int {
  int old_option = fcntl(interest_fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(interest_fd, F_SETFL, new_option);
  return old_option;
}