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

Requirements: Linux, Clang 21 (the Makefile default), `make`, `ar`, and
OpenSSL development headers/libraries.

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
