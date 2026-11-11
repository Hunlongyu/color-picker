/* Render a synthetic desktop on an isolated Win32 desktop. Cursor and monitor
   input are simulated so these checks never move the user's real mouse. */
#include "../src/app.h"
#include <cui/cui.h>
#include <cui/cui_render.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

App app;
static POINT cursor;
static COLORREF accepted;
static int accepts, failures;
static BOOL WINAPI test_cursor(LPPOINT p) { *p = cursor; return TRUE; }
static BOOL WINAPI test_move(int x, int y) { cursor = (POINT){x, y}; return TRUE; }
static HMONITOR WINAPI test_monitor(POINT p, DWORD flags) {
    (void)flags; return (HMONITOR)(INT_PTR)(p.x < 0 ? 1 : 2);
}
static BOOL WINAPI test_monitor_info(HMONITOR monitor, LPMONITORINFO info) {
    int left = (INT_PTR)monitor == 1 ? -1920 : 0;
    info->rcMonitor = info->rcWork = (RECT){left, -1080, left + 1920, 1080}; return TRUE;
}
#define GetCursorPos test_cursor
#define SetCursorPos test_move
#define MonitorFromPoint test_monitor
#define GetMonitorInfoW test_monitor_info
#include "../src/picker.c"
#undef GetCursorPos
#undef SetCursorPos
#undef MonitorFromPoint
#undef GetMonitorInfoW

