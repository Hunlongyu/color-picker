#include "app.h"
#include <windowsx.h>
#include <imm.h>
#include <stdio.h>
#include <string.h>

/* Keep physical desktop coordinates throughout. One frozen frame also lets the
   user inspect transient menus and prevents the lens from sampling itself. */
static struct {
    HWND window;
    HDC screen, frame;
    HBITMAP bitmap, original, frame_bitmap, frame_original;
    const uint32_t *pixels;
    RECT desktop, damage;
    POINT point;
    int width, height, format, cell;
    bool show_value, ready, dirty, frame_queued;
} pick;
enum { MSG_PICKER_FRAME = WM_APP + 20 };

static void release_capture(void) {
    if (pick.original) SelectObject(pick.screen, pick.original);
    if (pick.frame_original) SelectObject(pick.frame, pick.frame_original);
    if (pick.bitmap) DeleteObject(pick.bitmap);
    if (pick.frame_bitmap) DeleteObject(pick.frame_bitmap);
    if (pick.screen) DeleteDC(pick.screen);
    if (pick.frame) DeleteDC(pick.frame);
    memset(&pick, 0, sizeof(pick));
}
static bool create_buffers(HDC desktop, int width, int height) {
    if (width <= 0 || height <= 0 || (size_t)width > SIZE_MAX / 4 / (size_t)height) return false;
    pick.width = width; pick.height = height;
    pick.screen = CreateCompatibleDC(desktop);
    pick.frame = CreateCompatibleDC(desktop);
    BITMAPINFO info = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER), .biWidth = width,
        .biHeight = -height, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
    void *pixels = NULL;
    pick.bitmap = CreateDIBSection(desktop, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    pick.frame_bitmap = CreateCompatibleBitmap(desktop, width, height);
    if (!pick.screen || !pick.frame || !pick.bitmap || !pick.frame_bitmap) return false;
    pick.original = SelectObject(pick.screen, pick.bitmap);
    pick.frame_original = SelectObject(pick.frame, pick.frame_bitmap);
    if (pick.original == HGDI_ERROR) pick.original = NULL;
    if (pick.frame_original == HGDI_ERROR) pick.frame_original = NULL;
    pick.pixels = pixels;
    return pick.original && pick.frame_original && pixels;
}
static COLORREF pixel_color(int x, int y) {
    if (x < 0 || y < 0 || x >= pick.width || y >= pick.height || !pick.pixels) return CLR_INVALID;
    uint32_t color = pick.pixels[(size_t)y * pick.width + x];
    return RGB((color >> 16) & 255, (color >> 8) & 255, color & 255);
}
static bool cursor_point(POINT *point) {
    POINT absolute;
    if (!GetCursorPos(&absolute)) return false;
    point->x = max(0, min(pick.width - 1, absolute.x - pick.desktop.left));
    point->y = max(0, min(pick.height - 1, absolute.y - pick.desktop.top));
    return true;
}

bool picker_active(void) {
    return pick.window != NULL;
}
void cancel_picker(void) {
    if (pick.window)
        DestroyWindow(pick.window);
}

