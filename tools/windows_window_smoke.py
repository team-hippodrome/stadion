"""Exercise the Windows game window and retain desktop captures of its transitions."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

from PIL import ImageGrab
from runner_platform import BACKENDS, actual_backend, backend_home


def eventually(check, seconds=8):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(.04)
    raise RuntimeError("Window transition did not complete")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Windows is required")
    binary, rom, out = args.bin.resolve(), args.rom.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    user = ctypes.windll.user32
    user.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    user.GetForegroundWindow.restype = wintypes.HWND
    enum_window = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    enum_monitor = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMONITOR, wintypes.HDC,
                                   ctypes.POINTER(wintypes.RECT), wintypes.LPARAM)
    monitors = []

    @enum_monitor
    def monitor(handle, _dc, bounds, _data):
        r = bounds.contents
        monitors.append((handle, (r.left, r.top, r.right, r.bottom)))
        return True

    user.EnumDisplayMonitors(None, None, monitor, 0)
    user.MonitorFromWindow.restype = wintypes.HMONITOR

    def find_window(pid):
        found = []

        @enum_window
        def window(hwnd, _):
            owner = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == pid and user.IsWindowVisible(hwnd):
                found.append(hwnd)
            return True

        user.EnumWindows(window, 0)
        return found[0] if found else None

    def client(hwnd):
        r, origin = wintypes.RECT(), wintypes.POINT()
        assert user.GetClientRect(hwnd, ctypes.byref(r))
        assert user.ClientToScreen(hwnd, ctypes.byref(origin))
        return origin.x, origin.y, origin.x + r.right, origin.y + r.bottom

    def size(hwnd):
        x, y, right, bottom = client(hwnd)
        return right - x, bottom - y

    def key(vk):
        scan = user.MapVirtualKeyW(vk, 0)
        user.keybd_event(vk, scan, 0, 0)
        time.sleep(.05)
        user.keybd_event(vk, scan, 2, 0)

    def alt_tab():
        scan = user.MapVirtualKeyW(0x12, 0)
        user.keybd_event(0x12, scan, 0, 0)
        try:
            key(0x09)
        finally:
            user.keybd_event(0x12, scan, 2, 0)

    results = {}
    for backend in BACKENDS:
        run = Path(tempfile.mkdtemp(prefix=f"{backend}-", dir=out))
        env = {k: v for k, v in os.environ.items() if not k.startswith("PORT_")}
        backend_home(env, str(run), backend)
        env["PORT_TRACE_SHADERS"] = "1"
        with (run / "trace.log").open("w", encoding="utf-8") as log:
            process = subprocess.Popen([str(binary), str(rom)], cwd=run, env=env,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                hwnd = eventually(lambda: find_window(process.pid))
                eventually(lambda: "startup warm-up complete" in (run / "trace.log").read_text(errors="replace"), 30)
                user.SetForegroundWindow(hwnd)
                eventually(lambda: user.GetForegroundWindow() == hwnd)
                time.sleep(.5)
                before = size(hwnd)
                key(0x7A)  # F11
                eventually(lambda: size(hwnd) != before)
                ImageGrab.grab(bbox=client(hwnd)).save(run / "fullscreen.png")
                key(0x7A)
                eventually(lambda: size(hwnd) == before)
                ImageGrab.grab(bbox=client(hwnd)).save(run / "windowed.png")
                alt_tab()
                eventually(lambda: user.GetForegroundWindow() != hwnd)
                time.sleep(.25)
                alt_tab()
                eventually(lambda: user.GetForegroundWindow() == hwnd)
                ImageGrab.grab(bbox=client(hwnd)).save(run / "refocused.png")
                assert user.SetWindowPos(hwnd, None, 0, 0, 840, 650, 0x16)
                eventually(lambda: size(hwnd) != before)
                ImageGrab.grab(bbox=client(hwnd)).save(run / "resized.png")
                current = user.MonitorFromWindow(hwnd, 2)
                start_dpi = user.GetDpiForWindow(hwnd)
                moved = "skipped (one display)"
                for handle, (left, top, _, _) in monitors:
                    if handle != current:
                        assert user.SetWindowPos(hwnd, None, left + 30, top + 30, 0, 0, 0x15)
                        eventually(lambda: user.MonitorFromWindow(hwnd, 2) == handle)
                        moved = f"{start_dpi} to {user.GetDpiForWindow(hwnd)} DPI"
                        ImageGrab.grab(bbox=client(hwnd)).save(run / "moved.png")
                        break
                user.PostMessageW(hwnd, 0x10, 0, 0)  # WM_CLOSE
                assert process.wait(timeout=10) == 0
                assert actual_backend(run) == BACKENDS[backend][0]
                results[backend] = {"fullscreen": "passed", "alt_tab": "passed",
                                    "resize": "passed", "monitor_move": moved, "captures": str(run)}
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
        print(backend, results[backend], flush=True)
    (out / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
