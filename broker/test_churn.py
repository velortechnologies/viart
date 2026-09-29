#!/usr/bin/env python3
"""Repeated register/disconnect/reuse on existing TCP, WS, and WSS transports."""
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
CYCLES = int(os.environ.get("VIART_CHURN_CYCLES", "200"))
MAX_RSS_KIB = int(os.environ.get("VIART_CHURN_MAX_RSS_KIB", "32768"))

def free_port():
    s=socket.socket();s.bind(("127.0.0.1",0));port=s.getsockname()[1];s.close();return port

async def register_raw(port,name):
    r,w=await asyncio.open_connection("127.0.0.1",port)
    assert await r.readexactly(3)==b"\xeb\x01\x00"
    w.write(b"\xeb\x01\x00");await w.drain()
    assert await r.readexactly(1)==b"\x01"
    raw=name.encode();w.write(struct.pack("<H",len(raw))+raw);await w.drain()
    result=await r.readexactly(1)
    if result!=b"\x01":w.close();await w.wait_closed();return False
    w.close();await w.wait_closed();return True

async def register_ws(uri,name,ctx=None,token=False):
    kw={"compression":None}
    if ctx:kw["ssl"]=ctx
    if token:kw["extra_headers"]={"Authorization":"Bearer abcdefghijklmnop0123456789"}
    async with websockets.connect(uri,**kw) as ws:
        assert await ws.recv()==b"\xeb\x01\x00"
        await ws.send(b"\xeb\x01\x00")
        assert await ws.recv()==b"\x01"
        raw=name.encode();await ws.send(struct.pack("<H",len(raw))+raw)
        return await ws.recv()==b"\x01"

async def retry(fn):
    for _ in range(20):
        if await fn():return
        await asyncio.sleep(.01)
    raise AssertionError("name was not reusable after disconnect")

async def main():
    with tempfile.TemporaryDirectory(prefix="viart-churn-") as temp:
        cert,key,tokens=(Path(temp)/n for n in ("cert.pem","key.pem","tokens"))
        subprocess.run(["openssl","req","-x509","-newkey","rsa:2048","-noenc",
                        "-keyout",str(key),"-out",str(cert),"-days","1","-subj","/CN=localhost",
                        "-addext","subjectAltName=DNS:localhost,IP:127.0.0.1"],
                       check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        tokens.write_text("secure abcdefghijklmnop0123456789\n");tokens.chmod(0o600)
        tcp,ws,wss=(free_port() for _ in range(3))
        broker=subprocess.Popen([str(BINARY),"--tcp",f"127.0.0.1:{tcp}",
            "--ws",f"127.0.0.1:{ws}","--wss",f"127.0.0.1:{wss}",
            "--tls-cert",str(cert),"--tls-key",str(key),"--tokens",str(tokens)],stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                if broker.poll() is not None:raise RuntimeError(broker.stderr.read().decode())
                try:
                    r,w=await asyncio.open_connection("127.0.0.1",tcp)
                    w.close();await w.wait_closed();break
                except ConnectionRefusedError:await asyncio.sleep(.01)
            ctx=ssl.create_default_context(cafile=str(cert))
            for i in range(CYCLES):
                await retry(lambda:register_raw(tcp,"raw"))
                await retry(lambda:register_ws(f"ws://127.0.0.1:{ws}","plain"))
                await retry(lambda:register_ws(f"wss://127.0.0.1:{wss}","secure",ctx,True))
                assert broker.poll() is None
            await asyncio.sleep(.2)
            status=Path(f"/proc/{broker.pid}/status").read_text()
            rss=int(next(line.split()[1] for line in status.splitlines() if line.startswith("VmRSS:")))
            assert rss<MAX_RSS_KIB,f"broker RSS grew to {rss} KiB (limit {MAX_RSS_KIB})"
            print(f"churn: {CYCLES} cycles x TCP/WS/WSS, same names reusable, RSS={rss} KiB")
        finally:
            broker.terminate();broker.wait(timeout=5)
            assert broker.returncode==0,broker.stderr.read().decode()

asyncio.run(main())
