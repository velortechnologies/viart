#!/usr/bin/env python3
"""Core .broker RPC wire integration test."""
import os,socket,struct,subprocess,tempfile,time
from pathlib import Path
import msgpack
BINARY=os.environ.get("VIART_BROKER_BINARY",str(Path(__file__).resolve().parents[1]/"build/viartd"))
def exact(s,n):
 out=b""
 while len(out)<n:
  part=s.recv(n-len(out));assert part;out+=part
 return out
def connect(path):
 s=socket.socket(socket.AF_UNIX);s.settimeout(2);s.connect(path)
 assert exact(s,3)==b"\xeb\x01\x00";s.sendall(b"\xeb\x01\x00");assert exact(s,1)==b"\x01"
 name=b"rpc.test";s.sendall(struct.pack("<H",len(name))+name);assert exact(s,1)==b"\x01";return s
def rpc(s,ident,method,params=b""):
 data=b"\x01"+struct.pack("<I",ident)+method.encode()+b"\0"+params
 s.sendall(struct.pack("<IBI",ident,0x52,len(b".broker\0")+len(data))+b".broker\0"+data)
 answer=None;ack=False
 for _ in range(2):
  head=exact(s,6)
  if head[0]==0xfe:
   assert head[1:5]==struct.pack("<I",ident) and head[5]==1;ack=True
  else:
   assert head[0]==0x12
   body=exact(s,struct.unpack("<I",head[1:5])[0])
   assert body.startswith(b".broker\0")
   answer=body[8:]
 assert ack and answer and struct.unpack("<I",answer[1:5])[0]==ident
 return answer[0],answer[5:]
with tempfile.TemporaryDirectory(prefix="viart-rpc-") as temp:
 path=temp+"/sock";p=subprocess.Popen([BINARY,"-B",path],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  for _ in range(100):
   if os.path.exists(path):break
   time.sleep(.01)
  s=connect(path)
  kind,payload=rpc(s,1,"test");assert kind==0x11 and msgpack.unpackb(payload)=={"ok":True}
  kind,payload=rpc(s,2,"benchmark.test",b"mirror");assert kind==0x11 and payload==b"mirror"
  kind,payload=rpc(s,3,"info");assert kind==0x11 and "version" in msgpack.unpackb(payload)
  kind,payload=rpc(s,4,"stats");assert kind==0x11 and "r_frames" in msgpack.unpackb(payload)
  kind,payload=rpc(s,5,"client.list",msgpack.packb({"filter":"rpc.*"}))
  assert kind==0x11 and any(c["name"]=="rpc.test" for c in msgpack.unpackb(payload)["clients"])
  kind,payload=rpc(s,6,"absent.method");assert kind==0x12 and struct.unpack("<h",payload[:2])[0]==-32601
  s.close();print("broker RPC: test, benchmark.test, info, stats, client.list/filter, method error OK")
 finally:p.terminate();p.wait(timeout=3)
