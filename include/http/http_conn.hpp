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

#include <netinet/in.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace my_web_server {

constexpr size_t kReadBufferSize = 2048;
constexpr size_t kWriteBufferSize = 1024;

// Class to handle HTTP connections
class HttpConn {
 public:
  // HTTP request methods
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
  // Main state machine states
  enum CHECK_STATE {
    CHECK_STATE_REQUESTLINE = 0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
  };
  // Sub state machine states
  enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

  enum HTTP_CODE {
    NO_REQUEST = 0,     // request not complete yet, continue reading
    GET_REQUEST,        // got a complete request (200)
    BAD_REQUEST,        // malformed request (400)
    NO_RESOURCE,        // resource not found (404)
    FORBIDDEN_REQUEST,  // access forbidden (403)
    INTERNAL_ERROR,     // internal server error (500)
    CLOSED_CONNECTION   // connection closed by client
  };

  enum class NetEvent { READ_EVENT, WRITE_EVENT };

  HttpConn();
  HttpConn(const HttpConn&) = delete;
  auto operator=(const HttpConn&) -> HttpConn& = delete;
  ~HttpConn();

  void init();
  void init(int sockfd, const sockaddr_in& addr, int fd);
  // Handle the HTTP connection
  void process();
  // Non-block read all available data from the socket(for ET mode)
  auto read() -> bool;
  // Non-block write all data to the socket(for ET mode)
  auto write() -> bool;

 private:
  // Process the read operation
  auto process_read() -> HTTP_CODE;
  auto parse_request(char*) -> HTTP_CODE;  // For request line
  auto parse_header(char*) -> HTTP_CODE;   // For headers
  auto parse_content() -> HTTP_CODE;       // For message body
  auto parse_line() -> LINE_STATUS;        // Find a complete line

  // Process the write operation
  auto process_write(HTTP_CODE ret) -> bool;
  auto write_internal_error() -> bool;
  auto write_bad_request() -> bool;
  auto write_forbidden_request() -> bool;
  auto write_no_resource() -> bool;
  auto write_get_request() -> bool;
  auto write_server_error() -> bool;
  auto add_response(std::string_view text)
      -> bool;  // Add response to write buffer

  // Utility functions for epoll
  auto set_nonblocking(int interest_fd) -> int;
  void mod_fd(int interest_fd, NetEvent ev);

  int sockfd_{-1};  // socket file descriptor
#if defined(__linux__)
  int epollfd_{-1};  // epoll file descriptor
#elif defined(__APPLE__)
  int kq_{-1};
#endif
  sockaddr_in address_;  // client address

  char read_buf_[kReadBufferSize];  // read buffer
  int read_idx_{0};                 // index of the next byte to read
  int checked_idx_{0};              // index of the byte being analyzed
  int start_line_{0};  // start index of the current line to be parsed

  char write_buf_[kWriteBufferSize];  // write buffer
  int write_idx_{0};                  // index of the next byte to write

  int version_{0};      // HTTP version
  std::string url_{};   // request URL
  std::string host_{};  // Host header value
  bool linger_{false};  // whether to keep the connection alive

  METHOD method_;  // request method
  CHECK_STATE check_state_{
      CHECK_STATE_REQUESTLINE};       // main state machine current state
  LINE_STATUS line_status_{LINE_OK};  // line parsing status

  off_t file_size_{0};        // size of file being served
  int file_fd_{-1};           // fd of file being sent via sendfile
  int write_buf_sent_{0};     // bytes sent from write_buf_
  off_t file_bytes_sent_{0};  // bytes sent from file via sendfile

  std::filesystem::path server_working_dir_{};  // cached working dir
};

}  // namespace my_web_server

/* HTTP Request message structure
Request Line:        METHOD URL VERSION\r\n
Request Headers:     <HEADER_FIELD_NAME>: <VALUE>\r\n
                     <HEADER_FIELD_NAME>: <VALUE>\r\n
                     ...
A Blank Line:        \r\n
Message Body:        ...
*/
/* HTTP Response message structure
    HTTP-VERSION SP STATUS-CODE SP REASON-PHRASE\r\n
    Headers...\r\n
    \r\n
    Body
*/
