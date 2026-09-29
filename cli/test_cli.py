#!/usr/bin/env python3
"""CLI smoke against a running broker Unix socket."""
import os
import select
import subprocess
import time

binary = os.environ.get("VIART_CLI_BINARY", "build/viart-cli")
sock = os.environ.get("VIART_CLI_SOCKET", "/tmp/viart-c-broker.sock")

def run(*args, input=None):
    p = subprocess.run([binary, sock, *args], input=input, capture_output=True,
                       text=True, timeout=10)
    assert p.returncode == 0, (args, p.stdout, p.stderr)
    return p.stdout

def read_until(p, needle, timeout=5):
    output = b""
    until = time.monotonic() + timeout
    while needle not in output and time.monotonic() < until:
        if select.select([p.stdout], [], [], .2)[0]:
            chunk = os.read(p.stdout.fileno(), 4096)
            if not chunk:
                break
            output += chunk
    assert needle in output, (needle, output, p.poll())
    return output

for method in ("info", "stats", "client.list", "test"):
    assert run("broker", method)
assert "hello" in run("rpc", "call", ".broker", "benchmark.test", "-", input="hello")
assert run("rpc", "call0", ".broker", "test").strip() == "OK"
assert run("benchmark", "-w", "2", "-i", "20", "--payload-size", "32").count("op/s") == 7

listener = subprocess.Popen([binary, sock, "-n", "viart.cli.listener", "listen", "-t", "cli/test"],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
try:
    read_until(listener, b"Listening")
    assert run("publish", "cli/test", "hello pub").strip() == "OK"
    read_until(listener, b"hello pub")
    assert run("send", "viart.cli.listener", "hello direct").strip() == "OK"
    read_until(listener, b"hello direct")
finally:
    listener.terminate()
    listener.communicate(timeout=5)

rpc_listener = subprocess.Popen([binary, sock, "-n", "viart.cli.rpc.listener", "rpc", "listen"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
try:
    read_until(rpc_listener, b"Listening")
    assert run("rpc", "notify", "viart.cli.rpc.listener", "hello notify").strip() == "OK"
    assert run("rpc", "call", "viart.cli.rpc.listener", "test") == "(empty)\n"
    typed = run("rpc", "call", "viart.cli.rpc.listener", "benchmark.selftest",
                "flag=true", "count=42", "neg=-7", "ratio=1.5", "text=x")
    for part in ('"flag":true', '"count":42', '"neg":-7', '"ratio":1.5', '"text":"x"'):
        assert part in typed, typed
finally:
    rpc_listener.terminate()
    rpc_listener.communicate(timeout=5)

print("CLI: broker/read, publish/send/listen, RPC call/call0/notify/listen, 7 benchmark phases OK")
