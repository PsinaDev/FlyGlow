"""Space Station IPC helper: schema dump, pipe-capture decoder and a read-only pipe probe."""
import argparse
import base64
import gzip
import json
import os
import re
import struct
import sys
import time

DEFAULT_ASAR = r"C:\Program Files\Flydigi Space Station\resources\app.asar"
PIPE = r"\\.\pipe\fcs.sock"


def read_varint(b, i):
    shift = 0
    v = 0
    while True:
        c = b[i]
        i += 1
        v |= (c & 0x7F) << shift
        if not c & 0x80:
            return v, i
        shift += 7


def iter_fields(b):
    """Yield (field_number, wire_type, value) for one message buffer."""
    i = 0
    n = len(b)
    while i < n:
        key, i = read_varint(b, i)
        fn, wt = key >> 3, key & 7
        if wt == 0:
            v, i = read_varint(b, i)
        elif wt == 1:
            v = b[i:i + 8]
            i += 8
        elif wt == 2:
            ln, i = read_varint(b, i)
            v = b[i:i + ln]
            i += ln
        elif wt == 5:
            v = b[i:i + 4]
            i += 4
        else:
            raise ValueError(f"unsupported wire type {wt} at {i}")
        yield fn, wt, v


def enc_varint(v):
    if v < 0:
        v += 1 << 64
    out = bytearray()
    while True:
        c = v & 0x7F
        v >>= 7
        if v:
            out.append(c | 0x80)
        else:
            out.append(c)
            return bytes(out)


def enc_key(fn, wt):
    return enc_varint((fn << 3) | wt)


def asar_read(path, want):
    with open(path, "rb") as f:
        _, pickle_size, _, json_len = struct.unpack("<4I", f.read(16))
        hdr = json.loads(f.read(json_len).decode("utf-8"))
        node = hdr
        for part in want.split("/"):
            node = node["files"][part]
        f.seek(8 + pickle_size + int(node["offset"]))
        return f.read(node["size"])


def extract_descriptors(asar):
    js = asar_read(asar, ".vite/build/main.js").decode("utf-8")
    out = []
    # A FileDescriptorProto starts with 0x0A <len> "<name>.proto", which base64-encodes to "C" + one of g..v.
    for m in re.finditer(r'[\w$]+\("(C[g-v][A-Za-z0-9+/=]{20,})"', js):
        s = m.group(1)
        s += "=" * (-len(s) % 4)
        try:
            b = base64.b64decode(s)
        except Exception:
            continue
        if len(b) > 3 and b[0] == 0x0A and b[2:2 + b[1]].endswith(b".proto"):
            out.append(b)
    return out


TYPE_NAMES = {1: "double", 2: "float", 3: "int64", 4: "uint64", 5: "int32", 6: "fixed64",
              7: "fixed32", 8: "bool", 9: "string", 10: "group", 11: "message", 12: "bytes",
              13: "uint32", 14: "enum", 15: "sfixed32", 16: "sfixed64", 17: "sint32", 18: "sint64"}


def _s(b):
    return bytes(b).decode("utf-8")


def parse_enum(b, prefix):
    e = {"name": "", "values": []}
    for fn, wt, v in iter_fields(b):
        if fn == 1:
            e["name"] = _s(v)
        elif fn == 2:
            name, num = "", 0
            for f2, w2, v2 in iter_fields(v):
                if f2 == 1:
                    name = _s(v2)
                elif f2 == 2:
                    num = v2 - (1 << 64) if v2 >= 1 << 63 else v2
            e["values"].append((name, num))
    e["full"] = prefix + "." + e["name"]
    return e


def parse_message(b, prefix, msgs, enums):
    m = {"name": "", "fields": [], "oneofs": []}
    nested = []
    nested_enums = []
    for fn, wt, v in iter_fields(b):
        if fn == 1:
            m["name"] = _s(v)
        elif fn == 2:
            f = {"label": 1, "oneof": None, "type_name": "", "json": "", "proto3_optional": False}
            for f2, w2, v2 in iter_fields(v):
                if f2 == 1:
                    f["name"] = _s(v2)
                elif f2 == 3:
                    f["number"] = v2
                elif f2 == 4:
                    f["label"] = v2
                elif f2 == 5:
                    f["type"] = v2
                elif f2 == 6:
                    f["type_name"] = _s(v2).lstrip(".")
                elif f2 == 9:
                    f["oneof"] = v2
                elif f2 == 10:
                    f["json"] = _s(v2)
                elif f2 == 17:
                    f["proto3_optional"] = bool(v2)
            if not f["json"]:
                parts = f["name"].split("_")
                f["json"] = parts[0] + "".join(p[:1].upper() + p[1:] for p in parts[1:])
            m["fields"].append(f)
        elif fn == 3:
            nested.append(v)
        elif fn == 4:
            nested_enums.append(v)
        elif fn == 8:
            for f2, w2, v2 in iter_fields(v):
                if f2 == 1:
                    m["oneofs"].append(_s(v2))
    m["full"] = prefix + "." + m["name"]
    msgs[m["full"]] = m
    for nb in nested:
        parse_message(nb, m["full"], msgs, enums)
    for eb in nested_enums:
        e = parse_enum(eb, m["full"])
        enums[e["full"]] = e
    return m


