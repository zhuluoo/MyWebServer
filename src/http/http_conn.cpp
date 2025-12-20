#include "http/http_conn.hpp"
// filename: http_conn.cpp

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

// Constructor
HttpConn::HttpConn() {
  memset(read_buf_, '\0', sizeof(read_buf_));
  memset(write_buf_, '\0', sizeof(write_buf_));
}

// Destructor
HttpConn::~HttpConn() = default;

// Initialize the HTTP connection
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

void HttpConn::init(int sockfd, const sockaddr_in &addr, int epollfd) {
  sockfd_ = sockfd;
  address_ = addr;
  epollfd_ = epollfd;
  init();
}

void HttpConn::process() {
  HTTP_CODE read_ret = process_read();
  if (read_ret == NO_REQUEST) {
    // Need to read more data; re-arm EPOLLIN for this socket (one-shot)
    mod_fd(sockfd_, EPOLLIN);
    return;
  }
  bool write_ret = process_write(read_ret);
  if (!write_ret) {
    // Failed to process write, set write_idx_ to -1 to indicate no data to send
    write_idx_ = -1;
  }
  // Ready to send response in write_buf_, switch to EPOLLOUT for sending
  mod_fd(sockfd_, EPOLLOUT);
}

// Read available data from the socket (non-blocking ET loop)
auto HttpConn::read() -> bool {
  // Protect against overflowing the read buffer
  if (read_idx_ >= static_cast<int>(sizeof(read_buf_) - 1)) {
    return false; // buffer full -> treat as error
  }

  ssize_t bytes_read = 0;
  // Non-blocking read loop
  while (true) {
    bytes_read = recv(sockfd_, read_buf_ + read_idx_,
                      static_cast<int>(sizeof(read_buf_) - read_idx_ - 1), 0);
    if (bytes_read == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // No more data for now
        break;
      }
      // Other socket error
      return false;
    } else if (bytes_read == 0) {
      // Client closed connection
      return false;
    }

    read_idx_ += static_cast<int>(bytes_read);
    // Ensure we don't overflow the buffer
    if (read_idx_ >= static_cast<int>(sizeof(read_buf_) - 1)) {
      return false;
    }
  }

  return true;
}

// Send the prepared response to the client (non-blocking ET loop)
auto HttpConn::write() -> bool {
  if (write_idx_ == -1) {
    // Write process failed, close connection
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
        mod_fd(sockfd_, EPOLLOUT);
        return true; // Keep connection alive for retry
      }
      // Other socket error
      return false;
    }
    if (bytes_written == 0) {
      // Connection closed
      return false;
    }
    // Update total bytes written
    total_bytes_written += bytes_written;
  }

  // All data has been sent successfully
  // If connection is not persistent, close it
  if (!linger_) {
    return false; // Indicate to close connection
  }
  // Keep-alive: reset state for next request and re-arm for reading
  init();
  mod_fd(sockfd_, EPOLLIN);
  return true;
}

// Handle the HTTP connection
auto HttpConn::process_read() -> HTTP_CODE {
  line_status_ = LINE_OK;
  HTTP_CODE ret = NO_REQUEST;
  char *text = nullptr;
  // Main state machine loop
  while ((check_state_ == CHECK_STATE_CONTENT && line_status_ == LINE_OK) ||
         ((line_status_ = parse_line()) == LINE_OK)) {
    // Get the start line
    text = read_buf_ + start_line_;
    // Update start_line_ to the next line
    start_line_ = checked_idx_;
    switch (check_state_) {
      // Parse request line
    case CHECK_STATE_REQUESTLINE: {
      ret = parse_request(text);
      if (ret == BAD_REQUEST) {
        return BAD_REQUEST;
      }
      break;
    }
    // Parse headers
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
    // Parse message body
    case CHECK_STATE_CONTENT: {
      ret = parse_content();
      /* TODO */
      break;
    }
    default: {
      return INTERNAL_ERROR;
    }
    }
  }
  return NO_REQUEST;
}

