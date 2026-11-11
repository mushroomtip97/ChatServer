# Chat Server

A WebSocket-based chat server written in C++17 and Qt Core.

The server supports user accounts, room messaging, private messages, presence,
persistence, TLS, rate limiting, and a health/metrics endpoint. It is designed
to run headless as a console application.

## Features

- WebSocket server (TLS or plain)
- User registration and authentication
- Argon2id password hashing with automatic legacy-hash upgrade
- Public chat rooms and private messages
- Presence notifications
- SQLite persistence
- Rate limiting per IP and per account
- Health and metrics HTTP endpoint
- File logging
- Graceful shutdown on Windows and Linux

## Requirements

- Qt 6.5 or newer with `Core`, `Network`, `WebSockets`, `Sql`, `Concurrent`, `Test`
- CMake 3.19 or newer
- A C++17 compiler
- libsodium

On Windows, point CMake at libsodium:

```cmake
-DSODIUM_ROOT=C:/libsodium/libsodium-win64
```

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Or use the provided presets:

```bash
cmake --preset release
cmake --build --preset release
```

## Configuration

The server reads:

```text
config/server.conf
```

An example configuration is provided as:

```text
config/server.conf.example
```

Important TLS settings:

```ini
[tls]
enabled=true
cert_path=certs/server.crt
key_path=certs/server.key
```

The private key is intentionally not committed. Generate a local development
certificate before running with TLS enabled, or just disable TLS in the config.

## Running

The build step copies `config/` and `certs/` next to the executable.

```bash
./chat-server
```

The server listens on the configured WebSocket port and, when enabled, the
health/metrics HTTP port.

## Tests

C++ unit tests:

```bash
ctest --test-dir build --output-on-failure
```

Python robustness tests are available in `tests/` and are run by the GitHub
Actions workflow.

## Security notes

- Passwords are hashed with Argon2id.
- Plaintext password buffers are wiped as soon as libsodium no longer needs them.
- Password verification burns the same Argon2 cost for valid, empty, malformed,
  and legacy hashes.
- Legacy SHA-256 hashes are upgraded automatically after a successful login.
- Do not commit `certs/server.key`.
- Set `seed_demo_user=false` outside local development.

## License

MIT. See `LICENSE`.
