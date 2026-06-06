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

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#if defined(__linux__)
#include <sys/epoll.h>
#include <sys/sendfile.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#endif
#include <sys/socket.h>
#include <unistd.h>

#include <format>

#include "config/global_config.hpp"
#include "http/http_response_templates.hpp"
#include "logger/logger.hpp"
#include "utils/resource_utils.hpp"

namespace my_web_server {

namespace {
auto load_body(const char* path, std::string* out) -> bool {
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  out->assign(std::istreambuf_iterator<char>(file),
              std::istreambuf_iterator<char>());
  return true;
}
}  // namespace

HttpConn::HttpConn() {
  memset(read_buf_, '\0', sizeof(read_buf_));
  memset(write_buf_, '\0', sizeof(write_buf_));
}

HttpConn::~HttpConn() = default;

void HttpConn::Init() {
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

  file_size_ = 0;

  if (file_fd_ != -1) {
    close(file_fd_);
    file_fd_ = -1;
  }
  write_buf_sent_ = 0;
  file_bytes_sent_ = 0;

  server_working_dir_.clear();
  const auto& cfg = GlobalConfig::Instance().Get();
  if (!cfg.server_working_dir.empty()) {
    server_working_dir_ = cfg.server_working_dir;
  }
}

void HttpConn::Init(int sockfd, const sockaddr_in& addr, int fd) {
  sockfd_ = sockfd;
  address_ = addr;
  mux_fd_ = fd;
  Init();
}

void HttpConn::Process() {
  HTTP_CODE read_ret = ProcessRead();
  if (read_ret == NO_REQUEST) {
    // Need to read more data; re-arm EPOLLIN for this socket (one-shot)
    ModFd(sockfd_, NetEvent::READ_EVENT);
    return;
  }
  bool write_ret = ProcessWrite(read_ret);
  if (!write_ret) {
    // Failed to process write, set write_idx_ to -1 to indicate no data to send
    write_idx_ = -1;
  }
  // Ready to send response in write_buf_, switch to EPOLLOUT for sending
  ModFd(sockfd_, NetEvent::WRITE_EVENT);

  LOG_INFO(std::format("{}:{} {} -> {}", ntohl(address_.sin_addr.s_addr),
                       ntohs(address_.sin_port), url_,
                       static_cast<int>(read_ret)));
}

auto HttpConn::Read() -> bool {
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

// Write response to socket
auto HttpConn::Write() -> bool {
  if (write_idx_ == -1) {
    return false;
  }

  // Phase 1: send HTTP header from write_buf_
  while (write_buf_sent_ < write_idx_) {
    auto ret = send(sockfd_, write_buf_ + write_buf_sent_,
                    write_idx_ - write_buf_sent_, 0);
    if (ret == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        ModFd(sockfd_, NetEvent::WRITE_EVENT);
        return true;
      }
      return false;
    }
    if (ret == 0) {
      return false;
    }
    write_buf_sent_ += ret;
  }

  // Phase 2: send file body via sendfile
  if (file_fd_ != -1) {
    while (file_bytes_sent_ < file_size_) {
#if defined(__linux__)
      off_t offset = file_bytes_sent_;
      auto ret = sendfile(sockfd_, file_fd_, &offset,
                          static_cast<size_t>(file_size_ - file_bytes_sent_));
      auto sent = ret;
#elif defined(__APPLE__)
      off_t len = file_size_ - file_bytes_sent_;
      auto ret =
          sendfile(file_fd_, sockfd_, file_bytes_sent_, &len, nullptr, 0);
      auto sent = len;
#endif
      if (ret == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
#if defined(__APPLE__)
          file_bytes_sent_ += sent;
#endif
          ModFd(sockfd_, NetEvent::WRITE_EVENT);
          return true;
        }
        close(file_fd_);
        file_fd_ = -1;
        return false;
      }
      file_bytes_sent_ += sent;
    }
    close(file_fd_);
    file_fd_ = -1;
  }

  if (!linger_) {
    return false;
  }

  Init();
  ModFd(sockfd_, NetEvent::READ_EVENT);
  return true;
}

// Handle the HTTP connection
auto HttpConn::ProcessRead() -> HTTP_CODE {
  line_status_ = ParseLine();
  HTTP_CODE ret = NO_REQUEST;
  char* text = nullptr;
  // Main state machine loop
  while (line_status_ == LINE_OK) {
    text = read_buf_ + start_line_;
    start_line_ = checked_idx_;
    switch (check_state_) {
      case CHECK_STATE_REQUESTLINE: {
        ret = ParseRequest(text);
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        }
        break;
      }

      case CHECK_STATE_HEADER: {
        ret = ParseHeader(text);
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
        ret = ParseContent();
        /* TODO */
        break;
      }

      default: {
        return INTERNAL_ERROR;
      }
    }

    line_status_ = ParseLine();
  }
  return NO_REQUEST;
}

// Process the write operation
auto HttpConn::ProcessWrite(HTTP_CODE ret) -> bool {
  switch (ret) {
    case INTERNAL_ERROR:
      return WriteInternalError();
    case BAD_REQUEST:
      return WriteBadRequest();
    case FORBIDDEN_REQUEST:
      return WriteForbiddenRequest();
    case NO_RESOURCE:
      return WriteNoResource();
    case GET_REQUEST:
      return WriteGetRequest();
    default:
      return false;
  }
}

auto HttpConn::WriteInternalError() -> bool {
  std::string body;
  auto path = (resource_dir() / "html" / "500.html").string();
  if (!load_body(path.c_str(), &body)) {
    return WriteServerError();
  }
  if (!AddResponse(std::format(kHeader500, body.size()))) {
    return false;
  }
  return AddResponse(body);
}

