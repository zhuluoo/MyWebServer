#include <iostream>
#include "server/web_server.hpp"

auto main(int argc, char *argv[]) -> int {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " ip_address port_number\n";
    return 1;
  }
  int port = std::stoi(argv[2]);
  if (port <= 1024 || port >= 65536) {
    std::cerr << "Port number must be between 1025 and 65535\n";
    return 1;
  }

  WebServer server(argv[1], port);
  server.run();
  return 0;
}