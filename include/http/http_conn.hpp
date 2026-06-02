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

// File overview: Defines HttpConn, which handles a single HTTP connection.

#pragma once

#define WIN32_LEAN_AND_MEAN
#define FD_SETSIZE 1024
#include <winsock2.h>

#ifdef DELETE
#undef DELETE
#endif
#ifdef CONNECT
#undef CONNECT
#endif

#include <filesystem>
#include <string>
#include <string_view>

namespace my_web_server {

constexpr size_t kReadBufferSize = 2048;
constexpr size_t kWriteBufferSize = 1024;

class SelectPoller;

class HttpConn {
 public:
  enum METHOD {
    GET = 0,
    POST,
    HEAD,
    PUT,
    DELETE,
    TRACE,
    OPTIONS,
    CONNECT,
    PATCH
  };
  enum CHECK_STATE {
    CHECK_STATE_REQUESTLINE = 0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
  };
  enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

  enum HTTP_CODE {
    NO_REQUEST = 0,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION
  };

  enum class NetEvent { READ_EVENT, WRITE_EVENT };

  HttpConn();
  HttpConn(const HttpConn&) = delete;
  auto operator=(const HttpConn&) -> HttpConn& = delete;
  ~HttpConn();

  void init();
  void init(SOCKET sockfd, const sockaddr_in& addr, SelectPoller* poller);
  void process();
  auto read() -> bool;
  auto write() -> bool;

 private:
  auto process_read() -> HTTP_CODE;
  auto parse_request(char*) -> HTTP_CODE;
  auto parse_header(char*) -> HTTP_CODE;
  auto parse_content() -> HTTP_CODE;
  auto parse_line() -> LINE_STATUS;

  auto process_write(HTTP_CODE ret) -> bool;
  auto write_internal_error() -> bool;
  auto write_bad_request() -> bool;
  auto write_forbidden_request() -> bool;
  auto write_no_resource() -> bool;
  auto write_get_request() -> bool;
  auto write_server_error() -> bool;
  auto add_response(std::string_view text) -> bool;

  auto set_nonblocking(SOCKET fd) -> int;
  void mod_fd(SOCKET fd, NetEvent ev);

  SOCKET sockfd_{INVALID_SOCKET};
  SelectPoller* poller_{nullptr};
  sockaddr_in address_;

  char read_buf_[kReadBufferSize];
  int read_idx_{0};
  int checked_idx_{0};
  int start_line_{0};

  char write_buf_[kWriteBufferSize];
  int write_idx_{0};

  int version_{0};
  std::string url_{};
  std::string host_{};
  bool linger_{false};

  METHOD method_;
  CHECK_STATE check_state_{CHECK_STATE_REQUESTLINE};
  LINE_STATUS line_status_{LINE_OK};

  long long file_size_{0};
  int file_fd_{-1};
  int write_buf_sent_{0};
  long long file_bytes_sent_{0};

  std::filesystem::path server_working_dir_{};
};

}  // namespace my_web_server