def parse_file(b, msgs, enums):
    f = {"name": "", "package": "", "deps": [], "messages": [], "enums": []}
    raw_msgs, raw_enums = [], []
    for fn, wt, v in iter_fields(b):
        if fn == 1:
            f["name"] = _s(v)
        elif fn == 2:
            f["package"] = _s(v)
        elif fn == 3:
            f["deps"].append(_s(v))
        elif fn == 4:
            raw_msgs.append(v)
        elif fn == 5:
            raw_enums.append(v)
    for eb in raw_enums:
        e = parse_enum(eb, f["package"])
        enums[e["full"]] = e
        f["enums"].append(e["full"])
    for mb in raw_msgs:
        m = parse_message(mb, f["package"], msgs, enums)
        f["messages"].append(m["full"])
    return f


class Schema:
    def __init__(self, asar=DEFAULT_ASAR):
        self.msgs, self.enums, self.files = {}, {}, []
        for b in extract_descriptors(asar):
            self.files.append(parse_file(b, self.msgs, self.enums))
        self.pkg = self.files[0]["package"] if self.files else ""

    def msg(self, short):
        if short in self.msgs:
            return self.msgs[short]
        return self.msgs[self.pkg + "." + short]

    def enum_value(self, enum_full, name):
        for n, v in self.enums[enum_full]["values"]:
            if n == name:
                return v
        raise KeyError(name)

    def enum_name(self, enum_full, num):
        e = self.enums.get(enum_full)
        if e:
            for n, v in e["values"]:
                if v == num:
                    return n
        return num

    def proto_text(self, f):
        out = [f'// {f["name"]}', 'syntax = "proto3";', f'package {f["package"]};']
        for d in f["deps"]:
            out.append(f'import "{d}";')
        out.append("")
        for e in f["enums"]:
            out += self._enum_text(self.enums[e], "")
        for m in f["messages"]:
            out += self._msg_text(self.msgs[m], "")
        return "\n".join(out)

    def _enum_text(self, e, ind):
        out = [f"{ind}enum {e['name']} {{"]
        for n, v in e["values"]:
            out.append(f"{ind}  {n} = {v};")
        out.append(f"{ind}}}")
        return out

    def _short(self, tn):
        return tn[len(self.pkg) + 1:] if tn.startswith(self.pkg + ".") else tn

    def _msg_text(self, m, ind):
        out = [f"{ind}message {m['name']} {{"]
        for full, nm in self.msgs.items():
            if full.startswith(m["full"] + ".") and "." not in full[len(m["full"]) + 1:]:
                out += self._msg_text(nm, ind + "  ")
        for full, e in self.enums.items():
            if full.startswith(m["full"] + ".") and "." not in full[len(m["full"]) + 1:]:
                out += self._enum_text(e, ind + "  ")
        done_oneof = set()
        for f in m["fields"]:
            t = TYPE_NAMES[f["type"]]
            if t in ("message", "enum"):
                t = self._short(f["type_name"])
            if f["oneof"] is not None and not f["proto3_optional"]:
                oi = f["oneof"]
                if oi in done_oneof:
                    continue
                done_oneof.add(oi)
                out.append(f"{ind}  oneof {m['oneofs'][oi]} {{")
                for g in m["fields"]:
                    if g["oneof"] == oi:
                        gt = TYPE_NAMES[g["type"]]
                        if gt in ("message", "enum"):
                            gt = self._short(g["type_name"])
                        out.append(f"{ind}    {gt} {g['name']} = {g['number']};")
                out.append(f"{ind}  }}")
                continue
            lab = "repeated " if f["label"] == 3 else ("optional " if f["proto3_optional"] else "")
            out.append(f"{ind}  {lab}{t} {f['name']} = {f['number']};")
        out.append(f"{ind}}}")
        return out

    def decode(self, msg_full, b):
        m = self.msgs[msg_full]
        byno = {f["number"]: f for f in m["fields"]}
        out = {}
        for fn, wt, v in iter_fields(b):
            f = byno.get(fn)
            if f is None:
                out.setdefault(f"#{fn}", []).append(v.hex() if isinstance(v, (bytes, bytearray)) else v)
                continue
            vals = []
            t = f["type"]
            if wt == 2 and t not in (9, 11, 12):   # packed scalars
                j = 0
                while j < len(v):
                    x, j = read_varint(v, j)
                    vals.append(x)
            else:
                vals.append(v)
            conv = []
            for x in vals:
                if t == 11:
                    conv.append(self.decode(f["type_name"], x))
                elif t == 9:
                    conv.append(_s(x))
                elif t == 12:
                    conv.append(base64.b64encode(bytes(x)).decode())
                elif t == 14:
                    conv.append(self.enum_name(f["type_name"], x))
                elif t == 8:
                    conv.append(bool(x))
                elif t in (5, 3):
                    conv.append(x - (1 << 64) if x >= 1 << 63 else x)
                elif t == 17 or t == 18:
                    conv.append((x >> 1) ^ -(x & 1))
                elif t == 2:
                    conv.append(struct.unpack("<f", x)[0])
                elif t == 1:
                    conv.append(struct.unpack("<d", x)[0])
                elif t in (7, 15):
                    conv.append(struct.unpack("<i" if t == 15 else "<I", x)[0])
                elif t in (6, 16):
                    conv.append(struct.unpack("<q" if t == 16 else "<Q", x)[0])
                else:
                    conv.append(x)
            if f["label"] == 3:
                out.setdefault(f["json"], []).extend(conv)
            else:
                out[f["json"]] = conv[-1]
        return out

    def encode(self, msg_full, d):
        m = self.msgs[msg_full]
        byjson = {f["json"]: f for f in m["fields"]}
        out = bytearray()
        for k, val in d.items():
            f = byjson[k]
            vals = val if f["label"] == 3 else [val]
            for x in vals:
                t = f["type"]
                if t == 11:
                    sub = self.encode(f["type_name"], x)
                    out += enc_key(f["number"], 2) + enc_varint(len(sub)) + sub
                elif t in (9, 12):
                    bb = x.encode() if isinstance(x, str) else bytes(x)
                    out += enc_key(f["number"], 2) + enc_varint(len(bb)) + bb
                elif t == 14:
                    num = self.enum_value(f["type_name"], x) if isinstance(x, str) else x
                    out += enc_key(f["number"], 0) + enc_varint(num)
                elif t in (3, 4, 5, 13, 8):
                    out += enc_key(f["number"], 0) + enc_varint(int(x))
                elif t == 1:
                    out += enc_key(f["number"], 1) + struct.pack("<d", float(x))
                elif t == 2:
                    out += enc_key(f["number"], 5) + struct.pack("<f", float(x))
                else:
                    raise NotImplementedError(TYPE_NAMES[t])
        return bytes(out)


