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

// File overview: Implements resource directory resolution utilities.

#include "utils/resource_utils.hpp"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace my_web_server {
namespace {

auto get_executable_dir() -> std::filesystem::path {
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
    return std::filesystem::path(buffer).parent_path();
  }
  return {};
#elif defined(__linux__)
  std::string buffer(4096, '\0');
  ssize_t count = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (count > 0) {
    buffer.resize(static_cast<size_t>(count));
    return std::filesystem::path(buffer).parent_path();
  }
  return {};
#else
  return {};
#endif
}

auto resolve_resource_dir() -> std::filesystem::path {
  auto exe_dir = get_executable_dir();
  if (!exe_dir.empty()) {
    return exe_dir / "resources";
  }
  return std::filesystem::path("resources");
}

}  // namespace

auto resource_dir() -> const std::filesystem::path& {
  static const std::filesystem::path cached = resolve_resource_dir();
  return cached;
}

}  // namespace my_web_server
