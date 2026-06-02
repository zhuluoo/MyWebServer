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

// File overview: Windows select()-based I/O multiplexer, epoll-like
// add/mod/del/wait API.

#pragma once

#define WIN32_LEAN_AND_MEAN
#define FD_SETSIZE 1024
#include <winsock2.h>
#include <ws2tcpip.h>

#include <unordered_map>
#include <vector>

namespace my_web_server {

class SelectPoller {
 public:
  enum class EventType { kRead, kWrite, kError };
  struct Event {
    SOCKET fd;
    EventType type;
  };

  void add(SOCKET fd, bool one_shot);
  void mod(SOCKET fd, bool want_read);
  void del(SOCKET fd);
  int wait(std::vector<Event>& events, int timeout_ms);
  bool empty() const;

 private:
  struct FdState {
    bool want_read = false;
    bool want_write = false;
    bool is_listen = false;
  };
  std::unordered_map<SOCKET, FdState> states_;
};

inline void SelectPoller::add(SOCKET fd, bool one_shot) {
  FdState st;
  st.want_read = true;
  st.is_listen = !one_shot;
  states_[fd] = st;
}

inline void SelectPoller::mod(SOCKET fd, bool want_read) {
  auto& st = states_[fd];
  st.want_read = want_read;
  st.want_write = !want_read;
}

inline void SelectPoller::del(SOCKET fd) { states_.erase(fd); }

inline bool SelectPoller::empty() const { return states_.empty(); }

inline int SelectPoller::wait(std::vector<Event>& events, int timeout_ms) {
  fd_set readfds;
  fd_set writefds;
  fd_set exceptfds;
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  FD_ZERO(&exceptfds);
  SOCKET maxfd = 0;

  for (const auto& [fd, st] : states_) {
    if (st.want_read) {
      FD_SET(fd, &readfds);
    }
    if (st.want_write) {
      FD_SET(fd, &writefds);
    }
    FD_SET(fd, &exceptfds);
    if (fd > maxfd) {
      maxfd = fd;
    }
  }

  timeval tv{};
  timeval* ptv = nullptr;
  if (timeout_ms >= 0) {
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ptv = &tv;
  }

  int ret =
      select(static_cast<int>(maxfd + 1), &readfds, &writefds, &exceptfds, ptv);
  if (ret <= 0) {
    return ret;
  }

  for (const auto& [fd, st] : states_) {
    if (FD_ISSET(fd, &exceptfds)) {
      events.push_back({fd, EventType::kError});
    }
    if (FD_ISSET(fd, &readfds)) {
      events.push_back({fd, EventType::kRead});
    }
    if (FD_ISSET(fd, &writefds)) {
      events.push_back({fd, EventType::kWrite});
    }
  }
  return static_cast<int>(events.size());
}

}  // namespace my_web_server
