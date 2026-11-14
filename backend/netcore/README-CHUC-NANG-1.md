# Test the C transport and Go bridge

Build and run in Linux or WSL:

```sh
cd backend/netcore
make
```

## End-to-end request through C to Go

First start PostgreSQL and configure `backend/.env`. Then, in terminal 1, start the Go server (from the backend directory):

```sh
cd backend
go run ./cmd/server
```

In terminal 2, build and start the C gateway:

```sh
cd backend/netcore
make
./bin/bridge_gateway 9000 127.0.0.1 9100
```

In terminal 3, send `SYS_HEALTH` (message type `1`) with JSON payload `{}`:

```sh
cd backend/netcore
./bin/echo_client 127.0.0.1 9000 1
```

Enter `{}`. The client should print a Go response containing health status. Enter `/quit` to exit. The third client argument is the Go bridge message type; use `0x0201` for `FLIGHT_SEARCH` and provide the JSON fields expected by that handler.

Each C client gets its own `pthread` worker and Go bridge connection/session. To check concurrent clients, launch another client in a separate terminal.

## Standalone echo smoke check

The echo server remains available for checking framing/CRC without Go:

```sh
./bin/echo_server 9001
./bin/echo_client 127.0.0.1 9001
```

This mode echoes its JSON/text payload and does not call Go.

Proxy mode listens on port 9002 and forwards to a target TCP server, for example standalone echo on port 9001:

```sh
./bin/lossy_proxy 9002 9001
./bin/echo_client 127.0.0.1 9002
```

Optional proxy arguments are `drop_reply_once`, `corrupt_reply_once`, and `delay_ms`, for example:

```sh
./bin/lossy_proxy 9001 9000 0 0 500
```

The current proxy is an early TCP stream fault-injection tool. Drop/corrupt modes are not yet reliable frame-level scenarios, so use direct mode and delay mode for basic checks. The six planned scenarios and feature 10 logging/metrics are not complete.

The client-facing protocol uses a 32-byte header with CRC and req_id. The internal C-Go bridge retains its existing 24-byte header. The gateway translates between them.