auto HttpConn::WriteBadRequest() -> bool {
  std::string body;
  auto path = (resource_dir() / "html" / "400.html").string();
  if (!load_body(path.c_str(), &body)) {
    return WriteServerError();
  }
  if (!AddResponse(std::format(kHeader400, body.size()))) {
    return false;
  }
  return AddResponse(body);
}

auto HttpConn::WriteForbiddenRequest() -> bool {
  std::string body;
  auto path = (resource_dir() / "html" / "403.html").string();
  if (!load_body(path.c_str(), &body)) {
    return WriteServerError();
  }
  if (!AddResponse(std::format(kHeader403, body.size()))) {
    return false;
  }
  return AddResponse(body);
}

auto HttpConn::WriteNoResource() -> bool {
  std::string body;
  auto path = (resource_dir() / "html" / "404.html").string();
  if (!load_body(path.c_str(), &body)) {
    return WriteServerError();
  }
  if (!AddResponse(std::format(kHeader404, body.size()))) {
    return false;
  }
  return AddResponse(body);
}

auto HttpConn::WriteServerError() -> bool {
  linger_ = false;
  return AddResponse(kHeader500Empty);
}

auto HttpConn::WriteGetRequest() -> bool {
  const auto& cfg = GlobalConfig::Instance().Get();

  // Default response without server dir specified
  if (server_working_dir_.empty()) {
    std::string body;
    if (cfg.custom_response_text.has_value()) {
      body += cfg.custom_response_text.value();
      body += '\n';
    }

    if (body.empty()) {
      auto path = (resource_dir() / "html" / "200.html").string();
      if (!load_body(path.c_str(), &body)) {
        return WriteServerError();
      }
    } else {
      body = std::format(kHtmlWrapFmt, body);
    }

    if (!AddResponse(std::format(kHeader200, body.size(),
                                 (linger_ ? "keep-alive" : "close")))) {
      return false;
    }
    return AddResponse(body);
  }

  // Default request with server dir specified
  if (url_ == "/") {
    std::string dir_listing;
    try {
      for (const auto& entry :
           std::filesystem::directory_iterator(server_working_dir_)) {
        if (entry.is_regular_file()) {
          auto name = entry.path().filename().string();
          dir_listing += std::format(kFileLinkFmt, name, name);
        }
      }
    } catch (const std::filesystem::filesystem_error& e) {
      dir_listing += std::format(kDirErrorFmt, e.what());
    }

    std::string body;
    if (cfg.custom_response_text.has_value()) {
      body += cfg.custom_response_text.value();
      body += '\n';
    }
    body += std::format(kPreFmt, dir_listing);
    body = std::format(kHtmlWrapFmt, body);

    if (!AddResponse(std::format(kHeader200, body.size(),
                                 (linger_ ? "keep-alive" : "close")))) {
      return false;
    }
    return AddResponse(body);
  }

  // Request for file, allow single-level plain file only
  auto requested_path =
      (server_working_dir_ / url_.substr(1)).lexically_normal();

  // Guard against escape
  auto [mismatch_start, _] =
      std::mismatch(server_working_dir_.begin(), server_working_dir_.end(),
                    requested_path.begin());
  if (mismatch_start != server_working_dir_.end()) {
    return WriteForbiddenRequest();
  }

  // Check single-level
  auto relative =
      std::filesystem::relative(requested_path, server_working_dir_);
  if (relative.empty() || relative.has_parent_path()) {
    return WriteForbiddenRequest();
  }

  auto file_status = std::filesystem::symlink_status(requested_path);
  if (!std::filesystem::exists(file_status)) {
    return WriteNoResource();
  }
  if (std::filesystem::is_directory(file_status) ||
      std::filesystem::is_symlink(file_status)) {
    return WriteForbiddenRequest();
  }

  file_fd_ = open(requested_path.c_str(), O_RDONLY);
  if (file_fd_ == -1) {
    return WriteServerError();
  }
  file_size_ = std::filesystem::file_size(requested_path);
  LOG_INFO(std::format("Serving file: {} ({} bytes)", requested_path.string(),
                       file_size_));
  if (!AddResponse(std::format(kHeader200File, file_size_,
                               (linger_ ? "keep-alive" : "close")))) {
    close(file_fd_);
    file_fd_ = -1;
    return false;
  }
  return true;
}

// Parse a line and determine its status (Search \r\n)
auto HttpConn::ParseLine() -> LINE_STATUS {
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

auto HttpConn::ParseRequest(char* text) -> HTTP_CODE {
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

auto HttpConn::ParseHeader(char* text) -> HTTP_CODE {
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
auto HttpConn::ParseContent() -> HTTP_CODE {
  // For GET requests there is typically no message body.
  // Accept this as a complete request.
  return GET_REQUEST;
}

// Add response data to the write buffer
auto HttpConn::AddResponse(std::string_view text) -> bool {
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
auto HttpConn::SetNonblocking(int interest_fd) -> int {
  int old_option = fcntl(interest_fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(interest_fd, F_SETFL, new_option);
  return old_option;
}

#if defined(__linux__)
void HttpConn::ModFd(int interest_fd, NetEvent ev) {
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
  epoll_ctl(mux_fd_, EPOLL_CTL_MOD, interest_fd, &event);
}
#elif defined(__APPLE__)
void HttpConn::ModFd(int interest_fd, NetEvent ev) {
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
  kevent(mux_fd_, &event, 1, nullptr, 0, nullptr);
}
#endif

}  // namespace my_web_server