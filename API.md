# API Overview

## Core Namespace

All public types live under the `comm` namespace.

### Enumerations

- `Protocol` – Transport family (`Unknown`, `UDP`, `TCP`, `Serial`, `CAN`).
- `Mode` – Addressing mode for transports (`Unspecified`, `Unicast`, `Broadcast`, `Multicast`).
- `Direction` – Traffic direction (`Unspecified`, `SendOnly`, `ReceiveOnly`, `TwoWay`).
- `Role` – Active or passive endpoints (`Undefined`, `Client`, `Server`).
- `TransportReliability` – Expected delivery semantics (`Unspecified`, `Reliable`, `BestEffort`).
- `LogLevel` – Logging verbosity (`Trace`, `Debug`, `Info`, `Warn`, `Error`, `Critical`, `Off`).
- `HealthState` – Health monitoring state (`Unknown`, `Healthy`, `Degraded`, `Faulted`).

### Configuration Structures

- `RetryPolicy`
  - `enabled` – Enables retry behavior.
  - `maxAttempts` – Maximum number of retry attempts (0 = unlimited when enabled).
  - `baseDelay` – Initial backoff duration.
  - `maxDelay` – Maximum backoff duration.
  - `backoffMultiplier` – Growth factor for exponential backoff.

- `TimeoutSettings`
  - `openTimeout` – Timeout for opening the transport.
  - `closeTimeout` – Timeout for closing the transport.
  - `sendTimeout` – Timeout for send operations.
  - `receiveTimeout` – Timeout for receive operations.
  - `reconnectInterval` – Interval before attempting reconnection.

- `Statistics`
  - `bytesSent`, `bytesReceived` – Byte counters.
  - `messagesSent`, `messagesReceived` – Message counters.
  - `errorCount` – Errors encountered.
  - `startTime` – Start time of the statistics window.

- `HealthStatus`
  - `state` – Current health state.
  - `message` – Diagnostic message.

- `LogConfig`
  - `level` – Minimum log level.
  - `handler` – Custom sink callback receiving `(LogLevel, std::string_view)`.

- `Config`
  - Transport-agnostic parameters shared across all implementations.
  - `spawnReceiveThread` – Controls whether transports start background receive threads (default `true`).
  - `receiveThreadSleep` – Baseline sleep interval for background receive loops when no data is available (default 5 ms).

### Callbacks

- `ReceiveCallback` – `void(const ByteVector&)`.
- `ErrorCallback` – `void(const Error&)`.
- `CallbackRegistry<Signature>` – Thread-safe helper for managing callbacks.

### Error Handling

- `Error` – Structured error reports containing category, severity, human-friendly message, optional system error code, and timestamp.
- `ErrorCode`, `ErrorCategory`, `ErrorSeverity` – Classification enums.
- `isRetryable(const Error&)` – Convenience helper for retry policies.
- `toString(ErrorCode|ErrorCategory|ErrorSeverity)` – String views for logging.

### Transport Interface

```cpp
class ICommunication {
public:
    virtual ~ICommunication() = default;

    virtual bool open() = 0;
    virtual bool close() = 0;
    virtual bool isOpen() const = 0;

    virtual std::ptrdiff_t send(const ByteVector& data) = 0;
    virtual std::ptrdiff_t receive(ByteVector& buffer, std::size_t maxSize) = 0;

    virtual bool configure(const Config& config) = 0;
    virtual Config getConfig() const = 0;

    virtual Statistics getStatistics() const = 0;
    virtual bool isHealthy() const = 0;

    virtual void setReceiveCallback(ReceiveCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
};
```

Implementations should avoid throwing exceptions. Prefer structured error returns and callbacks for asynchronous reporting.

## Ethernet Namespace

### `comm::ethernet::Endpoint`

Lightweight address container with `std::string address` and `uint16_t port`.

### `comm::ethernet::UDPConfig`

Extends `comm::Config` with UDP-specific fields:

- `Endpoint local` – Local bind address (`address`, `port`).
- `Endpoint remote` – Default peer for outbound datagrams.
- `allowBroadcast` – Enables sending broadcast frames.
- `joinMulticast`/`multicastGroup` – Controls multicast membership.
- `multicastTTL`/`multicastLoopback` – Multicast behaviour tuning.
- `autoRebind` – Enables automatic rebind strategies with periodic health checks.

### `comm::ethernet::UDP`

Implements `ICommunication` for UDP sockets with thread-safe send/receive, statistics collection, and callback dispatch. Features include configurable bind/remote endpoints, multicast participation, non-blocking mode, and receive loop management guarded by `ReceiveCallbackRegistry`.

Honours `Config::spawnReceiveThread` and `Config::receiveThreadSleep` to control whether an internal polling loop is created and how frequently it yields when idle.

### `comm::ethernet::TCPConfig`

Extends `comm::Config` with TCP-specific properties:

- `Endpoint local` – Optional bind address for clients or servers.
- `Endpoint remote` – Remote peer when operating as a client.
- `reuseAddress` – Enables `SO_REUSEADDR` on sockets.
- `noDelay` – Toggles `TCP_NODELAY` for low-latency traffic.
- `keepAlive` – Enables `SO_KEEPALIVE` probes.
- `listenBacklog` – Queue depth for passive (server) endpoints.

### `comm::ethernet::TCP`

Implements `ICommunication` for TCP streams supporting client and server roles. Provides synchronous read/write, optional auto-reconnect for clients, connection accept handling for servers, and integrates with the standard callback/statistics infrastructure.

Respects shared receive thread controls, allowing applications to run entirely in user-managed threads when desired.

## Serial Namespace

### Serial Configuration

- `comm::serial::SerialConfig` extends `Config` and exposes:
  - `device` – Device path (e.g. `/dev/ttyUSB0`).
  - `baudRate` – Integer baud rate (9600–921600).
  - `dataBits`, `parity`, `stopBits`, `flowControl` – Frame configuration enums.
  - `readTimeout` – Timeout applied to blocking reads.
  - `autoReconnect`, `reconnectInterval` – Placeholders for advanced recovery.

### `comm::serial::SerialPort`

POSIX `termios`-based implementation handling open/close, runtime configuration, synchronous send/receive operations, statistics updates, and error callbacks.

## CAN Namespace

### `comm::can::PCANBasicConfig`

- `handle` – Numerical PCAN channel handle (e.g. USB1/USB2).
- `bitrate` – Classical CAN bitrate constant accepted by PCAN-Basic.
- `enableFD`/`fdBitrate` – Enable CAN FD mode with detailed timing string.
- `listenOnly` – Activate passive monitoring mode.
- `hardwareTimestamps` – Toggle timestamp retrieval via `PCAN_SetValue`.
- Inherits retry/timeouts/autoReconnect from `comm::Config` to manage recovery.

### `comm::can::PCANBasic`

Linux implementation backed by Peak-System's `libpcanbasic`. Offers synchronous send/receive operations, optional background receive loop, automatic bus recovery, and structured error propagation. Falls back to no-op stubs when the PCAN-Basic dependency is not present at build time.

The background receive loop can be disabled via `Config::spawnReceiveThread`, with the sleep cadence controlled through `Config::receiveThreadSleep`.
