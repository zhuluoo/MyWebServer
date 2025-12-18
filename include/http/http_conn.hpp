#pragma once
// filename: http_conn.hpp

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
    NO_REQUEST = 0,    // request not complete yet, continue reading
    GET_REQUEST,       // got a complete request (200)
    BAD_REQUEST,       // malformed request (400)
    NO_RESOURCE,       // resource not found (404)
    FORBIDDEN_REQUEST, // access forbidden (403)
    FILE_REQUEST,      // file request successful (200)
    INTERNAL_ERROR,    // internal server error (500)
    CLOSED_CONNECTION  // connection closed by client
  };

  explicit HttpConn(int sockfd = -1, const sockaddr_in &addr = {});
  HttpConn(const HttpConn &) = delete;
  auto operator=(const HttpConn &) -> HttpConn & = delete;
  ~HttpConn();

  void init();
  // Handle the HTTP connection
  void process();

private:
  // The entry of main state machine
  auto parse_content() -> HTTP_CODE;
  // Find out a complete line, sub state machine
  auto parse_line() -> LINE_STATUS;
  // Parse the HTTP request line
  auto parse_request(char *) -> HTTP_CODE;
  // Parse HTTP headers
  auto parse_header(char *) -> HTTP_CODE;

  int sockfd_ = -1;     // socket file descriptor
  sockaddr_in address_; // client address

  char read_buf_[2048]; // read buffer
  int read_idx_ = 0;    // index of the next byte to read
  int checked_idx_ = 0; // index of the byte being analyzed
  int start_line_ = 0;  // start index of the current line being parsed

  char write_buf_[1024]; // write buffer
  int write_idx_ = 0;    // index of the next byte to write

  int version_ = 0;     // HTTP version
  std::string url_{};   // request URL
  std::string host_{};  // Host header value
  bool linger_ = false; // whether to keep the connection alive

  METHOD method_;           // request method
  CHECK_STATE check_state_; // main state machine current state
  LINE_STATUS line_status_; // line parsing status

  char *file_address_ = nullptr; // requested file address
  int file_stat_ = 0;            // file status
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
