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

// File overview: Implements HttpConn parsing, read, and write logic.

#include "http/http_conn.hpp"

#include <fcntl.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#if defined(__linux__)
#include <sys/epoll.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#endif
#include <sys/socket.h>
#include <unistd.h>

#include <format>

#include "utils/resource_utils.hpp"

namespace my_web_server {

namespace {
constexpr std::string_view kHeader500Empty =
    "HTTP/1.1 500 Internal Server Error\r\n"
    "Content-Length: 0\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n";
constexpr std::string_view kHeader500 =
    "HTTP/1.1 500 Internal Server Error\r\n"
    "Content-Length: {}\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n";
constexpr std::string_view kHeader400 =
    "HTTP/1.1 400 Bad Request\r\n"
    "Content-Length: {}\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n";
constexpr std::string_view kHeader403 =
    "HTTP/1.1 403 Forbidden\r\n"
    "Content-Length: {}\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n";
constexpr std::string_view kHeader404 =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: {}\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n";
constexpr std::string_view kHeader200 =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: {}\r\n"
    "Content-Type: text/html\r\n"
    "Connection: {}\r\n"
    "\r\n";
constexpr std::string_view kHeader200File =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: {}\r\n"
    "Content-Type: application/octet-stream\r\n"
    "Connection: {}\r\n"
    "\r\n";
auto load_body(const char* path, std::string* out) -> bool {
  std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return false;
  }
  auto size = file.tellg();
  file.seekg(0, std::ios::beg);
  if (size <= 0) {
    return false;
  }
  out->resize(size);
  if (file.read(out->data(), size)) {
    return true;
  }
  return false;
}
}  // namespace

HttpConn::HttpConn() {
  memset(read_buf_, '\0', sizeof(read_buf_));
  memset(write_buf_, '\0', sizeof(write_buf_));
}

HttpConn::~HttpConn() = default;

void HttpConn::init() {
  memset(read_buf_, '\0', sizeof(read_buf_));
  read_idx_ = 0;
  checked_idx_ = 0;
  start_line_ = 0;

  memset(write_buf_, '\0', sizeof(write_buf_));
  write_idx_ = 0;

  version_ = 0;
  url_.clear();
  host_.clear();
  linger_ = false;

  method_ = GET;
  check_state_ = CHECK_STATE_REQUESTLINE;
  line_status_ = LINE_OK;

  file_address_ = nullptr;
  file_stat_ = 0;
}

void HttpConn::init(int sockfd, const sockaddr_in& addr, int fd) {
  sockfd_ = sockfd;
  address_ = addr;
#if defined(__linux__)
  epollfd_ = fd;
#elif defined(__APPLE__)
  kq_ = fd;
#endif
  init();
}

void HttpConn::process() {
  HTTP_CODE read_ret = process_read();
  if (read_ret == NO_REQUEST) {
    // Need to read more data; re-arm EPOLLIN for this socket (one-shot)
    mod_fd(sockfd_, NetEvent::READ_EVENT);
    return;
  }
  bool write_ret = process_write(read_ret);
  if (!write_ret) {
    // Failed to process write, set write_idx_ to -1 to indicate no data to send
    write_idx_ = -1;
  }
  // Ready to send response in write_buf_, switch to EPOLLOUT for sending
  mod_fd(sockfd_, NetEvent::WRITE_EVENT);
}

// Read available data from the socket (non-blocking ET loop)
auto HttpConn::read() -> bool {
  if (read_idx_ >= static_cast<int>(sizeof(read_buf_) - 1)) {
    return false;  // buffer full -> treat as error
  }

  ssize_t bytes_read = 0;
  // Non-blocking read loop
  while (true) {
    bytes_read = recv(sockfd_, read_buf_ + read_idx_,
                      static_cast<int>(sizeof(read_buf_) - read_idx_ - 1), 0);
    if (bytes_read == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // No more data for now
        return true;
      }
      return false;
    }

    // Client closed connection
    if (bytes_read == 0) {
      return false;
    }

    read_idx_ += static_cast<int>(bytes_read);
    // Buffer overflow
    if (read_idx_ >= static_cast<int>(sizeof(read_buf_) - 1)) {
      return false;
    }
  }
}