HDR_SMALL, HDR_CHUNK, HDR_COMP = b"FDG_PROTOCOL\n", b"FDG_CHUNK\n", b"FDG_COMPRESSED\n"


def frame(payload):
    """Frame exactly like Space Station's Electron client (cu.sendMessage)."""
    n = len(payload)
    if n >= 5 * 1024 * 1024:
        z = gzip.compress(payload)
        return HDR_COMP + struct.pack("<i", len(z)) + z
    if n >= 1024 * 1024:
        chunks = [payload[i:i + 65536] for i in range(0, n, 65536)]
        out = HDR_CHUNK + struct.pack("<ii", n, len(chunks))
        for c in chunks:
            out += struct.pack("<i", len(c)) + c
        return out
    return HDR_SMALL + struct.pack("<i", n) + payload


def unframe(buf):
    """Split a byte stream into (kind, payload) tuples; returns (msgs, rest)."""
    msgs = []
    while True:
        nl = buf.find(b"\n")
        if nl < 0:
            break
        hdr = buf[:nl].strip()
        if not hdr.startswith(b"FDG_"):
            k = buf.find(b"FDG_", 1)
            buf = buf[k:] if k >= 0 else b""
            continue
        body = buf[nl + 1:]
        if hdr.startswith(b"FDG_COMPRESSED"):
            if len(body) < 4:
                break
            (n,) = struct.unpack_from("<i", body)
            if len(body) < 4 + n:
                break
            msgs.append(("compressed", gzip.decompress(body[4:4 + n])))
            buf = body[4 + n:]
        elif hdr.startswith(b"FDG_CHUNK"):
            if len(body) < 8:
                break
            total, cnt = struct.unpack_from("<ii", body)
            o, parts, ok = 8, [], True
            for _ in range(cnt):
                if len(body) < o + 4:
                    ok = False
                    break
                (cl,) = struct.unpack_from("<i", body, o)
                o += 4
                if len(body) < o + cl:
                    ok = False
                    break
                parts.append(body[o:o + cl])
                o += cl
            if not ok:
                break
            p = b"".join(parts)
            if len(p) != total:
                print(f"warning: chunked total {total} != {len(p)}", file=sys.stderr)
            msgs.append(("chunked", p))
            buf = body[o:]
        else:
            if len(body) < 4:
                break
            (n,) = struct.unpack_from("<i", body)
            if len(body) < 4 + n:
                break
            msgs.append(("small", body[4:4 + n]))
            buf = body[4 + n:]
    return msgs, buf


