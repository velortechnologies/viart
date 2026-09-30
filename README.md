# VIART

VIART is a Linux C23 real-time messaging library, single-threaded broker, and
command-line client. It supports pub/sub, direct messages, broadcast, processed
acknowledgements, and asynchronous RPC over Unix sockets, IPv4 TCP, WS, and WSS.

The project includes:

- `build/libviart.a` — client library; public headers are in [`include/`](include/).
- `build/viartd` — nonblocking `epoll` broker with resource limits, optional
  ACL rules, and WSS bearer-token authentication.
- `build/viart-cli` — messaging, RPC, and benchmark commands.

## Build and run

Requirements for the existing full Makefile build: Linux, Clang 21 (the
Makefile default), `make`, `ar`, and OpenSSL development headers/libraries.

```sh
make all broker
./build/viartd -B /tmp/viart.sock
```

In a second terminal:

```sh
./build/viart-cli /tmp/viart.sock broker info
./build/viart-cli /tmp/viart.sock broker client.list
./build/viart-demo /tmp/viart.sock demo-1
```

The demo subscribes, publishes a message to itself, and exits after three
seconds. Stop the broker with Ctrl-C. Choose another socket path if this one is
in use. See the [CLI guide](cli/README.md) and [broker guide](broker/README.md)
for more options.

To use the client library, include `viart.h` (and `viart_rpc.h` for RPC) and
link `build/libviart.a` with `-pthread -lssl -lcrypto`. See
[`examples/demo.c`](examples/demo.c).

### Optional components with Meson

Meson and Ninja provide separate, reproducible build directories. The default
Meson configuration preserves the full feature set:

```sh
CC=clang-21 meson setup build-full --buildtype=release
meson compile -C build-full
meson test -C build-full
```

For a small Unix/TCP-only client library and broker, without OpenSSL,
WebSocket, broker management RPC, or the bundled MessagePack codec:

```sh
CC=clang-21 meson setup build-minimal --buildtype=release \
    -Dws=false -Dbroker_rpc=false -Dcli=false -Ddemo=false
meson compile -C build-minimal
meson test -C build-minimal
```

The resulting broker is `build-minimal/viartd` and the client library is
`build-minimal/libviart.a`; link this library with `-pthread` only. Builds
without WS reject `--ws`, `--wss`, and token-map options, and client WS/WSS
connections return `EOPNOTSUPP`. Disabling `broker_rpc` removes the `.broker`
management methods and announcements, but not application RPC forwarding.
The MessagePack implementation is bundled source, not an external dependency;
the CLI uses it independently, so disable the CLI to omit it entirely.
Currently both WS and WSS require OpenSSL, including plain WS handshake code.

The Makefile remains available for the existing full-build and Python
integration-test workflows. Keep Meson build directories separate from it.

## Test

```sh
make test broker-test
```

Python integration tests require Python 3 and the packages in
`requirements-test.txt`:

```sh
python3 -m pip install -r requirements-test.txt
make cli-test broker-integration broker-rpc-integration broker-limits \
     broker-load broker-acl-integration broker-wss-integration
```

`make integration rpc-integration` tests the client against a separately
running VIART broker at `/tmp/viart-test.sock`. Set `VIART_TEST_SOCKET` for a
different endpoint.

## API notes

`viart_client_start()` starts a reactor thread and returns immediately.
Commands are thread-safe to enqueue. A successful `publish`, `send`, or
`subscribe` call means *queued locally*, not delivered. Processed QoS produces
a later ACK confirming broker processing, not target receipt. The bounded TX
queue returns `EAGAIN` when full.

Callbacks run on the reactor thread. Borrowed message buffers remain valid
only until the callback returns. Keep callbacks short; do not call
`stop`/`destroy` inside one. Re-subscribe on `VIART_CONNECTED` after reconnect.
RPC results and transport ACKs are separate; see
[`include/viart_rpc.h`](include/viart_rpc.h).

Endpoints are Unix paths, `IPv4:port`, `ws://IPv4:port`, and
`wss://IPv4:port`. DNS names and IPv6 are not supported. WSS validates the
server certificate and supports a private CA and bearer token. Raw TCP/WS
client names are unauthenticated and listeners bind to loopback by default;
read the [broker guide](broker/README.md) before exposing one.

## License

Apache-2.0. See [LICENSE](LICENSE).
