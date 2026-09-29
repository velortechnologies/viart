# VIART broker

`build/viartd` serves Unix, numeric IPv4 TCP, WS, and WSS clients in one
nonblocking `epoll` loop. The broker has no worker threads.

## Implemented behavior

- Client registration, unique names, and secondary-client lifecycle.
- Topic subscribe/unsubscribe and exclude/unexclude, including wildcard masks.
- Publish, publish-for-primary, direct send, broadcast, ping/NOP, and processed
  acknowledgements. An ACK confirms broker processing, not recipient receipt.
- RPC routing and `.broker` methods `test`, `benchmark.test`, `info`, `stats`,
  and `client.list` (optional regex filter). Unknown methods return `-32601`.
- Bounded per-client output queues, partial I/O, slow-reader disconnects,
  handshake/idle timeouts, configurable limits, and graceful shutdown.
- Optional deny-by-default ACL rules for connect, subscribe, publish, direct,
  and broadcast operations. Connect rules can match Unix UID or IPv4 CIDR.
- WSS with OpenSSL TLS 1.2+ and optional identity-bound bearer tokens.

## Build and run

From the repository root:

```sh
make broker broker-test broker-integration broker-limits broker-load \
     broker-rpc-integration broker-rpc-events broker-acl-integration \
     broker-wss-integration broker-transports-load
./build/viartd -B /tmp/viart.sock
```

At least one listener is required. Optional listeners and policy flags include
`--tcp`, `--ws`, `--wss`, `--tls-cert`, `--tls-key`, `--tokens`, and `--acl`.
The Unix socket path must not already belong to another process. Resource
limits include `--max-clients`, `--max-frame`, `--max-queue`, `--handshake-ms`,
and `--idle-ms`.

Example ACL (one grant per line; comments start with `#`):

```text
camera connect uid:1000
camera connect 10.1.0.0/16
camera subscribe telemetry/#
camera publish camera/+/status
camera p2p controller.*
camera broadcast controller.*
```

A token file line is `camera <random-token-at-least-16-characters>`. The file
must be regular and inaccessible to group/other users (`chmod 600`). WSS
clients send `Authorization: Bearer <token>` and must register under the
matching name. Never put real tokens in command-line arguments or docs.

Without `--acl`, local operation is permissive. With an ACL, a missing grant
denies the operation. Raw TCP/WS names are not cryptographically authenticated;
those listeners bind to loopback by default. Non-loopback raw binds require
`--allow-public-tcp`; use WSS with tokens for untrusted networks. A non-loopback
WSS bind requires a token map.