static RECT lens(void) {
    int r = (pick.cell * 9) / 2;
    return (RECT){pick.point.x - r, pick.point.y - r, pick.point.x + r + 1, pick.point.y + r + 1};
}
static RECT value_box(void) {
    RECT box = lens();
    int w = pick.cell * 24, h = pick.cell * 3;
    POINT absolute = {pick.point.x + pick.desktop.left, pick.point.y + pick.desktop.top};
    MONITORINFO mon = {.cbSize = sizeof(mon)};
    GetMonitorInfoW(MonitorFromPoint(absolute, MONITOR_DEFAULTTONEAREST), &mon);
    OffsetRect(&mon.rcMonitor, -pick.desktop.left, -pick.desktop.top);
    int x = max(mon.rcMonitor.left, min(pick.point.x - w / 2, mon.rcMonitor.right - w));
    int y = box.bottom + 3;
    if (y + h > mon.rcMonitor.bottom)
        y = box.top - h - 3;
    return (RECT){x, y, x + w, y + h};
}
static void request_frame(void) {
    pick.dirty = true;
    /* Invalidate only the last displayed area. The newest destination is added
       at paint time, so a burst of motion never repaints the whole mouse trail. */
    InvalidateRect(pick.window, &pick.damage, FALSE);
    if (!pick.frame_queued)
        pick.frame_queued = PostMessageW(pick.window, MSG_PICKER_FRAME, 0, 0) != FALSE;
}
static void move_lens(int x, int y) {
    POINT next = {max(0, min(pick.width - 1, x)), max(0, min(pick.height - 1, y))};
    if (next.x == pick.point.x && next.y == pick.point.y) return;
    pick.point = next;
    request_frame();
}
void cycle_picker_format(void) {
    if (!pick.ready)
        return;
    pick.format = (pick.format + 1) % FORMAT_COUNT;
    pick.show_value = true;
    request_frame();
}
static void compose_frame(void) {
    HDC dc = pick.frame;
    RECT desktop = {0, 0, pick.width, pick.height}, old;
    if (IntersectRect(&old, &pick.damage, &desktop))
        BitBlt(dc, old.left, old.top, old.right - old.left, old.bottom - old.top,
               pick.screen, old.left, old.top, SRCCOPY);
    RECT r = lens();
    int c = pick.cell;
    HRGN circle = CreateEllipticRgn(r.left, r.top, r.left + 9 * c, r.top + 9 * c);
    int state = SaveDC(dc);
    ExtSelectClipRgn(dc, circle, RGN_AND);
    HBRUSH brush = GetStockObject(DC_BRUSH);
    SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, RGB(130, 130, 130));
    /* Expand the cached 9x9 pixels in one operation. GetPixel would synchronize
       GDI for every cell; direct reads keep the rendering work off the window. */
    uint32_t samples[9 * 9];
    for (int j = 0; j < 9; j++)
        for (int i = 0; i < 9; i++) {
            int x = max(0, min(pick.width - 1, pick.point.x + i - 4));
            int y = max(0, min(pick.height - 1, pick.point.y + j - 4));
            samples[j * 9 + i] = pick.pixels[(size_t)y * pick.width + x];
        }
    BITMAPINFO info = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER), .biWidth = 9,
        .biHeight = -9, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, r.left, r.top, c * 9, c * 9, 0, 0, 9, 9, samples, &info, DIB_RGB_COLORS, SRCCOPY);
    for (int i = 0; i < 9; i++) {
        MoveToEx(dc, r.left + i * c, r.top, NULL); LineTo(dc, r.left + i * c, r.top + 9 * c);
        MoveToEx(dc, r.left, r.top + i * c, NULL); LineTo(dc, r.left + 9 * c, r.top + i * c);
    }
    RECT center = {r.left + 4 * c - 1, r.top + 4 * c - 1, r.left + 5 * c + 1, r.top + 5 * c + 1};
    FrameRect(dc, &center, GetStockObject(WHITE_BRUSH));
    InflateRect(&center, -1, -1);
    FrameRect(dc, &center, GetStockObject(BLACK_BRUSH));
    FrameRgn(dc, circle, GetStockObject(GRAY_BRUSH), 1, 1);
    RestoreDC(dc, state);
    DeleteObject(circle);
    if (pick.show_value) {
        RECT box = value_box();
        wchar_t value[96];
        color_text(pixel_color(pick.point.x, pick.point.y), pick.format, value);
        SetDCBrushColor(dc, RGB(36, 36, 36));
        FillRect(dc, &box, brush);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(dc, value, -1, &box, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
    pick.damage = r;
    if (pick.show_value) {
        RECT box = value_box(); UnionRect(&pick.damage, &pick.damage, &box);
    }
    pick.dirty = false;
}
static void draw_picker(HWND hwnd) {
    if (pick.dirty) {
        /* Sample the cursor again after queued input, rather than drawing an
           outdated WM_MOUSEMOVE coordinate while the mouse is already ahead. */
        cursor_point(&pick.point);
        compose_frame();
        InvalidateRect(hwnd, &pick.damage, FALSE);
    }
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT dirty = ps.rcPaint;
    BitBlt(dc, dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top,
           pick.frame, dirty.left, dirty.top, SRCCOPY);
    EndPaint(hwnd, &ps);
}
static void accept_color(int x, int y) {
    COLORREF color = pixel_color(x, y);
    if (color == CLR_INVALID) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    int format = pick.format;
    cancel_picker();
    app.format = format;
    remember_color(color);
}
static LRESULT CALLBACK picker_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE:
        /* The lens takes commands. A Chinese IME otherwise consumes C as the
           start of a composition and delivers VK_PROCESSKEY instead. */
        ImmAssociateContext(hwnd, NULL);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        draw_picker(hwnd);
        return 0;
    case MSG_PICKER_FRAME:
        pick.frame_queued = false;
        if (pick.ready) UpdateWindow(hwnd);
        return 0;
    case WM_SETCURSOR:
        SetCursor(NULL);
        return TRUE;
    case WM_MOUSEMOVE: {
        POINT point;
        if (pick.ready && cursor_point(&point)) move_lens(point.x, point.y);
        return 0;
    }
    case WM_LBUTTONUP:
        if (pick.ready)
            accept_color(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return 0;
    case WM_RBUTTONUP:
    case WM_CLOSE:
        cancel_picker();
        return 0;
    case WM_KEYDOWN:
        if (!pick.ready) return 0;
        if (w == VK_ESCAPE)
            cancel_picker();
        else if (w == VK_RETURN) {
            cursor_point(&pick.point);
            accept_color(pick.point.x, pick.point.y);
        } else if (w == 'C' && !(l & (1L << 30))) {
            cycle_picker_format();
        } else if (w >= VK_LEFT && w <= VK_DOWN) {
            POINT p;
            if (GetCursorPos(&p) &&
                SetCursorPos(p.x + (w == VK_RIGHT) - (w == VK_LEFT), p.y + (w == VK_DOWN) - (w == VK_UP)) &&
                cursor_point(&p))
                move_lens(p.x, p.y);
        }
        return 0;
    case WM_ACTIVATEAPP:
        if (!w && pick.ready)
            cancel_picker();
        return 0;
    case WM_DISPLAYCHANGE:
        cancel_picker();
        return 0;
    case WM_DESTROY:
        release_capture();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
void start_picker(void) {
    if (picker_active() || !IsWindowEnabled(app.window))
        return;
    WNDCLASSW cls = {
        .lpfnWndProc = picker_proc, .hInstance = app.instance, .lpszClassName = L"HlyColorPicker.Lens"};
    RegisterClassW(&cls);
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN), height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    pick.desktop = (RECT){x, y, x + width, y + height};
    HDC desktop = GetDC(NULL);
    BOOL captured = create_buffers(desktop, width, height) &&
                    BitBlt(pick.screen, 0, 0, width, height, desktop, x, y, SRCCOPY | CAPTUREBLT) &&
                    BitBlt(pick.frame, 0, 0, width, height, pick.screen, 0, 0, SRCCOPY);
    /* Complete the single capture before accessing its DIB pixels directly. */
    GdiFlush();
    ReleaseDC(NULL, desktop);
    if (captured) {
        cursor_point(&pick.point);
        pick.format = app.format;
        pick.show_value = false;
        pick.cell = max(6, px(10));
        pick.damage = lens();
        pick.dirty = true;
        pick.window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, cls.lpszClassName, L"Color Picker",
                                      WS_POPUP, x, y, width, height, NULL, NULL, app.instance, NULL);
    }
    if (!pick.window) {
        release_capture();
        MessageBoxW(app.window, tr(L"无法截取当前桌面。", L"Could not capture the desktop."), L"Color Picker",
                    MB_ICONERROR);
        return;
    }
    ShowWindow(pick.window, SW_SHOW);
    SetForegroundWindow(pick.window);
    SetFocus(pick.window);
    SetCursor(NULL);
    pick.ready = true;
    UpdateWindow(pick.window);
}
