#!/usr/bin/env python3
import os,socket,struct,subprocess,tempfile,time
from pathlib import Path
import msgpack
BINARY=os.environ.get("VIART_BROKER_BINARY",str(Path(__file__).resolve().parents[1]/"build/viartd"))
def exact(s,n):
 data=b""
 while len(data)<n:
  part=s.recv(n-len(data));assert part;data+=part
 return data
def connect(path,name):
 s=socket.socket(socket.AF_UNIX);s.settimeout(3);s.connect(path)
 assert exact(s,3)==b"\xeb\x01\x00";s.sendall(b"\xeb\x01\x00");assert exact(s,1)==b"\x01"
 raw=name.encode();s.sendall(struct.pack("<H",len(raw))+raw);assert exact(s,1)==b"\x01";return s
def subscribe(s,topic,ident):
 raw=topic.encode();s.sendall(struct.pack("<IBI",ident,0x42,len(raw))+raw)
 assert exact(s,6)==b"\xfe"+struct.pack("<I",ident)+b"\x01"
def event(s,topic,subject):
 h=exact(s,6);assert h[0]==1
 body=exact(s,struct.unpack("<I",h[1:5])[0]);sender,got,payload=body.split(b"\0",2)
 assert sender==b".broker" and got.decode()==topic
 obj=msgpack.unpackb(payload);assert obj["s"]==subject and obj["t"]>0
 return obj
with tempfile.TemporaryDirectory(prefix="viart-rpc-events-") as d:
 path=d+"/sock";p=subprocess.Popen([BINARY,"-B",path],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  for _ in range(100):
   if os.path.exists(path):break
   time.sleep(.01)
  watcher=connect(path,"watcher");subscribe(watcher,".broker/info",1);subscribe(watcher,".broker/warn",2)
  other=connect(path,"new.client")
  assert event(watcher,".broker/info","reg")["d"]=="new.client"
  other.close()
  assert event(watcher,".broker/info","unreg")["d"]=="new.client"
  p.terminate();event(watcher,".broker/warn","shutdown")
  watcher.close();p.wait(timeout=3)
  print("broker RPC announcements: reg, unreg, shutdown OK")
 finally:
  if p.poll() is None:p.terminate();p.wait(timeout=3)
