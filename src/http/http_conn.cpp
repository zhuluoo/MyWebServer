#include "http/http_conn.hpp"
// filename: http_conn.cpp

#include <cstring>

// Constructor
HttpConn::HttpConn() {
  memset(read_buf_, '\0', sizeof(read_buf_));
  memset(write_buf_, '\0', sizeof(write_buf_));
}

// Initialize the HTTP connection
void HttpConn::init() {
  memset(read_buf_, '\0', sizeof(read_buf_));
  read_idx_ = 0;
  checked_idx_ = 0;
  start_line_ = 0;

  memset(write_buf_, '\0', sizeof(write_buf_));
  write_idx_ = 0;

  version_ = 0;
  url_ = nullptr;
  host_ = nullptr;
  linger_ = false;

  method_ = GET;
  check_state_ = CHECK_STATE_REQUESTLINE;
  line_status_ = LINE_OK;

  file_address_ = nullptr;
  file_stat_ = 0;
}

void HttpConn::init(int sockfd, const sockaddr_in &addr) {
  sockfd_ = sockfd;
  address_ = addr;
  init();
}

// Handle the HTTP connection
void HttpConn::process() {
  init();
  while (true) {
    int data_read =
        recv(sockfd_, read_buf_ + read_idx_, sizeof(read_buf_) - read_idx_, 0);
    if (data_read < 0) {
      // Error in reading
      break;
    }
    if (data_read == 0) {
      // Connection closed by client
      break;
    }

    read_idx_ += data_read;
    HTTP_CODE ret_code = parse_content();
    // Not a complete request yet, continue reading
    if (ret_code == NO_REQUEST) {
      continue;
    }
    // Get a complete request
    if (ret_code == GET_REQUEST) {
      /* TODO: should reponse something here*/
      break;
    }
    // Other errors
    /* TODO: should response something here*/
    break;
  }
}

// The entry of main state machine
auto HttpConn::parse_content() -> HTTP_CODE {
  LINE_STATUS line_status = LINE_OK;
  HTTP_CODE ret_code = NO_REQUEST;
  // Parse lines one by one
  while ((line_status = parse_line()) == LINE_OK) {
    char *text = read_buf_ + start_line_;
    start_line_ = checked_idx_; // Update start_line_ to the next line
    // Parse according to the current state
    switch (check_state_) {
    case CHECK_STATE_REQUESTLINE: {
      ret_code = parse_request(text);
      if (ret_code == BAD_REQUEST) {
        return BAD_REQUEST;
      }
      break;
    }
    case CHECK_STATE_HEADER: {
      ret_code = parse_header(text);
      if (ret_code == BAD_REQUEST) {
        return BAD_REQUEST;
      }
      if (ret_code == GET_REQUEST) {
        return GET_REQUEST;
      }
      break;
    }
    case CHECK_STATE_CONTENT: {
      // Currently not handling content parsing
      break;
    }
    default: {
      return INTERNAL_ERROR;
    }
    }
  }
  // If we reach here, we either need more data or encountered a bad line
  if (line_status == LINE_OPEN) {
    return NO_REQUEST; // Need to read more data
  }
  if (line_status == LINE_BAD) {
    return BAD_REQUEST; // Malformed line
  }
  return NO_REQUEST;
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