// Process the write operation
auto HttpConn::process_write(HTTP_CODE ret) -> bool {
  switch (ret) {
  case INTERNAL_ERROR: {
    const char *body =
        "<html><head><title>500 Internal Server Error</title></head>"
        "<body><h1>Internal Server Error</h1></body></html>";
    if (!add_response("HTTP/1.1 500 Internal Server Error\r\n"
                      "Content-Length: %d\r\n"
                      "Content-Type: text/html\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      static_cast<int>(strlen(body)))) {
      return false;
    }
    return add_response("%s", body);
  }
  case BAD_REQUEST: {
    const char *body = "<html><head><title>400 Bad Request</title></head>"
                       "<body><h1>Bad Request</h1></body></html>";
    if (!add_response("HTTP/1.1 400 Bad Request\r\n"
                      "Content-Length: %d\r\n"
                      "Content-Type: text/html\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      static_cast<int>(strlen(body)))) {
      return false;
    }
    return add_response("%s", body);
  }
  case FORBIDDEN_REQUEST: {
    const char *body = "<html><head><title>403 Forbidden</title></head>"
                       "<body><h1>Forbidden</h1></body></html>";
    if (!add_response("HTTP/1.1 403 Forbidden\r\n"
                      "Content-Length: %d\r\n"
                      "Content-Type: text/html\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      static_cast<int>(strlen(body)))) {
      return false;
    }
    return add_response("%s", body);
  }
  case NO_RESOURCE: {
    const char *body = "<html><head><title>404 Not Found</title></head>"
                       "<body><h1>Not Found</h1></body></html>";
    if (!add_response("HTTP/1.1 404 Not Found\r\n"
                      "Content-Length: %d\r\n"
                      "Content-Type: text/html\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      static_cast<int>(strlen(body)))) {
      return false;
    }
    return add_response("%s", body);
  }
  case GET_REQUEST: {
    // Simple successful response for GET (small test response)
    const char *body = "<html><body><h1>It works!</h1></body></html>";
    if (!add_response("HTTP/1.1 200 OK\r\n"
                      "Content-Length: %d\r\n"
                      "Content-Type: text/html\r\n"
                      "Connection: %s\r\n"
                      "\r\n",
                      static_cast<int>(strlen(body)),
                      (linger_ ? "keep-alive" : "close"))) {
      return false;
    }
    return add_response("%s", body);
  }
  case FILE_REQUEST: {
    // Headers only; file body will be sent separately (e.g. via mmap/sendfile)
    return add_response("HTTP/1.1 200 OK\r\n"
                        "Content-Length: %d\r\n"
                        "Content-Type: application/octet-stream\r\n"
                        "Connection: %s\r\n"
                        "\r\n",
                        file_stat_, (linger_ ? "keep-alive" : "close"));
  }
  default: {
    return false;
  }
  }
}

// Parse a line and determine its status
auto HttpConn::parse_line() -> LINE_STATUS {
  char tmp;
  for (; checked_idx_ < read_idx_; ++checked_idx_) {

    tmp = read_buf_[checked_idx_];
    // Find \r, check whether read a complete line
    if (tmp == '\r') {
      // Reached the end of the buffer without \n
      if ((checked_idx_ + 1) == read_idx_) {
        return LINE_OPEN; // Need to read more
      }
      // Next character is \n, line is complete
      if (read_buf_[checked_idx_ + 1] == '\n') {
        read_buf_[checked_idx_++] = '\0'; // For convenience
        read_buf_[checked_idx_++] = '\0';
        return LINE_OK;
      }
      // Isolated \r
      return LINE_BAD;
    }
    // Found \n, check if preceded by \r
    if (tmp == '\n') {
      if (checked_idx_ > 0 && read_buf_[checked_idx_ - 1] == '\r') {
        read_buf_[checked_idx_ - 1] = '\0';
        read_buf_[checked_idx_++] = '\0';
        return LINE_OK;
      }
      // Isolated \n
      return LINE_BAD;
    }
  }

  // No complete line found yet
  return LINE_OPEN;
}

// Parse the HTTP request line
auto HttpConn::parse_request(char *text) -> HTTP_CODE {
  int start = 0;
  int end = 0;
  // Parse method
  while (text[end] != '\0') {
    if (text[end] == ' ') {
      std::string method(text + start, end - start);
      if (method == "GET") {
        method_ = GET;
      } else {
        return BAD_REQUEST; // We only support GET for now
      }
      break;
    }
    ++end;
  }
  // Not enough information
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
  // Not enough information
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
        return BAD_REQUEST; // We only support HTTP/1.1 for now
      }
      break;
    }
    ++end;
  }
  // Not enough information
  if (text[end] == '\0' && text[end - 1] == ' ') {
    return BAD_REQUEST;
  }

  check_state_ = CHECK_STATE_HEADER; // Move to header parsing state
  return NO_REQUEST;                 // Successfully parsed request line
}

// Parse HTTP headers
auto HttpConn::parse_header(char *text) -> HTTP_CODE {
  // An empty line indicates the end of headers
  if (text[0] == '\0') {
    // If there's no content, we have a complete request
    return GET_REQUEST;
  }
  // Parse individual headers
  if (strncasecmp(text, "Host:", 5) == 0) {
    text += 5;
    while (*text == ' ') {
      ++text;
    }
    host_ = std::string(text);
  } else if (strncasecmp(text, "Connection:", 11) == 0) {
    text += 11;
    while (*text == ' ') {
      ++text;
    }
    if (strcasecmp(text, "keep-alive") == 0) {
      linger_ = true;
    }
  } else {
    // Other headers are ignored for now
  }
  return NO_REQUEST; // Need to read more
}

// Parse the HTTP message body
auto HttpConn::parse_content() -> HTTP_CODE {
  // For GET requests there is typically no message body.
  // Accept this as a complete request.
  return GET_REQUEST;
}

// Add response data to the write buffer
auto HttpConn::add_response(const char *format, ...) -> bool {
  if (static_cast<size_t>(write_idx_) >= sizeof(write_buf_)) {
    return false;
  }
  va_list arg_list;
  va_start(arg_list, format);
  // Format the response and add it to the write buffer
  int len = vsnprintf(write_buf_ + write_idx_,
                      sizeof(write_buf_) - write_idx_ - 1, format, arg_list);
  // Check for buffer overflow
  if (static_cast<size_t>(len) >= (sizeof(write_buf_) - write_idx_ - 1)) {
    va_end(arg_list);
    return false;
  }
  write_idx_ += len;
  va_end(arg_list);
  return true;
}

// Set a file descriptor to non-blocking mode
auto HttpConn::set_nonblocking(int interest_fd) -> int {
  int old_option = fcntl(interest_fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(interest_fd, F_SETFL, new_option);
  return old_option;
}

// Modify the events associated with a file descriptor in the epoll instance
auto HttpConn::mod_fd(int interest_fd, int ev) -> void {
  epoll_event event;
  event.data.fd = interest_fd;
  // Set new events while keeping ET, RDHUP, and ONESHOT flags
  event.events = ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
  epoll_ctl(epollfd_, EPOLL_CTL_MOD, interest_fd, &event);
}
