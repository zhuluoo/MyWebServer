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

// File overview: HTTP response header templates and format strings.

#pragma once

#include <string_view>

namespace my_web_server {

inline constexpr std::string_view kHeader500Empty =
    "HTTP/1.1 500 Internal Server Error\r\n"
    "Content-Length: 0\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n";
inline constexpr std::string_view kHeader500 =
    "HTTP/1.1 500 Internal Server Error\r\n"
    "Content-Length: {}\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n";
inline constexpr std::string_view kHeader400 =
    "HTTP/1.1 400 Bad Request\r\n"
    "Content-Length: {}\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n";
inline constexpr std::string_view kHeader403 =
    "HTTP/1.1 403 Forbidden\r\n"
    "Content-Length: {}\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n";
inline constexpr std::string_view kHeader404 =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: {}\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n";
inline constexpr std::string_view kHeader200 =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: {}\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: {}\r\n"
    "\r\n";
inline constexpr std::string_view kHeader200File =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: {}\r\n"
    "Content-Type: application/octet-stream\r\n"
    "Connection: {}\r\n"
    "\r\n";
inline constexpr std::string_view kHtmlWrapFmt =
    "<html><body>\n{}</body></html>\n";
inline constexpr std::string_view kPreFmt = "<pre>\n{}</pre>\n";
inline constexpr std::string_view kDirErrorFmt =
    "[Failed to read directory: {}]\n";
inline constexpr std::string_view kFileLinkFmt = "<a href=\"/{}\">{}</a>\n";

}  // namespace my_web_server
