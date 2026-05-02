# Build
- Require cmake 3.23 at least

Check cmake version
```bash
cmake --version
```

Build
```bash
cmake -B build
cmake --build build
```

# Run
- If you want to run on local machine, use 127.0.0.1 as IP
- On LAN, check your IP by command ip (Linux) or ifconfig (Mac)
```bash
./build/src/server.o IP PORT
```