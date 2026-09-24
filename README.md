# Toy gRPC Messenger

Just a simple, educational messenger project built with C++

It demonstrates basic gRPC streaming, thread synchronization, and HTTP handling

Nothing too fancy

## How it works

- **Server:**
  - A central gRPC server
  - It receives messages and broadcasts them to all connected clients using server-side streams

- **Client:**
  - Acts as a bridge
  - It provides a simple HTTP REST interface for end-users (`/sendMessage`, `/getAndFlushMessages`) and communicates with the central server via gRPC under the hood

## Dependencies

- C++20 compatible compiler
- CMake (>= 3.15)
- gRPC & Protobuf
- [nlohmann/json](https://github.com/nlohmann/json)
- [cpp-httplib](https://github.com/yhirose/cpp-httplib)

## Build

Standard CMake build process:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
```
```sh
cmake --build build --config Release --parallel
```
