#!/usr/bin/env python3
"""Cross-transport fan-out under concurrent TCP, WS, and authenticated WSS clients."""
import asyncio
import os
import socket
import ssl
import struct
import subprocess
import tempfile
from pathlib import Path
import websockets

BINARY = Path(os.environ.get("VIART_BROKER_BINARY", Path(__file__).resolve().parents[1] / "build/viartd"))
N = int(os.environ.get("VIART_TRANSPORT_LOAD_N", "32"))
ROUNDS = int(os.environ.get("VIART_TRANSPORT_LOAD_ROUNDS", "1"))

def free_port():
    sock = socket.socket(); sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]; sock.close(); return port

def cmd(ident, op, body):
    return struct.pack("<IBI", ident, op | 0x40, len(body)) + body

async def raw_connect(port, name):
    r,w = await asyncio.open_connection("127.0.0.1", port)
    assert await r.readexactly(3) == b"\xeb\x01\x00"
    w.write(b"\xeb\x01\x00"); await w.drain()
    assert await r.readexactly(1) == b"\x01"
    raw=name.encode();w.write(struct.pack("<H",len(raw))+raw);await w.drain()
    assert await r.readexactly(1) == b"\x01"
    return r,w

async def main():
    with tempfile.TemporaryDirectory(prefix="viart-transport-load-") as temp:
        cert,key,tokens=(Path(temp)/n for n in ("cert.pem","key.pem","tokens"))
        subprocess.run(["openssl","req","-x509","-newkey","rsa:2048","-noenc",
                        "-keyout",str(key),"-out",str(cert),"-days","1","-subj","/CN=localhost",
                        "-addext","subjectAltName=DNS:localhost,IP:127.0.0.1"],
                       check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        tokens.write_text("".join(f"secure.{i} abcdefghijklmnop0123456789\n" for i in range(N)))
        tokens.chmod(0o600)
        tcp,ws,wss=(free_port() for _ in range(3))
        broker=subprocess.Popen([str(BINARY),"--tcp",f"127.0.0.1:{tcp}",
                                 "--ws",f"127.0.0.1:{ws}","--wss",f"127.0.0.1:{wss}",
                                 "--tls-cert",str(cert),"--tls-key",str(key),"--tokens",str(tokens)],
                                stderr=subprocess.PIPE)
        conns=[]
        try:
            for _ in range(100):
                if broker.poll() is not None: raise RuntimeError(broker.stderr.read().decode())
                try:
                    r,w=await asyncio.open_connection("127.0.0.1",tcp)
                    w.close();await w.wait_closed();break
                except ConnectionRefusedError: await asyncio.sleep(.01)
            ctx=ssl.create_default_context(cafile=str(cert))
            for i in range(N):
                r,w=await raw_connect(tcp,f"raw.{i}")
                w.write(cmd(1,2,b"load/#"));await w.drain()
                assert await r.readexactly(6)==b"\xfe"+struct.pack("<I",1)+b"\x01"
                conns.append(("raw",r,w))
            for i in range(N):
                conn=await websockets.connect(f"ws://127.0.0.1:{ws}",compression=None)
                assert await conn.recv()==b"\xeb\x01\x00"
                await conn.send(b"\xeb\x01\x00");assert await conn.recv()==b"\x01"
                name=f"plain.{i}".encode();await conn.send(struct.pack("<H",len(name))+name)
                assert await conn.recv()==b"\x01"
                await conn.send(cmd(1,2,b"load/#"))
                assert await conn.recv()==b"\xfe"+struct.pack("<I",1)+b"\x01"
                conns.append(("ws",conn,None))
            for i in range(N):
                conn=await websockets.connect(f"wss://127.0.0.1:{wss}",ssl=ctx,compression=None,
                    extra_headers={"Authorization":"Bearer abcdefghijklmnop0123456789"})
                assert await conn.recv()==b"\xeb\x01\x00"
                await conn.send(b"\xeb\x01\x00");assert await conn.recv()==b"\x01"
                name=f"secure.{i}".encode();await conn.send(struct.pack("<H",len(name))+name)
                assert await conn.recv()==b"\x01"
                await conn.send(cmd(1,2,b"load/#"))
                assert await conn.recv()==b"\xfe"+struct.pack("<I",1)+b"\x01"
                conns.append(("wss",conn,None))
            publisher,pw=await raw_connect(tcp,"publisher")
            for round_id in range(ROUNDS):
                payload=f"hit{round_id}".encode()
                pw.write(cmd(2+round_id,1,b"load/x\0"+payload));await pw.drain()
                assert await publisher.readexactly(6)==b"\xfe"+struct.pack("<I",2+round_id)+b"\x01"
                for kind,reader,_ in conns:
                    if kind=="raw":
                        header=await reader.readexactly(6)
                        msg=header+await reader.readexactly(struct.unpack("<I",header[1:5])[0])
                    else: msg=await reader.recv()
                    assert msg[0]==1 and b"load/x\0"+payload in msg,(kind,msg)
            pw.close();await pw.wait_closed()
            print(f"cross-transport fan-out: {N} TCP + {N} WS + {N} WSS subscribers x {ROUNDS} rounds OK")
        finally:
            for kind,reader,writer in conns:
                if kind=="raw":writer.close()
                else:await reader.close()
            broker.terminate();broker.wait(timeout=5)
            assert broker.returncode==0,broker.stderr.read().decode()

asyncio.run(main())
