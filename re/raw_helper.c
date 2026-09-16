// raw_helper: Windows Raw Input -> UDP displacement faucet for the NativeMouse
// TOTK mod (branch experiment/raw-input-bridge, mod ri1+).
//
// Forwards relative mouse motion as {magic, seq, dx, dy} datagrams to
// 127.0.0.1:51987, but ONLY while eden.exe owns the foreground window.
// No cursor warping: cursor position is never read, only relative deltas,
// so there is no focus-return jump by construction. The visible cursor is
// confined to the Eden client area while forwarding (ClipCursor) and
// released the moment focus is lost. No Zelda/camera logic lives here.
//
// Build (MSVC x64):  cl /O2 /W3 /Fe:raw_helper.exe raw_helper.c user32.lib ws2_32.lib
// Run: raw_helper.exe   (console, Ctrl+C quits)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <psapi.h>
#include <stdio.h>
#include <stdint.h>

#define PROBE_MAGIC 0x4D4E5657u
#define PROBE_PORT 51987

static SOCKET g_sock = INVALID_SOCKET;
static struct sockaddr_in g_dst;
static uint32_t g_seq = 0;
static uint64_t g_sent = 0;
static HWND g_eden_hwnd = NULL;   // cached Eden window; revalidated cheaply
static int g_forwarding = 0;

#pragma pack(push, 1)
typedef struct { uint32_t magic; uint32_t seq; int32_t dx; int32_t dy; } Packet;
#pragma pack(pop)

static int ProcessIsEden(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return 0;
    }
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (h == NULL) {
        return 0;
    }
    WCHAR name[MAX_PATH] = { 0 };
    int ok = 0;
    if (GetModuleBaseNameW(h, NULL, name, MAX_PATH)) {
        _wcslwr_s(name, MAX_PATH);
        ok = (wcscmp(name, L"eden.exe") == 0);
    }
    CloseHandle(h);
    return ok;
}

// Eden client window iff it is foreground, else NULL. The cached handle
// avoids a process lookup on every mouse event; any fg change re-checks.
static HWND EdenForeground(void) {
    HWND fg = GetForegroundWindow();
    if (fg == NULL) {
        g_eden_hwnd = NULL;
        return NULL;
    }
    if (fg == g_eden_hwnd && IsWindow(fg)) {
        return fg;
    }
    g_eden_hwnd = NULL;
    if (ProcessIsEden(fg)) {
        g_eden_hwnd = fg;
        return fg;
    }
    return NULL;
}

static void RefreshClip(HWND eden) {
    RECT r;
    if (!GetClientRect(eden, &r)) {
        return;
    }
    POINT tl = { r.left, r.top }, br = { r.right, r.bottom };
    ClientToScreen(eden, &tl);
    ClientToScreen(eden, &br);
    RECT clip = { tl.x, tl.y, br.x, br.y };
    ClipCursor(&clip);
}

static void SetForwarding(int on, HWND eden) {
    if (on == g_forwarding) {
        return;
    }
    g_forwarding = on;
    if (on) {
        RefreshClip(eden);
        printf("raw_helper: forwarding (eden focused)\n");
    } else {
        ClipCursor(NULL);
        printf("raw_helper: idle (eden not focused)\n");
    }
    fflush(stdout);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_INPUT) {
        HWND eden = EdenForeground();
        SetForwarding(eden != NULL, eden);
        if (eden != NULL) {
            RefreshClip(eden);   // follows window moves/resizes
            UINT size = 0;
            GetRawInputData((HRAWINPUT)l, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
            if (size > 0 && size <= sizeof(BYTE) * 1024) {
                BYTE buf[1024];
                if (GetRawInputData((HRAWINPUT)l, RID_INPUT, buf, &size,
                                    sizeof(RAWINPUTHEADER)) == size) {
                    RAWINPUT *ri = (RAWINPUT *)buf;
                    if (ri->header.dwType == RIM_TYPEMOUSE &&
                        !(ri->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                        LONG dx = ri->data.mouse.lLastX;
                        LONG dy = ri->data.mouse.lLastY;
                        if (dx != 0 || dy != 0) {
                            Packet p;
                            p.magic = PROBE_MAGIC;
                            p.seq = ++g_seq;
                            p.dx = (int32_t)dx;
                            p.dy = (int32_t)dy;
                            sendto(g_sock, (const char *)&p, sizeof(p), 0,
                                   (const struct sockaddr *)&g_dst, sizeof(g_dst));
                            g_sent++;
                        }
                    }
                }
            }
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static BOOL WINAPI CtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
        ClipCursor(NULL);
        if (g_sock != INVALID_SOCKET) {
            closesocket(g_sock);
        }
        WSACleanup();
        printf("\nraw_helper: quit, %llu packets sent\n",
               (unsigned long long)g_sent);
        ExitProcess(0);
    }
    return FALSE;
}

int main(void) {
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
        printf("raw_helper: WSAStartup failed\n");
        return 1;
    }
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) {
        printf("raw_helper: socket failed\n");
        return 1;
    }
    memset(&g_dst, 0, sizeof(g_dst));
    g_dst.sin_family = AF_INET;
    g_dst.sin_port = htons(PROBE_PORT);
    g_dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"RawHelperMsg";
    if (!RegisterClassW(&wc)) {
        printf("raw_helper: RegisterClass failed\n");
        return 1;
    }
    HWND msg = CreateWindowExW(0, wc.lpszClassName, L"raw_helper",
                               0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                               wc.hInstance, NULL);
    if (msg == NULL) {
        printf("raw_helper: CreateWindow failed\n");
        return 1;
    }

    RAWINPUTDEVICE rid;
    rid.usUsagePage = 1;
    rid.usUsage = 2;   // mouse
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = msg;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        printf("raw_helper: RegisterRawInputDevices failed (%lu)\n",
               (unsigned long)GetLastError());
        return 1;
    }

    printf("raw_helper: listening for raw mouse, target 127.0.0.1:%d\n", PROBE_PORT);
    printf("raw_helper: focus Eden to forward (Ctrl+C quits)\n");
    fflush(stdout);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
