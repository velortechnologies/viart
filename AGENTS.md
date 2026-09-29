# Repository guidance for coding agents

This file applies to the entire VIART repository. Read `README.md` and the
public headers in `include/` before changing behavior.

If `AGENTS_LOCAL.md` exists in a developer checkout, read it too. It contains
local-only context and must never be committed.

- Preserve wire formats, public C API behavior, ACK semantics, and existing
  client/broker interactions. A processed ACK confirms broker processing, not
  delivery to a recipient.
- The broker uses a single-threaded, nonblocking `epoll` loop. Preserve bounded
  queues, partial-I/O handling, fairness budgets, and backpressure.
- Keep documentation and tests portable. Do not hard-code developer checkout
  paths, machine names, credentials, or environment-specific measurements.
- Build with `make all broker`. Run `make test broker-test cli-test` for ordinary
  changes and the relevant broker integration targets for touched paths.
  Python test dependencies are in `requirements-test.txt`.
- `make integration rpc-integration` needs a separately running VIART broker
  at `/tmp/viart-test.sock`, or a path set by `VIART_TEST_SOCKET`. Only report
  a test as passed if it actually ran and passed.
- For memory or concurrency changes, use relevant sanitizer and load tests.
  Record the workload, flags, duration, failures, and passes.
- For performance claims, distinguish completed operations from local queue
  insertion. In particular, `send.qos.no` is not delivered-message throughput.

Do not commit generated `build/` files, tokens, private keys, or test sockets.