// Send the prepared response to the client (non-blocking ET loop)
auto HttpConn::write() -> bool {
  // Write process failed, close connection
  if (write_idx_ == -1) {
    return false;
  }
  ssize_t bytes_written = 0;
  ssize_t total_bytes_written = 0;

  // Non-blocking write loop
  while (total_bytes_written < write_idx_) {
    bytes_written = send(sockfd_, write_buf_ + total_bytes_written,
                         write_idx_ - total_bytes_written, 0);
    if (bytes_written == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Socket buffer full; re-arm EPOLLOUT for next write attempt
        mod_fd(sockfd_, NetEvent::WRITE_EVENT);
        return true;  // Keep connection alive for retry
      }
      return false;
    }

    // Connection closed
    if (bytes_written == 0) {
      return false;
    }

    total_bytes_written += bytes_written;
  }

  if (!linger_) {
    return false;  // Indicate to close connection
  }

  init();
  mod_fd(sockfd_, NetEvent::READ_EVENT);
  return true;
}

// Handle the HTTP connection
auto HttpConn::process_read() -> HTTP_CODE {
  line_status_ = parse_line();
  HTTP_CODE ret = NO_REQUEST;
  char* text = nullptr;
  // Main state machine loop
  while (line_status_ == LINE_OK) {
    text = read_buf_ + start_line_;
    start_line_ = checked_idx_;
    switch (check_state_) {
      case CHECK_STATE_REQUESTLINE: {
        ret = parse_request(text);
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        }
        break;
      }

      case CHECK_STATE_HEADER: {
        ret = parse_header(text);
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        }
        if (ret == GET_REQUEST) {
          return GET_REQUEST;
        }
        // We ignore other cases for now
        break;
      }

      case CHECK_STATE_CONTENT: {
        ret = parse_content();
        /* TODO */
        break;
      }

      default: {
        return INTERNAL_ERROR;
      }
    }

    line_status_ = parse_line();
  }
  return NO_REQUEST;
}

// Process the write operation
auto HttpConn::process_write(HTTP_CODE ret) -> bool {
  auto add_server_error = [&]() -> bool {
    linger_ = false;  // Close connection
    return add_response(kHeader500Empty);
  };

  switch (ret) {
    case INTERNAL_ERROR: {
      std::string body;
      auto path = (resource_dir() / "html" / "500.html").string();
      if (!load_body(path.c_str(), &body)) {
        return add_server_error();
      }
      if (!add_response(std::format(kHeader500, body.size()))) {
        return false;
      }
      return add_response(body);
    }

    case BAD_REQUEST: {
      std::string body;
      auto path = (resource_dir() / "html" / "400.html").string();
      if (!load_body(path.c_str(), &body)) {
        return add_server_error();
      }
      if (!add_response(std::format(kHeader400, body.size()))) {
        return false;
      }
      return add_response(body);
    }

    case FORBIDDEN_REQUEST: {
      std::string body;
      auto path = (resource_dir() / "html" / "403.html").string();
      if (!load_body(path.c_str(), &body)) {
        return add_server_error();
      }
      if (!add_response(std::format(kHeader403, body.size()))) {
        return false;
      }
      return add_response(body);
    }

    case NO_RESOURCE: {
      std::string body;
      auto path = (resource_dir() / "html" / "404.html").string();
      if (!load_body(path.c_str(), &body)) {
        return add_server_error();
      }
      if (!add_response(std::format(kHeader404, body.size()))) {
        return false;
      }
      return add_response(body);
    }

    case GET_REQUEST: {
      std::string body;
      auto path = (resource_dir() / "html" / "200.html").string();
      if (!load_body(path.c_str(), &body)) {
        return add_server_error();
      }
      if (!add_response(std::format(kHeader200, body.size(),
                                    (linger_ ? "keep-alive" : "close")))) {
        return false;
      }
      return add_response(body);
    }

    case FILE_REQUEST: {
      // Headers only; file body will be sent separately (e.g. via
      // mmap/sendfile)
      return add_response(std::format(kHeader200File, file_stat_,
                                      (linger_ ? "keep-alive" : "close")));
    }

    default: {
      return false;
    }
  }
}

