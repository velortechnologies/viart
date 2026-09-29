#!/usr/bin/env python3
"""Wire-level smoke test for the standalone single-thread broker."""
import os
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

BINARY = Path(os.environ.get("VIART_BROKER_BINARY", Path(__file__).resolve().parents[1] / "build/viartd"))

def exact(sock, size):
    result = b""
    while len(result) < size:
        part = sock.recv(size - len(result))
        assert part, "unexpected EOF"
        result += part
    return result

def connect(path, name):
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(2)
    s.connect(path)
    assert exact(s, 3) == b"\xeb\x01\x00"
    s.sendall(b"\xeb")
    s.sendall(b"\x01\x00")
    assert exact(s, 1) == b"\x01"
    raw = name.encode()
    s.sendall(struct.pack("<H", len(raw)))
    s.sendall(raw)
    assert exact(s, 1) == b"\x01"
    return s

def command(sock, ident, op, qos, body):
    sock.sendall(struct.pack("<IBI", ident, op | qos << 6, len(body)) + body)

def ack(sock, ident, code=1):
    assert exact(sock, 6) == b"\xfe" + struct.pack("<I", ident) + bytes([code])

def delivery(sock, kind, sender, topic, payload):
    header = exact(sock, 6)
    assert header[0] == kind
    body = exact(sock, struct.unpack("<I", header[1:5])[0])
    expected = sender.encode() + b"\0" + (topic.encode() + b"\0" if topic else b"") + payload
    assert body == expected, (body, expected)

with tempfile.TemporaryDirectory(prefix="viart-broker-") as temp:
    path = temp + "/sock"
    broker = subprocess.Popen([str(BINARY), "-B", path], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            if os.path.exists(path): break
            if broker.poll() is not None: raise RuntimeError(broker.stderr.read().decode())
            time.sleep(.01)
        a = connect(path, "client.a")
        duplicate = socket.socket(socket.AF_UNIX)
        duplicate.settimeout(2); duplicate.connect(path)
        assert exact(duplicate, 3) == b"\xeb\x01\x00"
        duplicate.sendall(b"\xeb\x01\x00")
        assert exact(duplicate, 1) == b"\x01"
        duplicate.sendall(struct.pack("<H", 8) + b"client.a")
        assert exact(duplicate, 1) == b"\x76"
        duplicate.close()
        b = connect(path, "client.b")
        command(a, 1, 2, 1, b"demo/#")
        ack(a, 1)
        command(b, 2, 1, 1, b"demo/x\0hello")
        delivery(a, 1, "client.b", "demo/x", b"hello")
        ack(b, 2)
        command(a, 30, 4, 1, b"demo/x")
        ack(a, 30)
        command(b, 31, 1, 1, b"demo/x\0excluded")
        ack(b, 31)
        a.settimeout(.2)
        try: a.recv(1); raise AssertionError("received excluded topic")
        except socket.timeout: pass
        a.settimeout(2)
        command(a, 32, 5, 1, b"demo/x")
        ack(a, 32)
        command(b, 33, 1, 1, b"demo/x\0restored")
        delivery(a, 1, "client.b", "demo/x", b"restored")
        ack(b, 33)
        command(b, 34, 0x13, 1, b"client.*\0broadcast")
        delivery(a, 0x13, "client.b", None, b"broadcast")
        delivery(b, 0x13, "client.b", None, b"broadcast")
        ack(b, 34)
        secondary = connect(path, "client.a%%1")
        command(secondary, 35, 2, 1, b"demo/#")
        ack(secondary, 35)
        command(b, 36, 6, 1, b"demo/x\0client.a\0for-primary")
        delivery(a, 1, "client.b", "demo/x", b"for-primary")
        delivery(secondary, 1, "client.b", "demo/x", b"for-primary")
        ack(b, 36)
        command(secondary, 37, 3, 1, b"demo/#")
        ack(secondary, 37)
        command(b, 3, 0x12, 1, b"client.a\0direct")
        delivery(a, 0x12, "client.b", None, b"direct")
        ack(b, 3)
        command(a, 4, 3, 1, b"demo/#")
        ack(a, 4)
        command(b, 5, 1, 1, b"demo/x\0silent")
        ack(b, 5)
        a.settimeout(.2)
        try: a.recv(1); raise AssertionError("received after unsubscribe")
        except socket.timeout: pass
        assert broker.poll() is None
        a.close()
        secondary.settimeout(1)
        assert secondary.recv(1) == b"", "secondary remained after primary disconnect"
        secondary.close(); b.close()
        print("broker integration: handshake, pub/sub, excludes, broadcast, secondary, ACK, direct OK")
    finally:
        broker.terminate()
        broker.wait(timeout=3)
        assert broker.returncode == 0, broker.stderr.read().decode()
