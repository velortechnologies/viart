#!/usr/bin/env python3
"""Authenticated WSS, TLS certificate validation, and VIART RPC smoke."""
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

async def run():
    with tempfile.TemporaryDirectory(prefix="viart-wss-") as temp:
        cert = Path(temp) / "cert.pem"
        key = Path(temp) / "key.pem"
        tokens = Path(temp) / "tokens"
        tokens.write_text("alice abcdefghijklmnop0123456789\n"
                          "rpc.caller.test abcdefghijklmnop0123456789\n"
                          "rpc.handler.test abcdefghijklmnop0123456789\n")
        tokens.chmod(0o600)
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-noenc",
                        "-keyout", str(key), "-out", str(cert), "-days", "1",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        reserved = socket.socket()
        reserved.bind(("127.0.0.1", 0))
        port = reserved.getsockname()[1]
        reserved.close()
        broker = subprocess.Popen([str(BINARY), "--wss", f"127.0.0.1:{port}",
                                   "--tls-cert", str(cert), "--tls-key", str(key),
                                   "--tokens", str(tokens)], stderr=subprocess.PIPE)
        try:
            ctx = ssl.create_default_context(cafile=str(cert))
            uri = f"wss://127.0.0.1:{port}"
            for _ in range(100):
                if broker.poll() is not None: raise RuntimeError(broker.stderr.read().decode())
                try:
                    reader, writer = await asyncio.open_connection("127.0.0.1", port)
                    writer.close(); await writer.wait_closed()
                    break
                except ConnectionRefusedError:
                    await asyncio.sleep(.01)
            else: raise RuntimeError("WSS listener did not start")
            try:
                async with websockets.connect(uri, ssl=ctx): pass
                raise AssertionError("missing bearer accepted")
            except (websockets.exceptions.InvalidMessage, websockets.exceptions.InvalidStatusCode,
                    ConnectionError, EOFError): pass
            async with websockets.connect(uri, ssl=ctx,
                    extra_headers={"Authorization": "Bearer abcdefghijklmnop0123456789"}) as ws:
                assert await ws.recv() == b"\xeb\x01\x00"
                await ws.send(b"\xeb\x01\x00")
                assert await ws.recv() == b"\x01"
                await ws.send(struct.pack("<H", 3) + b"bob")
                assert await ws.recv() == b"\x79"
            async with websockets.connect(uri, ssl=ctx,
                    extra_headers={"Authorization": "Bearer abcdefghijklmnop0123456789"}) as ws:
                assert await ws.recv() == b"\xeb\x01\x00"
                await ws.send(b"\xeb\x01\x00")
                assert await ws.recv() == b"\x01"
                await ws.send(struct.pack("<H", 5) + b"alice")
                assert await ws.recv() == b"\x01"
                body = b".broker\0\x01" + struct.pack("<I", 1) + b"test\0"
                await ws.send(struct.pack("<IBI", 1, 0x52, len(body)) + body)
                first, second = await ws.recv(), await ws.recv()
                assert any(b"\x11\x01\x00\x00\x00\x81\xa2ok\xc3" in x for x in (first, second))
            env = {**os.environ, "VIART_TEST_SOCKET": uri, "VIART_TEST_CA": str(cert),
                   "VIART_TEST_TOKEN": "abcdefghijklmnop0123456789",
                   "VIART_TEST_CALLER": "rpc.caller.test", "VIART_TEST_HANDLER": "rpc.handler.test"}
            subprocess.run([str(BINARY.parent / "test_rpc_client")], env=env, check=True)
            probe = str(BINARY.parent / "test_ws_security")
            subprocess.run([probe, uri, "alice", "-", "abcdefghijklmnop0123456789", "0"], check=True)
            subprocess.run([probe, uri, "alice", str(cert), "wrongtokenwrongtoken", "0"], check=True)
            print("WSS: trusted cert, bearer required, token bound to identity, RPC OK")
        finally:
            broker.terminate()
            broker.wait(timeout=3)
            assert broker.returncode == 0, broker.stderr.read().decode()

asyncio.run(run())
