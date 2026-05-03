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

#include <string>

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
  // HTTP response codes
  enum HTTP_CODE {
    NO_REQUEST = 0,     // request not complete yet, continue reading
    GET_REQUEST,        // got a complete request (200)
    BAD_REQUEST,        // malformed request (400)
    NO_RESOURCE,        // resource not found (404)
    FORBIDDEN_REQUEST,  // access forbidden (403)
    FILE_REQUEST,       // file request successful (200)
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
  auto add_response(const char* format, ...)
      -> bool;  // Add response to write buffer

  // Utility functions for epoll
  auto set_nonblocking(int interest_fd) -> int;
  void mod_fd(int interest_fd, NetEvent ev);
  // auto add_fd(int interest_fd, bool one_shot) -> void;
  // auto remove_fd(int interest_fd) -> void;

  int sockfd_ = -1;  // socket file descriptor
#if defined(__linux__)
  int epollfd_ = -1;  // epoll file descriptor
#elif defined(__APPLE__)
  int kq_;
#endif
  sockaddr_in address_;  // client address

  char read_buf_[2048];  // read buffer
  int read_idx_ = 0;     // index of the next byte to read
  int checked_idx_ = 0;  // index of the byte being analyzed
  int start_line_ = 0;   // start index of the current line being parsed

  char write_buf_[1024];  // write buffer
  int write_idx_ = 0;     // index of the next byte to write

  int version_ = 0;      // HTTP version
  std::string url_{};    // request URL
  std::string host_{};   // Host header value
  bool linger_ = false;  // whether to keep the connection alive

  METHOD method_;            // request method
  CHECK_STATE check_state_;  // main state machine current state
  LINE_STATUS line_status_;  // line parsing status

  char* file_address_ = nullptr;  // requested file address
  int file_stat_ = 0;             // file status
};

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
