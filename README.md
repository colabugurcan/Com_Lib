# communication_lib

Modern C++17 communication library designed to be embedded as a Git submodule. The project targets Linux (Ubuntu 16.04/18.04/20.04) and provides a consistent abstraction layer for Ethernet (UDP/TCP), serial, and PCAN-Basic CAN transports.

## Highlights

- Optional internal receive threads with configurable polling intervals

## Getting Started

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

### Header-only usage

```cmake
add_subdirectory(communication_lib)
target_link_libraries(my_target PRIVATE comm)
```

### Static library (optional)

```bash
cmake -DCOMM_HEADER_ONLY=OFF ..
```

## Project Layout

```
communication_lib/
├── include/comm/core        # Core interfaces, configs, error handling
├── include/comm/ethernet    # Ethernet transports (UDP/TCP available)
├── include/comm/serial      # Serial transport (baseline available)
├── include/comm/can         # PCAN-Basic transport (baseline available)
├── src/                     # Source files for static build
├── tests/                   # GoogleTest unit test suite
├── examples/                # Usage examples (in progress)
└── CMakeLists.txt           # Build configuration
```

## Development Roadmap

1. Core interfaces and shared utilities ✅
2. UDP unicast support ✅
3. Serial transport baseline ✅
4. PCAN-Basic CAN baseline ✅
5. Advanced features (auto-reconnect, filtering, health monitoring) ⏳

## Integration Tests

Hardware-backed tests are opt-in. Enable them with `-DCOMM_ENABLE_INTEGRATION_TESTS=ON` and export the required interface variables before running `ctest`.

- `COMM_PCAN_TEST_HANDLE` – Numerical PCAN handle (e.g. `0x51` for USB1) for loopback validation.

### PCAN-Basic dependency

When `COMM_ENABLE_PCAN=ON` (default), CMake looks for Peak-System's `libpcanbasic`. Provide both the headers (`PCANBasic.h`) and the library (`libpcanbasic.so`) via standard include/library paths. If the dependency is absent the CAN transport remains available but operates in a no-op mode, reporting unsupported operations at runtime.

## Contributing

Pull requests are welcome. Please add or update unit tests when introducing new features.
