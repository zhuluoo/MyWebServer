#!/usr/bin/env bash
set -euo pipefail

echo "Start format process..."

find_clang_format() {
    # Check system PATH
    local cf_path
    cf_path=$(command -v clang-format 2>/dev/null || true)

    # Check Homebrew LLVM path
    if [[ -z "$cf_path" && -x "/opt/homebrew/opt/llvm/bin/clang-format" ]]; then
        cf_path="/opt/homebrew/opt/llvm/bin/clang-format"
    fi

    echo "$cf_path"
}

# Identify the binary
CLANG_FORMAT=$(find_clang_format)

if [[ -z "$CLANG_FORMAT" ]]; then
    echo "Error: clang-format not found in PATH or Homebrew LLVM directory." >&2
    echo "Please install it: brew install clang-format" >&2
    exit 1
fi

# Set the target directory (defaults to current directory if no argument is provided)
TARGET_DIR="${1:-.}"

# Ensure the target directory exists
if [[ ! -d "$TARGET_DIR" ]]; then
    echo "Error: Directory '$TARGET_DIR' does not exist." >&2
    exit 1
fi

echo "Using clang-format at: $CLANG_FORMAT"
echo "Formatting files in: $(cd "$TARGET_DIR" && pwd)"

##
# Find and format files
# -path "*/.*" -prune: Ignore hidden directories (like .git)
# -o: Logical OR
# -print0 | xargs -0: Handles filenames with spaces or special characters safely
##
find "$TARGET_DIR" \
    -path "*/.*" -prune -o \
    -path "*/build/*" -prune -o \
    \( \
        -name "*.cpp" -o \
        -name "*.hpp" -o \
        -name "*.h"   -o \
        -name "*.cc"  -o \
        -name "*.cxx" \
    \) -print0 | xargs -0 "$CLANG_FORMAT" -i -style=google

echo "Formatting complete!"
