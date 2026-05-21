# Build

Require cmake 3.23 at least.

Check cmake version:
```bash
cmake --version
```

Build:
```bash
cmake -B build
cmake --build build
```

# Run

```bash
./build/src/server.o [OPTIONS]
```

| Option | Description |
|--------|-------------|
| `--ip IP` | Listening address (default: 127.0.0.1) |
| `--port N` | Listening port, 1025–65535 (default: 8001) |
| `--text "..."` | Custom 200 response body text |
| `--dir PATH` | List files under given directory in 200 response |

## Examples

Default (show welcome page):
```bash
./build/src/server.o
```

Custom text response:
```bash
./build/src/server.o --text "Hello, world!"
```

Directory listing:
```bash
./build/src/server.o --dir ~/Desktop/test
```

Combined:
```bash
./build/src/server.o --ip 0.0.0.0 --port 9090 --text "Files:" --dir ./mydir
```