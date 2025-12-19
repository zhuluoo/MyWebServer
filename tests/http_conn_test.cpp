#include <atomic>
#include <cassert>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <fcntl.h>

#include "http/http_conn.hpp"

int main() {
  std::cout << "Running HTTP Server Tests...\n";

  // End-to-end HttpConn read/process/write using socketpair
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
    perror("socketpair");
    return 1;
  }

  int ep = epoll_create1(0);
  if (ep == -1) {
    perror("epoll_create1");
    return 1;
  }

  HttpConn conn;
  sockaddr_in dummy{};

  // Add sv[0] to epoll
  epoll_event event;
  event.data.fd = sv[0];
  event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
  event.events |= EPOLLONESHOT;
  epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &event);
  // Set sv[0] non-blocking
  int old_option = fcntl(sv[0], F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(sv[0], F_SETFL, new_option);

  conn.init(sv[0], dummy, ep);
  std::cout << "HttpConn initialized\n";

  // Client side sends a request
  const char *req =
      "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  ssize_t sent = send(sv[1], req, strlen(req), 0);
  std::cout << "Client sent: " << sent << " bytes\n";
  assert(sent == static_cast<ssize_t>(strlen(req)));

  // Server side read into buffer
  bool read_ok = conn.read();
  std::cout << "conn.read() -> " << read_ok << "\n";
  assert(read_ok);

  // Process the request
  conn.process();
  std::cout << "conn.process() called\n";

  // Write the response out
  // process() should arm EPOLLOUT; call write() to actually send
  bool write_ok = conn.write();
  std::cout << "conn.write() -> " << write_ok << "\n";

  // Client receives the response
  char buf[4096] = {0};
  ssize_t r = recv(sv[1], buf, sizeof(buf) - 1, 0);
  std::cout << "Client recv: " << r << " bytes\n";
  assert(r > 0);
  std::string resp(buf, r);
  std::cout << "Response received:\n" << resp << "\n";
  std::cout << "HttpConn end-to-end test passed\n";

  // Close sockets and epoll
  close(sv[0]);
  close(sv[1]);
  close(ep);

  std::cout << "All tests passed!\n";
  return 0;
}