// Parse a line and determine its status (Search \r\n)
auto HttpConn::parse_line() -> LINE_STATUS {
  char tmp;
  for (; checked_idx_ < read_idx_; ++checked_idx_) {
    tmp = read_buf_[checked_idx_];
    if (tmp == '\r') {
      if ((checked_idx_ + 1) == read_idx_) {
        return LINE_OPEN;
      }

      if (read_buf_[checked_idx_ + 1] == '\n') {
        read_buf_[checked_idx_++] = '\0';  // For convenience
        read_buf_[checked_idx_++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }

    if (tmp == '\n') {
      if (checked_idx_ > 0 && read_buf_[checked_idx_ - 1] == '\r') {
        read_buf_[checked_idx_ - 1] = '\0';
        read_buf_[checked_idx_++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }
  }

  return LINE_OPEN;
}

auto HttpConn::parse_request(char* text) -> HTTP_CODE {
  int start = 0;
  int end = 0;
  version_ = 0;
  // Parse method
  while (text[end] != '\0') {
    if (text[end] == ' ') {
      std::string method(text + start, end - start);
      if (method == "GET") {
        method_ = GET;
      } else {
        return BAD_REQUEST;  // We only support GET for now
      }
      break;
    }
    ++end;
  }

  if (text[end] == '\0') {
    return BAD_REQUEST;
  }
  start = ++end;

  // Parse URL
  while (text[end] != '\0') {
    if (text[end] == ' ') {
      url_ = std::string(text + start, end - start);
      break;
    }
    ++end;
  }

  if (text[end] == '\0') {
    return BAD_REQUEST;
  }
  start = ++end;

  // Parse HTTP version
  while (text[end] != '\0') {
    if (text[end + 1] == '\0') {
      std::string version(text + start, end - start + 1);
      if (version == "HTTP/1.1") {
        version_ = 1;
      } else {
        return BAD_REQUEST;  // We only support HTTP/1.1 for now
      }
      break;
    }
    ++end;
  }

  if (version_ == 0) {
    return BAD_REQUEST;
  }

  check_state_ = CHECK_STATE_HEADER;
  return NO_REQUEST;
}

auto HttpConn::parse_header(char* text) -> HTTP_CODE {
  // \r\n replaced to \0 during parsing
  std::string_view line(text);
  // An empty line indicates the end of headers
  if (line.empty()) {
    return GET_REQUEST;
  }

  auto colon_idx = line.find(':');
  if (colon_idx == std::string_view::npos) {
    return BAD_REQUEST;
  }

  std::string_view key = line.substr(0, colon_idx);
  std::string_view value = line.substr(colon_idx + 1);
  while (!value.empty() && value.front() == ' ') {
    value.remove_prefix(1);
  }

  auto is_equal_ncase = [](std::string_view s1, std::string_view s2) {
    return s1.size() == s2.size() &&
           std::equal(s1.begin(), s1.end(), s2.begin(), [](char a, char b) {
             return std::tolower(static_cast<unsigned char>(a)) ==
                    std::tolower(static_cast<unsigned char>(b));
           });
  };

  if (is_equal_ncase(key, "Host")) {
    host_ = std::string(value);
  } else if (is_equal_ncase(key, "Connection")) {
    if (is_equal_ncase(value, "keep-alive")) {
      linger_ = true;
    }
  } else {
    // Other headers are ignored for now
  }
  return NO_REQUEST;  // Need to read more
}

// Parse the HTTP message body
auto HttpConn::parse_content() -> HTTP_CODE {
  // For GET requests there is typically no message body.
  // Accept this as a complete request.
  return GET_REQUEST;
}

// Add response data to the write buffer
auto HttpConn::add_response(std::string_view text) -> bool {
  if (write_idx_ < 0) {
    return false;
  }
  size_t remaining = sizeof(write_buf_) - static_cast<size_t>(write_idx_);
  if (text.size() > remaining) {
    return false;
  }
  std::memcpy(write_buf_ + write_idx_, text.data(), text.size());
  write_idx_ += static_cast<int>(text.size());
  return true;
}

// Set a file descriptor to non-blocking mode
auto HttpConn::set_nonblocking(int interest_fd) -> int {
  int old_option = fcntl(interest_fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(interest_fd, F_SETFL, new_option);
  return old_option;
}

#if defined(__linux__)
void HttpConn::mod_fd(int interest_fd, NetEvent ev) {
  int event_flags = -1;
  if (ev == NetEvent::READ_EVENT) {
    event_flags = EPOLLREAD;
  } else if (ev == NetEvent::WRITE_EVENT) {
    event_flags = EPOLLOUT;
  } else {
    return;
  }

  epoll_event event;
  event.data.fd = interest_fd;
  // Set new events while keeping ET, RDHUP, and ONESHOT flags
  event.events = event_flags | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
  epoll_ctl(epollfd_, EPOLL_CTL_MOD, interest_fd, &event);
}
#elif defined(__APPLE__)
void HttpConn::mod_fd(int interest_fd, NetEvent ev) {
  int16_t filter = -1;
  if (ev == NetEvent::READ_EVENT) {
    filter = EVFILT_READ;
  } else if (ev == NetEvent::WRITE_EVENT) {
    filter = EVFILT_WRITE;
  } else {
    return;
  }

  struct kevent event;
  EV_SET(&event, interest_fd, filter,
         EV_ADD | EV_ENABLE | EV_CLEAR | EV_ONESHOT, 0, 0,
         (void*)(intptr_t)interest_fd);
  kevent(kq_, &event, 1, nullptr, 0, nullptr);
}
#endif

}  // namespace my_web_server