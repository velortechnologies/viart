#!/usr/bin/env python3
"""Exercise bounded slow-reader behavior and indexed exact-topic routing."""
import os
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

BINARY = Path(os.environ.get("VIART_BROKER_BINARY", Path(__file__).resolve().parents[1] / "build/viartd"))

def exact(s,n):
    out=b""
    while len(out)<n:
        part=s.recv(n-len(out))
        assert part, "unexpected EOF"
        out+=part
    return out

def launch(path,*options):
    p=subprocess.Popen([str(BINARY),"-B",path,*options])
    for _ in range(100):
        if os.path.exists(path):return p
        time.sleep(.01)
    raise AssertionError("no broker socket")

def client(path,name):
    s=socket.socket(socket.AF_UNIX);s.settimeout(2);s.connect(path)
    assert exact(s,3)==b"\xeb\x01\x00"
    s.sendall(b"\xeb\x01\x00");assert exact(s,1)==b"\x01"
    raw=name.encode();s.sendall(struct.pack("<H",len(raw))+raw);assert exact(s,1)==b"\x01"
    return s

def cmd(s,ident,op,qos,body):s.sendall(struct.pack("<IBI",ident,op|qos<<6,len(body))+body)
def ack(s,ident,result=1):assert exact(s,6)==b"\xfe"+struct.pack("<I",ident)+bytes([result])

with tempfile.TemporaryDirectory(prefix="viart-load-") as temp:
    path=temp+"/small.sock"
    p=launch(path,"--max-queue","64")
    try:
        reader=client(path,"slow.reader");sender=client(path,"sender")
        cmd(reader,1,2,1,b"bulk/#");ack(reader,1)
        cmd(sender,2,1,1,b"bulk/x\0"+b"x"*128);ack(sender,2)
        reader.settimeout(1)
        assert reader.recv(1)==b"", "slow reader not disconnected on queue overflow"
        reader.close();sender.close()
    finally:p.terminate();p.wait(timeout=3)
    path=temp+"/indexed.sock"
    p=launch(path)
    clients=[]
    try:
        for i in range(128):
            s=client(path,f"indexed.{i}")
            cmd(s,i+1,2,1,f"stress/{i}".encode());ack(s,i+1)
            clients.append(s)
        sender=client(path,"indexed.sender")
        start=time.monotonic()
        cmd(sender,500,1,1,b"stress/64\0ok")
        hdr=exact(clients[64],6);assert hdr[0]==1
        body=exact(clients[64],struct.unpack("<I",hdr[1:5])[0]);assert body==b"indexed.sender\0stress/64\0ok"
        ack(sender,500)
        elapsed=time.monotonic()-start
        for i in (0,32,96,127):
            clients[i].settimeout(.05)
            try:clients[i].recv(1);raise AssertionError(f"wrong subscriber {i}")
            except socket.timeout:pass
        print(f"broker load: bounded slow-reader disconnect; 128 exact-topic clients, target delivery in {elapsed*1000:.1f} ms")
        sender.close()
        for s in clients:s.close()
    finally:p.terminate();p.wait(timeout=3)
