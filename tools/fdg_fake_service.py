"""Fake SpaceStationService on its own pipe, for testing FlyGlow's upload path without the dock."""
import argparse
import ctypes
import os
import sys
import time
from ctypes import wintypes

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fdg_ipc as f   # noqa: E402

k = ctypes.WinDLL("kernel32", use_last_error=True)
k.CreateNamedPipeW.restype = wintypes.HANDLE
k.CreateNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                               wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID]
k.ConnectNamedPipe.argtypes = [wintypes.HANDLE, wintypes.LPVOID]
k.DisconnectNamedPipe.argtypes = [wintypes.HANDLE]
k.ReadFile.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
                       wintypes.LPVOID]
k.WriteFile.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
                        wintypes.LPVOID]
k.CloseHandle.argtypes = [wintypes.HANDLE]

PIPE_ACCESS_DUPLEX = 3
PIPE_NOWAIT = 1           # non-blocking byte pipe, so one thread can poll everything
ERROR_PIPE_CONNECTED, ERROR_PIPE_LISTENING, ERROR_NO_DATA, ERROR_BROKEN_PIPE = 535, 536, 232, 109
ERROR_PIPE_NOT_CONNECTED = 233
INVALID = wintypes.HANDLE(-1).value

DOCK = {"productName": "Flydigi CD2", "category": "Category_Charger", "connectType": "ConnectType_Wired",
        "deviceCode": "cd2", "uid": "fa4e0000000000000000000001", "isConnected": True,
        "firmwareVersion": "9.9.9.9", "vendorId": 0x37D7, "productId": 0x6001}
