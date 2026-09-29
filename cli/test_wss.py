#!/usr/bin/env python3
"""CLI WSS certificate and bearer identity smoke."""
import os
import socket
import subprocess
import tempfile
import time
from pathlib import Path

broker_binary = os.environ.get("VIART_BROKER_BINARY", "build/viartd")
cli_binary = os.environ.get("VIART_CLI_BINARY", "build/viart-cli")
with tempfile.TemporaryDirectory(prefix="viart-cli-wss-") as temp:
    cert, key, tokens = (Path(temp) / name for name in ("cert.pem", "key.pem", "tokens"))
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-noenc",
                    "-keyout", str(key), "-out", str(cert), "-days", "1",
                    "-subj", "/CN=localhost", "-addext", "subjectAltName=IP:127.0.0.1"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    tokens.write_text("cli.wss.test abcdefghijklmnop0123456789\n")
    tokens.chmod(0o600)
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    broker = subprocess.Popen([broker_binary, "--wss", f"127.0.0.1:{port}",
                               "--tls-cert", str(cert), "--tls-key", str(key),
                               "--tokens", str(tokens)], stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            if broker.poll() is not None:
                raise RuntimeError(broker.stderr.read().decode())
            try:
                with socket.create_connection(("127.0.0.1", port), .1):
                    break
            except ConnectionRefusedError:
                time.sleep(.01)
        uri = f"wss://127.0.0.1:{port}"
        good = subprocess.run([cli_binary, uri, "-n", "cli.wss.test", "--ca", str(cert),
                               "--token", "abcdefghijklmnop0123456789", "broker", "info"],
                              capture_output=True, text=True, timeout=10)
        assert good.returncode == 0 and "version" in good.stdout, good
        for opts in (("--ca", str(cert)),
                     ("--ca", str(cert), "--token", "wrongtoken"),
                     ("--token", "abcdefghijklmnop0123456789")):
            bad = subprocess.run([cli_binary, uri, "-n", "cli.wss.test", *opts,
                                  "--timeout", "1", "broker", "info"],
                                 capture_output=True, text=True, timeout=5)
            assert bad.returncode != 0, bad
        print("CLI WSS: trusted cert + bearer succeeds; missing/wrong bearer and untrusted cert fail")
    finally:
        broker.terminate()
        broker.wait(timeout=5)
