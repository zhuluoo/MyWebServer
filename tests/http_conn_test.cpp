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

// File overview: End-to-end HttpConn test using loopback socket pair.

#define WIN32_LEAN_AND_MEAN
#define FD_SETSIZE 1024
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "http/http_conn.hpp"
#include "server/windows_poller.hpp"

auto main() -> int {
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed\n";
    return 1;
  }

  std::cout << "Running HTTP Server Tests...\n";

  SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(server_fd != INVALID_SOCKET);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  addr.sin_port = 0;
  assert(bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) !=
         SOCKET_ERROR);

  socklen_t addr_len = sizeof(addr);
  getsockname(server_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len);
  assert(listen(server_fd, 1) != SOCKET_ERROR);

  u_long mode = 1;
  ioctlsocket(server_fd, FIONBIO, &mode);

  SOCKET client_fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(client_fd != INVALID_SOCKET);
  assert(connect(client_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) !=
         SOCKET_ERROR);

  SOCKET conn_fd;
  for (;;) {
    conn_fd = accept(server_fd, nullptr, nullptr);
    if (conn_fd != INVALID_SOCKET) break;
    if (WSAGetLastError() != WSAEWOULDBLOCK) {
      assert(false);
    }
    Sleep(10);
  }
  closesocket(server_fd);

  ioctlsocket(conn_fd, FIONBIO, &mode);

  my_web_server::SelectPoller poller;
  poller.add(conn_fd, true);

  sockaddr_in dummy{};
  my_web_server::HttpConn conn;
  conn.init(conn_fd, dummy, &poller);
  std::cout << "HttpConn initialized\n";

  const char* req =
      "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  int sent = send(client_fd, req, static_cast<int>(strlen(req)), 0);
  std::cout << "Client sent: " << sent << " bytes\n";
  assert(sent == static_cast<int>(strlen(req)));

  bool read_ok = conn.read();
  std::cout << "conn.read() -> " << read_ok << "\n";
  assert(read_ok);

  conn.process();
  std::cout << "conn.process() called\n";

  bool write_ok = conn.write();
  std::cout << "conn.write() -> " << write_ok << "\n";

  char buf[4096] = {0};
  int r = recv(client_fd, buf, sizeof(buf) - 1, 0);
  std::cout << "Client recv: " << r << " bytes\n";
  assert(r > 0);
  std::string resp(buf, static_cast<size_t>(r));
  std::cout << "Response received:\n" << resp << "\n";
  std::cout << "HttpConn end-to-end test passed\n";

  closesocket(conn_fd);
  closesocket(client_fd);

  WSACleanup();

  std::cout << "All tests passed!\n";
  return 0;
}
