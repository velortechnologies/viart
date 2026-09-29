#!/usr/bin/env python3
import os
import socket
import subprocess
import tempfile
import time
from pathlib import Path

BINARY = Path(os.environ.get("VIART_BROKER_BINARY", Path(__file__).resolve().parents[1] / "build/viartd"))

def wait_socket(path):
    for _ in range(100):
        if os.path.exists(path): return
        time.sleep(.01)
    raise AssertionError("no broker socket")

def connect(path):
    s=socket.socket(socket.AF_UNIX);s.settimeout(2);s.connect(path);return s

with tempfile.TemporaryDirectory(prefix="viart-limits-") as temp:
    path=temp+"/sock"
    p=subprocess.Popen([str(BINARY),"-B",path,"--max-clients","1","--handshake-ms","100","--idle-ms","250"])
    try:
        wait_socket(path)
        slow=connect(path)
        assert slow.recv(3)==b"\xeb\x01\x00"
        time.sleep(.35)
        assert slow.recv(1)==b"", "handshake timeout failed"
        slow.close()
        active=connect(path)
        assert active.recv(3)==b"\xeb\x01\x00"
        active.sendall(b"\xeb\x01\x00")
        assert active.recv(1)==b"\x01"
        active.sendall(b"\x01\x00a")
        assert active.recv(1)==b"\x01"
        extra=connect(path)
        assert extra.recv(1)==b"", "max-clients failed"
        extra.close()
        time.sleep(.5)
        assert active.recv(1)==b"", "idle timeout failed"
        active.close()
    finally:
        p.terminate();p.wait(timeout=3)
        assert p.returncode==0
        assert not os.path.exists(path), "owned socket was not cleaned"
print("broker limits: max clients, handshake timeout, idle timeout, socket cleanup OK")
