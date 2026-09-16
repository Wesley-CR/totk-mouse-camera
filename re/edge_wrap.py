# Edge-wrap helper for the NativeMouse experiment (see mod nm27).
#
# EXPERIMENTAL, isolated: stdlib-only, no install, no Eden changes.
#
# Problem: Eden's emulated-mouse feed derives deltas from the Windows cursor
# position, so motion stops at the window edge. This script watches the
# cursor while the Eden window is foreground and teleports it back to the
# client-area centre when it comes within MARGIN px of an edge. The teleport
# shows up in the guest as one poisoned HID sample with a huge delta; the
# mod (nm27+) spots it by its absolute (x,y) jumping to the centre
# (Eden scales the window to 1280x720) and discards that sample's dx/dy.
#
# Safety: starts DISARMED, only ever acts when eden.exe owns the foreground
# window, and only when the cursor is already inside its client area.
# Toggle with F9. Quit with Ctrl+C.

import ctypes
import time
from ctypes import wintypes

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi
winmm = ctypes.windll.winmm

VK_F9 = 0x78
MARGIN = 60
POLL_S = 0.002
CENTER_X, CENTER_Y = 640, 360  # must match the mod's expectation


class RECT(ctypes.Structure):
    _fields_ = [("left", wintypes.LONG), ("top", wintypes.LONG),
                ("right", wintypes.LONG), ("bottom", wintypes.LONG)]


def fg_is_eden():
    hwnd = user32.GetForegroundWindow()
    if not hwnd:
        return None
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    proc = kernel32.OpenProcess(0x0410, False, pid.value)
    if not proc:
        return None
    try:
        name = ctypes.create_unicode_buffer(260)
        if psapi.GetModuleBaseNameW(proc, None, name, 260):
            if name.value.lower() == "eden.exe":
                return hwnd
    finally:
        kernel32.CloseHandle(proc)
    return None


def client_center(hwnd):
    r = RECT()
    if not user32.GetClientRect(hwnd, ctypes.byref(r)):
        return None
    pt = wintypes.POINT(r.left + (r.right - r.left) // 2,
                        r.top + (r.bottom - r.top) // 2)
    if not user32.ClientToScreen(hwnd, ctypes.byref(pt)):
        return None
    return pt.x, pt.y, r.right - r.left, r.bottom - r.top


def main():
    winmm.timeBeginPeriod(1)
    armed = False
    f9_down = False
    wraps = 0
    print("edge_wrap: DISARMED. Focus the Eden window and press F9 to arm.")
    try:
        while True:
            down = bool(user32.GetAsyncKeyState(VK_F9) & 0x8000)
            if down and not f9_down:
                armed = not armed
                print("edge_wrap: %s" % ("ARMED" if armed else "DISARMED"))
            f9_down = down

            if armed:
                hwnd = fg_is_eden()
                if hwnd:
                    geo = client_center(hwnd)
                    if geo:
                        cx, cy, w, h = geo
                        pt = wintypes.POINT()
                        user32.GetCursorPos(ctypes.byref(pt))
                        # Cursor in client coords:
                        lx, ly = pt.x - (cx - w // 2), pt.y - (cy - h // 2)
                        if 0 <= lx < w and 0 <= ly < h:
                            if (lx < MARGIN or lx >= w - MARGIN or
                                    ly < MARGIN or ly >= h - MARGIN):
                                user32.SetCursorPos(cx, cy)
                                wraps += 1
                                if wraps % 25 == 1:
                                    print("edge_wrap: %d recenters" % wraps)
            time.sleep(POLL_S)
    except KeyboardInterrupt:
        print("\nedge_wrap: quit, %d recenters total" % wraps)
    finally:
        winmm.timeEndPeriod(1)


if __name__ == "__main__":
    main()