void remember_color(COLORREF color) { accepted = color; accepts++; }
void color_text(COLORREF color, int format, wchar_t out[96]) {
    swprintf(out, 96, L"%d: #%02X%02X%02X", format, GetRValue(color), GetGValue(color), GetBValue(color));
}
static void check(bool ok, const char *name) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name); fflush(stdout);
    if (!ok) failures++;
}
static uint32_t pattern(int x, int y) {
    return ((uint32_t)(x & 255) << 16) | ((uint32_t)(y & 255) << 8) | (uint32_t)((x * 3 + y * 5) & 255);
}
static COLORREF expected(int x, int y) {
    uint32_t c = pattern(x, y); return RGB(c >> 16, c >> 8, c);
}
static void set_point(int x, int y) {
    cursor = (POINT){pick.desktop.left + x, pick.desktop.top + y};
    /* Deliberately stale event coordinates: drawing must use the current cursor. */
    SendMessageW(pick.window, WM_MOUSEMOVE, 0, MAKELPARAM(1, 1));
}
static void drain(void) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    GdiFlush();
}
static bool fixture(void) {
    HDC desktop = GetDC(NULL);
    bool created = create_buffers(desktop, 3840, 2160); ReleaseDC(NULL, desktop);
    if (!created) { release_capture(); return false; }
    pick.desktop = (RECT){-1920, -1080, 1920, 1080};
    for (int y = 0; y < pick.height; y++) for (int x = 0; x < pick.width; x++)
        ((uint32_t *)pick.pixels)[(size_t)y * pick.width + x] = pattern(x, y);
    BitBlt(pick.frame, 0, 0, pick.width, pick.height, pick.screen, 0, 0, SRCCOPY); GdiFlush();
    pick.point = (POINT){600, 400}; pick.cell = 10; pick.format = HEX;
    cursor = (POINT){pick.desktop.left + pick.point.x, pick.desktop.top + pick.point.y};
    pick.damage = lens(); pick.dirty = true;
    pick.window = CreateWindowExW(WS_EX_TOOLWINDOW, L"PickerFlowQA", L"", WS_POPUP,
                                  0, 0, pick.width, pick.height, NULL, NULL, app.instance, NULL);
    if (!pick.window) { release_capture(); return false; }
    ShowWindow(pick.window, SW_SHOWNOACTIVATE); pick.ready = true;
    UpdateWindow(pick.window); drain(); return true;
}
static void snapshot(const char *path) {
    RECT desktop = {0, 0, pick.width, pick.height}, crop = pick.damage;
    InflateRect(&crop, 12, 12); IntersectRect(&crop, &crop, &desktop);
    int width = crop.right - crop.left, height = crop.bottom - crop.top;
    BITMAPINFO info = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER), .biWidth = width,
        .biHeight = -height, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
    unsigned char *pixels; HDC dc = CreateCompatibleDC(pick.frame);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    HGDIOBJ old = SelectObject(dc, bitmap);
    BitBlt(dc, 0, 0, width, height, pick.frame, crop.left, crop.top, SRCCOPY); GdiFlush();
    for (int i = 0; i < width * height; i++) {
        unsigned char blue = pixels[i * 4]; pixels[i * 4] = pixels[i * 4 + 2];
        pixels[i * 4 + 2] = blue; pixels[i * 4 + 3] = 255;
    }
    check(cui_write_png(path, pixels, width, height, width * 4), path);
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
}
static void grid_check(void) {
    RECT r = lens(); bool correct = true;
    for (int j = 1; j < 8; j++) for (int i = 1; i < 8; i++) {
        if ((i - 4) * (i - 4) + (j - 4) * (j - 4) > 12) continue;
        int x = r.left + i * pick.cell + pick.cell / 2;
        int y = r.top + j * pick.cell + pick.cell / 2;
        if (x < 0 || y < 0 || x >= pick.width || y >= pick.height) continue;
        correct &= GetPixel(pick.frame, x, y) == expected(max(0, min(pick.width - 1, pick.point.x + i - 4)),
                                                        max(0, min(pick.height - 1, pick.point.y + j - 4)));
    }
    check(correct, "magnified cells preserve exact source pixels, orientation and edge clamping");
}
static int compare_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y);
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 2 || !SetCurrentDirectoryW(argv[1])) return 2;
    wchar_t name[64]; swprintf(name, 64, L"PickerFlow%lu", GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name, NULL, NULL, 0, GENERIC_ALL, NULL);
    if (!desktop || !SetThreadDesktop(desktop)) return 2;
    app.instance = GetModuleHandleW(NULL); app.dpi = 96;
    WNDCLASSW cls = {.hInstance = app.instance, .lpfnWndProc = picker_proc, .lpszClassName = L"PickerFlowQA"};
    RegisterClassW(&cls);
    if (!fixture()) return 3;
    grid_check(); snapshot("picker-grid.png");
    RECT previous = pick.damage;
    for (int i = 0; i < 5000; i++) set_point(80 + i % 3500, 80 + i % 1900);
    MSG frame; int queued = 0;
    while (PeekMessageW(&frame, pick.window, MSG_PICKER_FRAME, MSG_PICKER_FRAME, PM_REMOVE)) queued++;
    check(queued == 1 && pick.dirty, "5000 movement events queue only one frame");
    check(EqualRect(&previous, &pick.damage), "movement never erases or partially redraws the displayed lens");
    cursor = (POINT){pick.desktop.left + 1200, pick.desktop.top + 800};
    SendMessageW(pick.window, MSG_PICKER_FRAME, 0, 0);
    check(!pick.dirty && pick.point.x == 1200 && pick.point.y == 800, "frame follows newest cursor after queued input");
    check(GetPixel(pick.frame, 620, 420) == expected(620, 420), "previous lens area is restored without a trail");
    grid_check();
    SendMessageW(pick.window, WM_KEYDOWN, 'C', 0); drain();
    check(pick.format == HSL && pick.show_value, "C still switches format and shows its label");
    SendMessageW(pick.window, WM_KEYDOWN, 'C', (LPARAM)1 << 30);
    check(pick.format == HSL, "held C does not cycle repeatedly");
    snapshot("picker-value.png");
    POINT edge[] = {{0, 0}, {3839, 0}, {0, 2159}, {3839, 2159}, {1919, 500}, {1920, 500}};
    for (int i = 0; i < 6; i++) { set_point(edge[i].x, edge[i].y); drain(); grid_check(); }
    snapshot("picker-monitor-boundary.png");
    set_point(600, 400); drain(); SendMessageW(pick.window, WM_KEYDOWN, VK_RIGHT, 0); drain();
    check(pick.point.x == 601 && pick.point.y == 400, "arrow keys retain one-pixel positioning");
    pick.cell = 15; request_frame(); drain(); grid_check(); snapshot("picker-grid-150.png");
    pick.cell = 10;
    enum { FRAMES = 500 }; double times[FRAMES], total = 0;
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    for (int i = 0; i < FRAMES; i++) {
        LARGE_INTEGER start, end; QueryPerformanceCounter(&start);
        set_point(100 + (i * 197) % 3500, 100 + (i * 113) % 1800); drain();
        QueryPerformanceCounter(&end);
        times[i] = (double)(end.QuadPart - start.QuadPart) * 1000 / (double)frequency.QuadPart; total += times[i];
    }
    qsort(times, FRAMES, sizeof(double), compare_double);
    printf("4K synthetic desktop, %d frames: mean %.3f ms, p95 %.3f ms\n", FRAMES, total / FRAMES, times[FRAMES * 95 / 100]);
    set_point(321, 654);
    /* Confirm before the queued paint: selected color must use the current cursor. */
    SendMessageW(pick.window, WM_KEYDOWN, VK_RETURN, 0);
    check(accepts == 1 && accepted == expected(321, 654) && app.format == HSL && !picker_active(),
          "Enter confirms the latest pixel and chosen format before painting");
    check(!pick.pixels && !pick.screen && !pick.frame, "confirmation releases both buffers");
    if (!fixture()) return 3;
    SendMessageW(pick.window, WM_LBUTTONUP, 0, MAKELPARAM(123, 456));
    check(accepts == 2 && accepted == expected(123, 456), "mouse confirmation uses the clicked pixel");
    DWORD handles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 4; i++) {
        if (!fixture()) return 3;
        SendMessageW(pick.window, WM_KEYDOWN, VK_ESCAPE, 0);
        check(!picker_active() && accepts == 2, "Escape cancels without accepting a color");
    }
    check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == handles, "repeated sessions do not leak GDI objects");
    CloseDesktop(desktop); printf("Failures: %d\n", failures); return failures ? 1 : 0;
}