def cmd_proto(a):
    s = Schema(a.asar)
    for f in s.files:
        if a.files and f["name"] not in a.files:
            continue
        print(s.proto_text(f))
        print()


def top_messages(s):
    return s.pkg + ".IpcCommand", s.pkg + ".IpcResult"


def cmd_decode(a):
    s = Schema(a.asar)
    req, rsp = top_messages(s)
    data = open(a.file, "rb").read()
    msgs, rest = unframe(data)
    for kind, p in msgs:
        full = rsp if a.response else req
        print(json.dumps({"framing": kind, "payloadBytes": len(p), "message": full.split(".")[-1],
                          "body": s.decode(full, p)}, indent=a.indent, ensure_ascii=False))
    if rest:
        print(f"// {len(rest)} trailing bytes", file=sys.stderr)


def summarize(obj, maxlist=4):
    if isinstance(obj, dict):
        return {k: summarize(v, maxlist) for k, v in obj.items()}
    if isinstance(obj, list):
        if len(obj) > maxlist:
            return [summarize(x, maxlist) for x in obj[:maxlist]] + [f"... {len(obj) - maxlist} more"]
        return [summarize(x, maxlist) for x in obj]
    return obj


class Pipe:
    """Win32 named-pipe client that polls with PeekNamedPipe, so one thread can read and write a synchronous handle."""
    ERROR_PIPE_BUSY = 231
    ERROR_FILE_NOT_FOUND = 2
    OPEN_EXISTING = 3

    def __init__(self):
        import ctypes
        from ctypes import wintypes
        self.ct, self.wt = ctypes, wintypes
        k = ctypes.WinDLL("kernel32", use_last_error=True)
        k.CreateFileW.restype = wintypes.HANDLE
        k.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                                  wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        k.PeekNamedPipe.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
                                    ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD),
                                    ctypes.POINTER(wintypes.DWORD)]
        k.ReadFile.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
                               ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
        k.WriteFile.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD,
                                ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
        k.CloseHandle.argtypes = [wintypes.HANDLE]
        self.k = k
        self.h = None

    def open(self, name=PIPE):
        GENERIC_RW = 0x80000000 | 0x40000000
        h = self.k.CreateFileW(name, GENERIC_RW, 0, None, self.OPEN_EXISTING, 0, None)
        if h is None or h == self.wt.HANDLE(-1).value:
            return self.ct.get_last_error()
        self.h = h
        return 0

    def available(self):
        n = self.wt.DWORD(0)
        if not self.k.PeekNamedPipe(self.h, None, 0, None, self.ct.byref(n), None):
            raise OSError(self.ct.get_last_error(), "PeekNamedPipe failed (pipe closed?)")
        return n.value

    def read(self, n):
        buf = self.ct.create_string_buffer(n)
        got = self.wt.DWORD(0)
        if not self.k.ReadFile(self.h, buf, n, self.ct.byref(got), None):
            raise OSError(self.ct.get_last_error(), "ReadFile failed")
        return buf.raw[:got.value]

    def write(self, data):
        done = self.wt.DWORD(0)
        if not self.k.WriteFile(self.h, data, len(data), self.ct.byref(done), None):
            raise OSError(self.ct.get_last_error(), "WriteFile failed")
        return done.value

    def close(self):
        if self.h:
            self.k.CloseHandle(self.h)
            self.h = None