PAD = {"productName": "Fake pad", "category": "Category_Controller", "deviceCode": "f5",
       "uid": "fa4e0000000000000000000002", "isConnected": True}


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(
        description="Fake SpaceStationService: serves one client at a time with the real framing and schema,\n"
                    "answers GetConnectedDevices with one CD2 dock and UpdateConfig as --update says.",
        epilog="example:\n"
               "  python -I tools/fdg_fake_service.py --pipe flyglow-fake.sock --update ok\n"
               "  set FLYGLOW_FDG_PIPE=flyglow-fake.sock\n"
               "  b1\\ipc_probe.exe --test-upload 1",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pipe", default="flyglow-fake.sock")
    ap.add_argument("--update", default="ok", choices=["ok", "fail", "silent", "drop", "progress"],
                    help="ok: success after --ms-per-frame per frame; fail: state=false, code 7; silent: never "
                         "answers; drop: closes the pipe; progress: progress 50 and 100, then success")
    ap.add_argument("--ms-per-frame", type=float, default=85.0)
    ap.add_argument("--framing", default="small", choices=["small", "chunked", "compressed"],
                    help="framing for the GetConnectedDevices reply")
    ap.add_argument("--push", action="store_true", help="push unsolicited results on connect")
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--asar", default=f.DEFAULT_ASAR, help="Space Station's app.asar to read the schema from")
    a = ap.parse_args()
    if a.pipe.lower() == "fcs.sock":
        print("refusing to impersonate the real pipe name")
        return 2

    s = f.Schema(a.asar)
    req, rsp = f.top_messages(s)
    name = "\\\\.\\pipe\\" + a.pipe
    t0 = time.time()
    end = t0 + a.seconds

    def log(*x):
        print(f"[fake {time.time() - t0:6.2f}s]", *x, flush=True)

    def frame_as(p, how):
        if how == "chunked":
            return f.frame(p) if len(p) >= 1 << 20 else (
                f.HDR_CHUNK + f.struct.pack("<ii", len(p), 1) + f.struct.pack("<i", len(p)) + p)
        if how == "compressed":
            z = f.gzip.compress(p)
            return f.HDR_COMP + f.struct.pack("<i", len(z)) + z
        return f.frame(p)

    log(f"listening on {name}, UpdateConfig -> {a.update}")
    while time.time() < end:
        h = k.CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX, PIPE_NOWAIT, 1, 65536, 65536, 0, None)
        if h == INVALID or h is None:
            log("CreateNamedPipe failed", ctypes.get_last_error())
            return 1
        connected = False
        while time.time() < end:
            ok = k.ConnectNamedPipe(h, None)
            err = ctypes.get_last_error()
            if ok or err == ERROR_PIPE_CONNECTED:
                connected = True
                break
            if err not in (ERROR_PIPE_LISTENING,):
                break
            time.sleep(0.02)
        if not connected:
            k.CloseHandle(h)
            continue
        log("client connected")

        def write(data):
            done = wintypes.DWORD(0)
            pos = 0
            while pos < len(data):
                chunk = data[pos:pos + 65536]
                if not k.WriteFile(h, chunk, len(chunk), ctypes.byref(done), None):
                    raise OSError(ctypes.get_last_error(), "WriteFile")
                pos += done.value
                if done.value == 0:
                    time.sleep(0.005)

        def reply(obj, how="small"):
            write(frame_as(s.encode(rsp, obj), how))
            log("<-", obj.get("cmdId"), "state", obj.get("state", False))

        if a.push:
            reply({"cmdId": "IpcCommandEnum_GetDeviceDetailInfo", "state": True, "category": "Category_Charger",
                   "getDeviceDetailInfo": {"chargerInfo": {"parent": DOCK, "isControllerConnected": True}}})
            reply({"cmdId": "IpcCommandEnum_DeviceConnectionChanged", "state": True,
                   "deviceConnectionChanged": {"deviceInfo": PAD}})
        buf = b""
        rd = ctypes.create_string_buffer(65536)
        got = wintypes.DWORD(0)
        alive = True
        while alive and time.time() < end:
            if k.ReadFile(h, rd, 65536, ctypes.byref(got), None) and got.value:
                buf += rd.raw[:got.value]
            else:
                err = ctypes.get_last_error()
                if err in (ERROR_BROKEN_PIPE, ERROR_PIPE_NOT_CONNECTED):
                    log("client disconnected")
                    break
                time.sleep(0.01)
                continue
            msgs, buf = f.unframe(buf)
            for kind, p in msgs:
                cmd = s.decode(req, p)
                cid = cmd.get("cmdId")
                if cid == "IpcCommandEnum_GetConnectedDevices":
                    log("->", cid, f"({kind})")
                    reply({"cmdId": cid, "state": True,
                           "getConnectedDevices": {"connectedDevices": [PAD, DOCK]}}, a.framing)
                elif cid == "IpcCommandEnum_UpdateConfig":
                    c = cmd.get("updateConfigParam", {}).get("chargerLedConfig", {})
                    frames = c.get("frames", [])
                    log("->", cid, f"({kind}, {len(p)} B): uid", cmd["updateConfigParam"].get("uid"),
                        "mode", c.get("mode"), "period", c.get("period"), "brightness", c.get("brightness"),
                        "frames", len(frames), "colors/frame", sorted({len(x.get("colors", [])) for x in frames}))
                    if a.update == "drop":
                        log("dropping the connection")
                        alive = False
                        break
                    if a.update == "silent":
                        continue
                    time.sleep(a.ms_per_frame * len(frames) / 1000.0)
                    if a.update == "progress":
                        for pr in (50.0, 100.0):
                            reply({"cmdId": "IpcCommandEnum_UploadProgressChanged", "state": True,
                                   "ProgressChangedResult": {"progress": pr}})
                    if a.update == "fail":
                        reply({"cmdId": cid, "state": False, "code": 7, "category": "Category_Charger"})
                    else:
                        reply({"cmdId": cid, "state": True, "category": "Category_Charger"})
                else:
                    log("->", cid, "(answered with an empty result)")
                    reply({"cmdId": cid, "state": True})
        k.DisconnectNamedPipe(h)
        k.CloseHandle(h)
    log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
