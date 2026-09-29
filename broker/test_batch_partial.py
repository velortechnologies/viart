#!/usr/bin/env python3
"""Check frame integrity after a slow reader forces short broker writes."""
import os
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

BINARY = Path(os.environ.get("VIART_BROKER_BINARY", Path(__file__).resolve().parents[1] / "build/viartd"))


def exact(sock, count):
    parts = []
    while count:
        part = sock.recv(count)
        assert part, "unexpected EOF"
        parts.append(part)
        count -= len(part)
    return b"".join(parts)


def connect(path, name):
    sock = socket.socket(socket.AF_UNIX)
    sock.settimeout(10)
    sock.connect(path)
    assert exact(sock, 3) == b"\xeb\x01\x00"
    sock.sendall(b"\xeb\x01\x00")
    assert exact(sock, 1) == b"\x01"
    raw = name.encode()
    sock.sendall(struct.pack("<H", len(raw)) + raw)
    assert exact(sock, 1) == b"\x01"
    return sock


with tempfile.TemporaryDirectory(prefix="viart-batch-partial-") as temp:
    path = temp + "/sock"
    broker = subprocess.Popen([str(BINARY), "-B", path],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            if broker.poll() is not None:
                raise RuntimeError(broker.stderr.read().decode())
            if os.path.exists(path):
                break
            time.sleep(.01)
        else:
            raise RuntimeError("broker socket did not appear")
        reader = connect(path, "reader")
        reader.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        publisher = connect(path, "publisher")
        reader.sendall(struct.pack("<IBI", 1, 2 | 1 << 6, 6) + b"bulk/#")
        assert exact(reader, 6) == b"\xfe" + struct.pack("<I", 1) + b"\x01"
        payloads = [struct.pack("<I", i) + bytes([i % 251]) * (512 + i % 1024)
                    for i in range(256)]
        commands = b"".join(struct.pack("<IBI", i + 2, 1, 7 + len(payload))
                            + b"bulk/x\0" + payload
                            for i, payload in enumerate(payloads))
        publisher.sendall(commands)
        time.sleep(.05)
        for payload in payloads:
            header = exact(reader, 6)
            assert header[0] == 1
            body = exact(reader, struct.unpack("<I", header[1:5])[0])
            assert body == b"publisher\0bulk/x\0" + payload
        reader.close()
        publisher.close()
        print("broker slow-reader batch integrity: 256 ordered frames OK")
    finally:
        broker.terminate()
        broker.wait(timeout=5)
