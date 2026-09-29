#!/usr/bin/env python3
"""Deny-by-default ACL over Unix and TCP; no ACL on the legacy test broker."""
import os
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

BINARY = Path(os.environ.get("VIART_BROKER_BINARY", Path(__file__).resolve().parents[1] / "build/viartd"))

def exact(sock, n):
    buf = b""
    while len(buf) < n:
        part = sock.recv(n - len(buf))
        assert part
        buf += part
    return buf

def connect(address, name, expected=1):
    sock = socket.socket(socket.AF_UNIX if isinstance(address, str) else socket.AF_INET)
    sock.settimeout(2)
    sock.connect(address)
    assert exact(sock, 3) == b"\xeb\x01\x00"
    sock.sendall(b"\xeb\x01\x00")
    assert exact(sock, 1) == b"\x01"
    raw = name.encode()
    sock.sendall(struct.pack("<H", len(raw)) + raw)
    assert exact(sock, 1) == bytes([expected])
    return sock

def command(sock, ident, op, body, expected):
    sock.sendall(struct.pack("<IBI", ident, op | 0x40, len(body)) + body)
    assert exact(sock, 6) == b"\xfe" + struct.pack("<I", ident) + bytes([expected])

with tempfile.TemporaryDirectory(prefix="viart-acl-") as temp:
    policy = Path(temp) / "policy"
    policy.write_text("""alice connect local
alice connect 127.0.0.0/8
alice subscribe telemetry/#
alice publish telemetry/#
alice p2p .broker
""")
    unix_path = str(Path(temp) / "broker.sock")
    reserved = socket.socket()
    reserved.bind(("127.0.0.1", 0))
    port = reserved.getsockname()[1]
    reserved.close()
    broker = subprocess.Popen([str(BINARY), "-B", unix_path, "--tcp", f"127.0.0.1:{port}", "--acl", str(policy)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            if os.path.exists(unix_path): break
            if broker.poll() is not None: raise RuntimeError(broker.stderr.read().decode())
            time.sleep(.01)
        denied = connect(unix_path, "bob", 0x79)
        denied.close()
        alice = connect(unix_path, "alice")
        command(alice, 1, 2, b"telemetry/#", 1)
        command(alice, 2, 2, b"#", 0x79)  # broad-subscription escalation denied
        command(alice, 7, 3, b"telemetry/#", 1)
        command(alice, 3, 1, b"secret/x\0data", 0x79)
        command(alice, 4, 1, b"telemetry/x\0data", 1)
        command(alice, 5, 0x13, b"*\0data", 0x79)
        alice.close()
        remote_denied = connect(("127.0.0.1", port), "bob", 0x79)
        remote_denied.close()
        remote = connect(("127.0.0.1", port), "alice")
        command(remote, 6, 0x12, b"forbidden\0data", 0x79)
        remote.close()
        assert broker.poll() is None
        print("ACL: Unix/TCP connect allow/deny, subscription scope, publish/broadcast, P2P OK")
    finally:
        broker.terminate()
        broker.wait(timeout=3)
        assert broker.returncode == 0, broker.stderr.read().decode()
