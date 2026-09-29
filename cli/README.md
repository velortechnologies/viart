# VIART CLI

`make` builds `build/viart-cli`. Examples:

```sh
./build/viart-cli /tmp/viart.sock broker info
./build/viart-cli /tmp/viart.sock broker stats
./build/viart-cli /tmp/viart.sock broker client.list
./build/viart-cli /tmp/viart.sock listen -t 'sensors/#' --exclude sensors/noisy
./build/viart-cli /tmp/viart.sock publish sensors/temp '21.5'
./build/viart-cli /tmp/viart.sock send client.name 'hello'
./build/viart-cli /tmp/viart.sock rpc call .broker info
./build/viart-cli /tmp/viart.sock rpc listen -t 'rpc/#'
./build/viart-cli /tmp/viart.sock benchmark -w 4 -i 10000 --payload-size 100
```

Omit a publish/send/notify payload to read bytes from stdin. For RPC `call`
and `call0`, `-` reads raw parameters from stdin; otherwise `key=value`
arguments become a MessagePack map with booleans, signed integers, finite
floats, and strings. Replies show text or decoded MessagePack maps/arrays;
other binary payloads print as hex.

Global options after the endpoint: `-n/--name`, `--token`, `--ca`, `--timeout`
(seconds), `-s/--silent`, and `-v/--verbose`. Endpoints are Unix paths, numeric
IPv4 TCP, `ws://IPv4:port`, and `wss://IPv4:port`. For WSS, `--ca` selects a
private CA and the certificate must match the IP; `--token` supplies the
bearer token.

`send`, `publish`, `rpc notify`, and `rpc call0` wait for a processed transport
ACK. `rpc call` waits for an RPC reply/error. An ACK means broker processing,
not delivery to a recipient. `listen` and `rpc listen` run until SIGINT or
SIGTERM. The RPC listener replies with an empty result to ordinary calls and
echoes `benchmark.selftest`.

## Benchmark

Seven phases support configurable workers, iterations, and payload size:
`send.qos.no`, `send.qos.processed`, `send+recv.qos.no`,
`send+recv.qos.processed`, `rpc.call`, `rpc.call+handle`, and `rpc.call0`.
Each worker owns one raw and one RPC connection. Processed phases wait for
each broker ACK or reply. Receive phases end only after all expected messages
arrive. `send.qos.no` measures local enqueue throughput, **not** delivery.

Tests:

```sh
make cli-test
make cli-sanitize-test
```

The sanitizer WSS smoke disables LeakSanitizer because its detached bridge
thread may still release OpenSSL thread-local state during process exit;
ASan/UBSan remain enabled. The Unix CLI smoke enables leak detection.