def cmd_probe(a):
    s = Schema(a.asar)
    req, rsp = top_messages(s)
    t0 = time.time()

    def log(*x):
        print(f"[{time.time() - t0:6.2f}s]", *x, flush=True)

    pa = Pipe()
    err = pa.open()
    if err:
        log(f"connect A failed: winerror {err}" + (" (ERROR_PIPE_BUSY: another client holds the pipe)"
                                                   if err == Pipe.ERROR_PIPE_BUSY else ""))
        return 1
    log("client A connected to", PIPE)

    pb = Pipe()
    err = pb.open()
    if err:
        log(f"client B (concurrent) refused: winerror {err}"
            + (" = ERROR_PIPE_BUSY -> single client at a time" if err == Pipe.ERROR_PIPE_BUSY else ""))
    else:
        log("client B (concurrent) ALSO connected -> multiple instances; closing B")
        pb.close()

    buf = b""
    received = []

    def pump(seconds):
        nonlocal buf
        end = time.time() + seconds
        while time.time() < end:
            n = pa.available()
            if n:
                buf += pa.read(n)
                msgs, buf = unframe(buf)
                for kind, p in msgs:
                    d = s.decode(rsp, p)
                    received.append(d)
                    log(f"<- [{kind}, {len(p)} B]", json.dumps(summarize(d), ensure_ascii=False))
            else:
                time.sleep(0.02)

    def send(obj):
        p = s.encode(req, obj)
        pa.write(frame(p))
        log("->", json.dumps(obj, ensure_ascii=False), f"({len(p)} B payload)")

    # Read-only queries only: never Init, UpdateConfig, Save*, Apply*, Enable* or Set*.
    pump(0.3)   # catches anything the service pushes on connect
    send({"cmdId": "IpcCommandEnum_GetConnectedDevices", "category": "Category_Charger"})
    pump(a.seconds)
    if a.detail:
        for d in list(received):
            if d.get("cmdId") != "IpcCommandEnum_GetConnectedDevices":
                continue
            for dev in d.get("getConnectedDevices", {}).get("connectedDevices", []):
                if dev.get("category") == "Category_Charger":
                    send({"cmdId": "IpcCommandEnum_GetDeviceDetailInfo", "category": "Category_Charger",
                          "getDeviceDetailInfoParam": {"deviceCode": dev.get("deviceCode", ""),
                                                       "uid": dev.get("uid", "")}})
        pump(a.seconds)
    pa.close()
    log("client A closed;", len(received), "messages received")

    for i in range(20):
        pc = Pipe()
        err = pc.open()
        if not err:
            log(f"reconnect check: pipe available again after ~{(i + 1) * 100} ms; closing")
            pc.close()
            break
        time.sleep(0.1)
    else:
        log(f"reconnect check: pipe still unavailable after 2 s (last winerror {err})")

    if a.dump:
        with open(a.dump, "w", encoding="utf-8") as f:
            json.dump(received, f, indent=1, ensure_ascii=False)
    return 0


def cmd_hold(a):
    p = Pipe()
    err = p.open()
    if err:
        print(f"connect failed: winerror {err}", flush=True)
        return 1
    print(f"holding {PIPE} for {a.seconds} s (nothing is sent)", flush=True)
    end = time.time() + a.seconds
    buf = b""
    while time.time() < end:
        n = p.available()
        if n:
            buf += p.read(n)
        time.sleep(0.05)
    p.close()
    print(f"released; {len(buf)} unsolicited bytes received while holding", flush=True)
    return 0


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(
        description="Flydigi Space Station IPC helper. Run it as: python -I tools/fdg_ipc.py ...",
        epilog="The protobuf schema is not part of this repo: it is read at runtime from the descriptors "
               "embedded in the installed Space Station (app.asar -> .vite/build/main.js).")
    ap.add_argument("--asar", default=DEFAULT_ASAR, help="Space Station's app.asar to read the schema from")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("proto", help="print the embedded .proto files")
    p.add_argument("files", nargs="*", help="only these .proto files")
    p = sub.add_parser("decode", help="decode framed pipe bytes to JSON")
    p.add_argument("file")
    p.add_argument("--response", action="store_true", help="decode as IpcResult (service -> client)")
    p.add_argument("--request", action="store_true", help="decode as IpcCommand (client -> service, default)")
    p.add_argument("--indent", type=int, default=None)
    p = sub.add_parser("probe", help=f"live read-only probe of {PIPE}: sends only GetConnectedDevices and, "
                                     "with --detail, GetDeviceDetailInfo")
    p.add_argument("--seconds", type=float, default=4.0)
    p.add_argument("--detail", action="store_true")
    p.add_argument("--dump", help="write every received message to this JSON file")
    p = sub.add_parser("hold", help="occupy the pipe without sending anything, as Space Station's open "
                                    "window does")
    p.add_argument("--seconds", type=float, default=10.0)
    a = ap.parse_args()
    return {"proto": cmd_proto, "decode": cmd_decode, "probe": cmd_probe, "hold": cmd_hold}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main() or 0)
