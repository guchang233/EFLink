"""Minimal protobuf encoder for building test server messages."""
import struct,base64
def varint(v):
    out=bytearray();v&=(1<<64)-1
    while True:
        b=v&0x7f;v>>=7
        if v: out.append(b|0x80)
        else: out.append(b);return bytes(out)
def key(n,w): return varint((n<<3)|w)
def u(n,v): return key(n,0)+varint(v)
def i32(n,v): return key(n,0)+varint(v if v>=0 else v+(1<<64))
def b(n,v): return key(n,0)+varint(1 if v else 0)
def d(n,v): return key(n,1)+struct.pack('<d',v)
def f(n,v): return key(n,5)+struct.pack('<f',v)
def s(n,v): v=v.encode(); return key(n,2)+varint(len(v))+v
def m(n,payload): return key(n,2)+varint(len(payload))+payload
def b64(x): return base64.b64encode(x).decode()
