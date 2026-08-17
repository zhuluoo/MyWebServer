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

// File overview: Implements Base64 encoding and decoding utilities.

#include "utils/base64.hpp"

namespace my_web_server {

namespace {

constexpr char kBase64Map[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

auto CharBase64ToInt(char ch) -> int {
  if ('A' <= ch && ch <= 'Z') {
    return ch - 'A';
  }

  if ('a' <= ch && ch <= 'z') {
    return ch - 'a' + 26;
  }

  if ('0' <= ch && ch <= '9') {
    return ch - '0' + 52;
  }

  if ('+' == ch) {
    return 62;
  }

  if ('/' == ch) {
    return 63;
  }

  return -1;
}

}  // namespace

auto Utf8ToBase64(std::string_view str_utf8) -> std::string {
  if (str_utf8.empty()) {
    return {};
  }

  std::string str_base64{};
  auto binary_process = [&str_base64](std::string_view sv) -> void {
    int idx1 = static_cast<unsigned char>(sv[0]) >> 2;
    str_base64.push_back(kBase64Map[idx1]);

    if (1 >= sv.size()) {
      int idx2 = (static_cast<unsigned char>(sv[0]) & 0b00000011) << 4;
      str_base64.push_back(kBase64Map[idx2]);
      str_base64.push_back('=');
      str_base64.push_back('=');
      return;
    }

    int idx2 = ((static_cast<unsigned char>(sv[0]) & 0b00000011) << 4) +
               (static_cast<unsigned char>(sv[1]) >> 4);
    str_base64.push_back(kBase64Map[idx2]);

    if (2 >= sv.size()) {
      int idx3 = (static_cast<unsigned char>(sv[1]) & 0b00001111) << 2;
      str_base64.push_back(kBase64Map[idx3]);
      str_base64.push_back('=');
      return;
    }

    int idx3 = ((static_cast<unsigned char>(sv[1]) & 0b00001111) << 2) +
               (static_cast<unsigned char>(sv[2]) >> 6);
    str_base64.push_back(kBase64Map[idx3]);
    int idx4 = static_cast<unsigned char>(sv[2]) & 0b00111111;
    str_base64.push_back(kBase64Map[idx4]);
  };

  for (size_t i = 0; i < str_utf8.size(); i += 3) {
    binary_process(str_utf8.substr(i, 3));
  }
  return str_base64;
}

auto Base64ToUtf8(std::string_view str_base64) -> std::string {
  if (str_base64.empty() || str_base64.size() % 4 != 0) {
    return {};
  }

  std::string str_utf8{};
  auto binary_process = [&str_utf8](std::string_view sv) -> void {
    int idx_base64[4];
    for (std::size_t i = 0; i < 4; ++i) {
      idx_base64[i] = CharBase64ToInt(sv[i]);
    }

    int idx1 = (idx_base64[0] << 2) + ((idx_base64[1] & 0b00110000) >> 4);
    str_utf8.push_back(static_cast<char>(idx1));
    if ('=' == sv[2]) {
      return;
    }

    int idx2 = ((idx_base64[1] & 0b00001111) << 4) + (idx_base64[2] >> 2);
    str_utf8.push_back(static_cast<char>(idx2));
    if ('=' == sv[3]) {
      return;
    }

    int idx3 = ((idx_base64[2] & 0b00000011) << 6) + idx_base64[3];
    str_utf8.push_back(static_cast<char>(idx3));
  };

  for (std::size_t i = 0; i < str_base64.size(); i += 4) {
    binary_process(str_base64.substr(i, 4));
  }

  return str_utf8;
}

}  // namespace my_web_server
