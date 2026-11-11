#include "app.h"
#include "icons.h"
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <imm.h>
#include <cui/cui.h>
#include <cui/cui_render.h>
#include <cui/cui_icons.h>
/* The pinned CUI revision has no public APIs for absolute widget placement,
   focus transfer, popup geometry or glyph ink bounds. Keep these host adapters here. */
#include <cui_internal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

enum Hit { NONE, MENU, HOME, ISSUES, HIDE, QUIT, PICK, COPY, FORMAT, STATUS,
           NOTICE, LANGUAGE, THEME, AUTO_UPDATE, CHECK_UPDATE, UPDATE_ACTION, VALUE, SHORTCUT, SWATCH };
enum { TIMER_HISTORY = 0x4350, TIMER_ACTIVITY, TIMER_CUI, HISTORY_DURATION_MS = 240, MENU_HEIGHT = 278 };
enum HistoryAction { HISTORY_ADD, HISTORY_REORDER, HISTORY_REMOVE };
typedef struct {
    cui_rect rect;
    cui_corners corners;
    cui_color color;
} RemovedSwatch;
static struct {
    cui_ctx *ui;
    cui_sw_renderer *renderer;
    cui_renderer_iface iface;
    cui_texture icons[ICON_COUNT];
    int icon_size;
    int main_width, main_max_width, format_widths[FORMAT_COUNT];
    HWND tips;
    int hover, pressed;
    bool menu_extent, menu_pending, shortcut_dirty, suppress_history;
    wchar_t surrogate;
    char value[4096], shortcut[192];
    char *notes, *clipboard;
    ULONGLONG last_frame;
    const wchar_t *shown_notes;
    cui_rect history_from[HISTORY_MAX];
    float history_alpha_from[HISTORY_MAX];
    RemovedSwatch history_removed[HISTORY_MAX];
    int history_removed_count;
    ULONGLONG history_started;
    float history_progress;
    enum HistoryAction history_action;
    unsigned char *pixels;
    size_t capacity;
} panel = {.history_progress = 1.f};

static void update_tips(void);
static void activate(int hit);
static void draw_labels(cui_ctx *ui);
static void draw_controls(cui_ctx *ui);
static void commit_shortcut(void);
static void sync_geometry(void);
static void close_popups(void);

static char *utf8(const wchar_t *text) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    char *result = n > 0 ? malloc((size_t)n) : NULL;
    if (result) WideCharToMultiByte(CP_UTF8, 0, text, -1, result, n, NULL, NULL);
    return result;
}
void panel_set_value(const wchar_t *text) {
    WideCharToMultiByte(CP_UTF8, 0, text, -1, panel.value, sizeof(panel.value), NULL, NULL);
}
const char *panel_value(void) { return panel.value; }
static cui_id control_id(int hit) {
    char id[32]; snprintf(id, sizeof(id), "control-%d", hit);
    return cui_make_id(panel.ui, id);
}
static bool popup_active(void) {
    for (int i = 0; i < panel.ui->popup_count; i++)
        if (panel.ui->popups[i].open) return true;
    return false;
}
static void close_popups(void) {
    panel.menu_pending = false;
    if (!panel.ui) return;
    for (int i = 0; i < panel.ui->popup_count; i++) {
        panel.ui->popups[i].open = false;
        panel.ui->popups[i].s = (cui_spring){0};
    }
    panel.ui->focus = panel.ui->active = 0;
}
static void set_clipboard(void *unused, const char *text) {
    (void)unused;
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wide = n > 0 ? malloc((size_t)n * sizeof(wchar_t)) : NULL;
    if (wide) {
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, n);
        copy_text(wide); free(wide);
    }
}
static const char *get_clipboard(void *unused) {
    (void)unused;
    free(panel.clipboard); panel.clipboard = NULL;
    if (OpenClipboard(app.window)) {
        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        const wchar_t *text = data ? GlobalLock(data) : NULL;
        if (text) { panel.clipboard = utf8(text); GlobalUnlock(data); }
        CloseClipboard();
    }
    return panel.clipboard ? panel.clipboard : "";
}
static void ime_caret(void *unused, cui_rect caret) {
    (void)unused;
    HIMC imc = ImmGetContext(app.window);
    if (imc) {
        COMPOSITIONFORM form = {.dwStyle = CFS_POINT, .ptCurrentPos = {px((int)caret.x), px((int)(caret.y + caret.h))}};
        ImmSetCompositionWindow(imc, &form);
        ImmReleaseContext(app.window, imc);
    }
}
static bool load_font(void) {
    const wchar_t *files[] = {L"msyh.ttc", L"segoeui.ttf", L"simsun.ttc"};
    for (int i = 0; i < 3; i++) {
        wchar_t path[MAX_PATH]; GetWindowsDirectoryW(path, MAX_PATH);
        wcscat(path, L"\\Fonts\\"); wcscat(path, files[i]);
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (file == INVALID_HANDLE_VALUE) continue;
        DWORD size = GetFileSize(file, NULL), got = 0;
        void *data = size != INVALID_FILE_SIZE ? malloc(size) : NULL;
        cui_font font = data && ReadFile(file, data, size, &got, NULL) && got == size
                            ? cui_font_load_memory(panel.ui, data, size, true) : -1;
        free(data); CloseHandle(file);
        if (font >= 0) {
            cui_font_set_role(panel.ui, CUI_FONT_BODY, font);
            cui_font_set_role(panel.ui, CUI_FONT_MEDIUM, font);
            return true;
        }
    }
    return false;
}

int panel_height(void) {
    return app.update_view ? UPDATE_HEIGHT : app.settings ? SETTINGS_HEIGHT :
        app.count || panel.history_removed_count ? PANEL_HEIGHT : PANEL_COMPACT_HEIGHT;
}
int panel_width(void) { return app.settings ? PANEL_WIDTH : max(PANEL_MIN_WIDTH, panel.main_width); }
static int value_width(const char *text) {
    /* 168 DIP for the pipette, copy button, format selector and outer spacing;
       26 DIP inside the input for padding, border and caret. */
    return (int)ceilf(cui_measure_text(panel.ui, CUI_FONT_BODY, 16, text, -1).width) + 168 + 26;
}
static void update_main_width(void) {
    if (!panel.main_max_width) {
        static const char *const samples[FORMAT_COUNT] = {
            "#DDDDDD", "hsl(359.9, 99.9%, 99.9%)", "rgb(255, 255, 255)",
            "100,100,100,100", "hwb(359.9, 99.9%, 99.9%)"
        };
        panel.main_max_width = PANEL_WIDTH;
        for (int i = 0; i < FORMAT_COUNT; i++) {
            panel.format_widths[i] = max(PANEL_MIN_WIDTH, value_width(samples[i]));
            panel.main_max_width = max(panel.main_max_width, panel.format_widths[i]);
        }
    }
    int width = max(panel.format_widths[app.format], value_width(panel.value));
    panel.main_width = min(width, panel.main_max_width);
}
static float history_width(int count) {
    float available = (float)(panel_width() - 14);
    return count > 0 && count * 30.f > available ? available / (float)count : 30.f;
}
static cui_color accent(void) { return app.dark ? CUI_RGB(0x65ddc4) : CUI_RGB(0x087f70); }
static cui_color muted(void) { return app.dark ? CUI_RGB(0xa8b3be) : CUI_RGB(0x65737f); }
static cui_color card(void) { return app.dark ? CUI_RGB(0x242b33) : CUI_RGB(0xffffff); }
static cui_color edge(void) { return app.dark ? CUI_RGB(0x38424c) : CUI_RGB(0xe1e7ea); }

static cui_color surface(void) {
    if (app.settings)
        return app.dark ? CUI_RGB(0x191f26) : CUI_RGB(0xf3f6f7);
    return app.dark ? CUI_RGB(0x252525) : CUI_RGB(0xffffff);
}
static cui_color field(void) {
    if (app.settings)
        return app.dark ? CUI_RGB(0x303a44) : CUI_RGB(0xf0f4f5);
    return app.dark ? CUI_RGB(0x383838) : CUI_RGB(0xf6f3f1);
}
static cui_color ink(void) {
    return app.dark ? CUI_RGB(0xf3f1ef) : CUI_RGB(0x333333);
}
static cui_color from_pixel(COLORREF c) {
    return (cui_color){GetRValue(c), GetGValue(c), GetBValue(c), 255};
}
static cui_rect bounds(int hit) {
    switch (hit) {
    case MENU:
        return (cui_rect){1, 1, 30, 30};
    case HOME:
        return (cui_rect){17, 499, 338, 55};
    case ISSUES:
        return (cui_rect){17, 555, 338, 54};
    case HIDE:
        return (cui_rect){(float)panel_width() - 61, 1, 30, 30};
    case QUIT:
        return (cui_rect){(float)panel_width() - 31, 1, 30, 30};
    case PICK:
        return (cui_rect){7, 41, 30, 30};
    case COPY:
        return (cui_rect){(float)panel_width() - 131, 41, 30, 30};
    case FORMAT:
        return app.settings ? (cui_rect){194, 132, 146, 32} : (cui_rect){(float)panel_width() - 91, 41, 82, 30};
    case STATUS:
        return (cui_rect){143, 91, 12, 12};
    case NOTICE: return (cui_rect){(float)panel_width() - 91, 1, 30, 30};
    case LANGUAGE: return (cui_rect){194, 224, 146, 32};
    case THEME: return (cui_rect){194, 276, 146, 32};
    case AUTO_UPDATE: return (cui_rect){28, 423, 312, 32};
    case CHECK_UPDATE: return (cui_rect){210, 366, 130, 34};
    case UPDATE_ACTION: return (cui_rect){16, 438, 340, 34};
    case VALUE: return (cui_rect){37, 41, (float)panel_width() - 168, 30};
    case SHORTCUT: return (cui_rect){168, 80, 172, 32};
    default: {
        float width = history_width(app.count);
        cui_rect r = {7 + (hit - SWATCH) * width, 86, width, 30};
        float t = panel.history_progress;
        if (t < 1.f) {
            cui_rect from = panel.history_from[hit - SWATCH];
            float remaining = 1.f - t;
            float eased = 1.f - remaining * remaining * remaining;
            r.x = from.x + (r.x - from.x) * eased;
            r.y = from.y + (r.y - from.y) * eased;
            r.w = from.w + (r.w - from.w) * eased;
            r.h = from.h + (r.h - from.h) * eased;
            if (hit == SWATCH && panel.history_action == HISTORY_REORDER)
                r.y -= 16.f * t * (1.f - t);
        }
        return r;
    }
    }
}
static cui_corners swatch_corners(int index) {
    return (cui_corners){index == 0 ? 4.f : 0.f, index == app.count - 1 ? 4.f : 0.f,
                         index == app.count - 1 ? 4.f : 0.f, index == 0 ? 4.f : 0.f};
}
static float swatch_opacity(int index) {
    float remaining = 1.f - panel.history_progress;
    return 1.f - (1.f - panel.history_alpha_from[index]) * remaining * remaining;
}
static RemovedSwatch removed_swatch(int index) {
    RemovedSwatch swatch = panel.history_removed[index];
    float remaining = 1.f - panel.history_progress;
    float shrink = .35f * (1.f - remaining * remaining * remaining);
    swatch.rect.x += swatch.rect.w * shrink * .5f;
    swatch.rect.y += swatch.rect.h * shrink * .5f;
    swatch.rect.w *= 1.f - shrink;
    swatch.rect.h *= 1.f - shrink;
    swatch.color.a = (uint8_t)(swatch.color.a * remaining * remaining);
    return swatch;
}
static void finish_history_animation(void) {
    KillTimer(app.window, TIMER_HISTORY);
    bool was_animating = panel.history_progress < 1.f;
    panel.history_progress = 1.f;
    panel.history_removed_count = 0;
    if (was_animating) {
        if (panel.hover >= SWATCH)
            panel.hover = NONE;
        update_tips();
    }
}
static bool capture_history_animation(cui_rect previous[HISTORY_MAX], float opacity[HISTORY_MAX]) {
    if (app.settings || !IsWindowVisible(app.window)) {
        finish_history_animation();
        return false;
    }
    /* Capture the displayed positions before the history array is reordered.
       Further picks and deletions continue from an unfinished transition. */
    for (int i = 0; i < app.count; i++) {
        previous[i] = bounds(SWATCH + i);
        opacity[i] = swatch_opacity(i);
    }
    int count = 0;
    for (int i = 0; i < panel.history_removed_count; i++) {
        RemovedSwatch swatch = removed_swatch(i);
        if (swatch.color.a)
            panel.history_removed[count++] = swatch;
    }
    panel.history_removed_count = count;
    return true;
}
static void start_history_animation(void) {
    panel.history_started = GetTickCount64();
    panel.history_progress = 0.f;
    if (!SetTimer(app.window, TIMER_HISTORY, 16, NULL))
        finish_history_animation();
}
void panel_animate_history(int previous_index) {
    cui_rect previous[HISTORY_MAX] = {{0}};
    float opacity[HISTORY_MAX] = {0};
    if (!capture_history_animation(previous, opacity))
        return;
    bool is_new = previous_index == app.count;
    panel.history_action = is_new ? HISTORY_ADD : HISTORY_REORDER;
    int next_count = min(app.count + (is_new ? 1 : 0), HISTORY_MAX);
    float width = history_width(next_count);
    panel.history_from[0] = is_new ? (cui_rect){7 + width * .15f, 94, width * .7f, 21} : previous[previous_index];
    panel.history_alpha_from[0] = is_new ? 0.f : opacity[previous_index];
    for (int i = 1; i < next_count; i++) {
        int source = i <= previous_index ? i - 1 : i;
        panel.history_from[i] = previous[source];
        panel.history_alpha_from[i] = opacity[source];
    }
    start_history_animation();
}
static void animate_history_removal(int index) {
    cui_rect previous[HISTORY_MAX] = {{0}};
    float opacity[HISTORY_MAX] = {0};
    if (!capture_history_animation(previous, opacity))
        return;
    /* Keep only the outgoing appearance. The actual history is deleted and
       persisted immediately, and hit testing uses only the remaining colors. */
    if (panel.history_removed_count == HISTORY_MAX) {
        memmove(panel.history_removed, panel.history_removed + 1, (HISTORY_MAX - 1) * sizeof(RemovedSwatch));
        panel.history_removed_count--;
    }
    cui_color color = from_pixel(app.history[index]);
    color.a = (uint8_t)(255.f * opacity[index]);
    panel.history_removed[panel.history_removed_count++] =
        (RemovedSwatch){previous[index], swatch_corners(index), color};
    for (int i = 0; i < app.count - 1; i++) {
        int source = i < index ? i : i + 1;
        panel.history_from[i] = previous[source];
        panel.history_alpha_from[i] = opacity[source];
    }
    panel.history_action = HISTORY_REMOVE;
    start_history_animation();
}
static RECT physical(cui_rect r) {
    return (RECT){px((int)r.x), px((int)r.y), px((int)(r.x + r.w)), px((int)(r.y + r.h))};
}
static bool visible_hit(int id) {
    if (id == HOME || id == ISSUES || id == STATUS)
        return app.settings && !app.update_view;
    if (id == NOTICE)
        return !app.settings && update_has_notice();
    if (id == LANGUAGE || id == THEME || id == AUTO_UPDATE || id == CHECK_UPDATE)
        return app.settings && !app.update_view;
    if (id == UPDATE_ACTION)
        return app.update_view;
    if (id == FORMAT)
        return !app.update_view;
    if (id == VALUE)
        return !app.settings;
    if (id == SHORTCUT)
        return app.settings && !app.update_view;
    if (id == PICK || id == COPY || id >= SWATCH)
        return !app.settings;
    return true;
}
static int hit_test(LPARAM l) {
    POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    for (int i = MENU; i < SWATCH + app.count; i++) {
        RECT r = physical(bounds(i));
        if (visible_hit(i) && PtInRect(&r, p))
            return i;
    }
    return NONE;
}
static void icon(cui_ctx *ui, int index, int hit, float size, cui_color color) {
    cui_rect box = bounds(hit);
    cui_rect r = {box.x + (box.w - size) / 2, box.y + (box.h - size) / 2, size, size};
    cui_draw_image(ui, panel.icons[index], r, (cui_rect){0, 0, 1, 1}, CUI_CORNERS(0), color);
}
static void image_at(cui_ctx *ui, int index, cui_rect r, cui_color color) {
    cui_draw_image(ui, panel.icons[index], r, (cui_rect){0, 0, 1, 1}, CUI_CORNERS(0), color);
}
static void button(cui_ctx *ui, int hit, bool primary) {
    cui_color fill = primary ? accent() : field();
    if (panel.hover == hit) {
        fill.r = (uint8_t)(fill.r * .92f);
        fill.g = (uint8_t)(fill.g * .92f);
        fill.b = (uint8_t)(fill.b * .92f);
    }
    cui_draw_rect(ui, bounds(hit), CUI_CORNERS(7), fill);
}
static void draw_canvas(cui_ctx *ui, cui_rect canvas, void *unused) {
    (void)unused;
    cui_draw_rect_border(ui, canvas, CUI_CORNERS(8), surface(), edge(), 1);
    if (!app.settings) {
        cui_rect input = bounds(VALUE); input.w += bounds(COPY).w;
        cui_draw_rect(ui, input, CUI_CORNERS(0), field());
        for (int i = 0; i < panel.history_removed_count; i++) {
            RemovedSwatch swatch = removed_swatch(i);
            cui_draw_rect(ui, swatch.rect, swatch.corners, swatch.color);
        }
        for (int i = app.count - 1; i >= 0; i--) {
            cui_corners radius = swatch_corners(i);
            cui_rect r = bounds(SWATCH + i);
            if (panel.hover == SWATCH + i && panel.history_progress == 1.f)
                cui_draw_shadow(ui, r, radius, 4, 0, (cui_vec2){0, 0}, CUI_RGBA(0x33333399));
            cui_color color = from_pixel(app.history[i]);
            color.a = (uint8_t)(255.f * swatch_opacity(i));
            cui_draw_rect(ui, r, radius, color);
        }
    } else if (app.update_view) {
        cui_draw_rect_border(ui, (cui_rect){16, 132, 340, 244}, CUI_CORNERS(12), card(), edge(), 1);
        button(ui, UPDATE_ACTION, update_info()->phase != UPDATE_DOWNLOADING);
        const UpdateInfo *u = update_info();
        if (u->phase == UPDATE_DOWNLOADING || u->phase == UPDATE_CHECKING || u->phase == UPDATE_READY) {
            cui_rect track = {16, 424, 340, 4};
            cui_draw_rect(ui, track, CUI_CORNERS(2), edge());
            if (u->percent >= 0 && u->phase != UPDATE_CHECKING)
                track.w *= (float)u->percent / 100.f;
            else {
                float t = (float)(GetTickCount64() % 1800) / 1800.f;
                track.x += 248.f * (t < .5f ? t * 2 : (1 - t) * 2);
                track.w = 92;
            }
            cui_draw_rect(ui, track, CUI_CORNERS(2), accent());
        }
        image_at(ui, u->phase == UPDATE_READY ? ICON_CHECK : ICON_DOWNLOAD,
                 (cui_rect){20, 60, 26, 26}, accent());
    } else {
        for (int i = 0; i < 4; i++) {
            float y = 66.f + i * 144.f;
            cui_draw_rect_border(ui, (cui_rect){16, y, 340, 112}, CUI_CORNERS(12), card(), edge(), 1);
            cui_draw_rect(ui, (cui_rect){28, y + 56, 312, 1}, CUI_CORNERS(0), edge());
        }
        cui_draw_circle(ui, (cui_vec2){149, 97}, 3,
                        app.registered ? CUI_RGB(0x17a05d) : CUI_RGB(0xdd4f43));
        button(ui, CHECK_UPDATE, true);
        cui_color toggle = app.auto_update ? accent() : edge();
        cui_draw_rect(ui, (cui_rect){304, 429, 36, 20}, CUI_CORNERS(10), toggle);
        cui_draw_circle(ui, (cui_vec2){app.auto_update ? 330.f : 314.f, 439}, 7, CUI_RGB(0xffffff));
        image_at(ui, ICON_KEYBOARD, (cui_rect){28, 88, 18, 18}, muted());
        image_at(ui, ICON_PALETTE, (cui_rect){28, 139, 18, 18}, muted());
        image_at(ui, ICON_LANGUAGE, (cui_rect){28, 231, 18, 18}, muted());
        image_at(ui, ICON_THEME, (cui_rect){28, 283, 18, 18}, muted());
        if (panel.hover == HOME || panel.hover == ISSUES) {
            cui_corners corners = panel.hover == HOME ? (cui_corners){11, 11, 0, 0}
                                                      : (cui_corners){0, 0, 11, 11};
            cui_draw_rect(ui, bounds(panel.hover), corners, field());
        }
    }
    if ((panel.hover == MENU || panel.hover == HIDE || panel.hover == QUIT || panel.hover == NOTICE ||
         panel.hover == PICK || panel.hover == COPY) && visible_hit(panel.hover))
        cui_draw_rect(ui, bounds(panel.hover), CUI_CORNERS(5), field());
    icon(ui, app.settings ? ICON_BACK : ICON_SETTINGS, MENU, 20, ink());
    icon(ui, ICON_MINUS, HIDE, 20, ink());
    icon(ui, ICON_CLOSE, QUIT, 18, ink());
    if (!app.settings) {
        icon(ui, ICON_PICK, PICK, 20, ink());
        icon(ui, ICON_COPY, COPY, 18, ink());
        if (update_has_notice()) {
            icon(ui, update_info()->phase == UPDATE_READY ? ICON_RESTART : ICON_DOWNLOAD, NOTICE, 19, accent());
            cui_draw_circle(ui, (cui_vec2){bounds(NOTICE).x + 24, 6}, 3, CUI_RGB(0xf09a38));
        }
    }
    draw_labels(ui);
}
static void label(cui_ctx *ui, const wchar_t *s, cui_rect r, float size, cui_color color, UINT align) {
    char text[2048];
    if (!WideCharToMultiByte(CP_UTF8, 0, s, -1, text, sizeof(text), NULL, NULL))
        return;
    cui_text_metrics m = cui_measure_text(ui, CUI_FONT_BODY, size, text, -1);
    float x = r.x;
    float width = m.width < r.w ? m.width : r.w;
    if (align == DT_CENTER) x += (r.w - width) * .5f;
    if (align == DT_RIGHT) x += r.w - width;
    cui_push_clip(ui, r);
    /* CUI draws from the font box top; measured height includes extra paragraph
       leading and would shift a single-line label above the control center. */
    float font_height = m.ascent - m.descent;
    int first_glyph = ui->dl.vtx_count;
    cui__draw_text_ellipsis(ui, CUI_FONT_BODY, size, x, r.y + (r.h - font_height) * .5f,
                            r.w, color, text, -1, false);
    if (align == DT_CENTER && ui->dl.vtx_count > first_glyph) {
        /* Center button captions by the glyphs CUI actually emitted, including
           fallback fonts. Snap to whole device pixels to keep the text crisp. */
        float top = ui->dl.vtx[first_glyph].y, bottom = top;
        for (int i = first_glyph; i < ui->dl.vtx_count; i++) {
            top = fminf(top, ui->dl.vtx[i].y); bottom = fmaxf(bottom, ui->dl.vtx[i].y);
        }
        float shift = roundf((r.y + r.h * .5f) * ui->scale - (top + bottom) * .5f);
        for (int i = first_glyph; i < ui->dl.vtx_count; i++) ui->dl.vtx[i].y += shift;
    }
    cui_pop_clip(ui);
}
static const wchar_t *action_label(void) {
    switch (update_info()->phase) {
    case UPDATE_CHECKING: return tr(L"正在检查…", L"Checking…");
    case UPDATE_DOWNLOADING: return tr(L"取消下载", L"Cancel download");
    case UPDATE_READY: return tr(L"重启安装", L"Restart and install");
    case UPDATE_FAILED: return tr(L"重试", L"Try again");
    default: return update_has_notice() ? tr(L"下载更新", L"Download update")
                                       : tr(L"检查更新", L"Check for updates");
    }
}
static void draw_labels(cui_ctx *ui) {
    if (!app.settings) return;
    label(ui, app.update_view ? tr(L"软件更新", L"Software update") : tr(L"设置", L"Settings"),
          (cui_rect){42, 1, 220, 30}, 14, ink(), DT_LEFT);
    const UpdateInfo *u = update_info();
    wchar_t version[128];
    swprintf(version, ARRAYSIZE(version), tr(L"当前版本  %hs", L"Version  %hs"), APP_VERSION);
    if (app.update_view) {
        label(ui, u->version[0] ? u->version : tr(L"保持最新", L"Stay up to date"),
              (cui_rect){58, 56, 288, 32}, 22, ink(), DT_LEFT);
        label(ui, version, (cui_rect){58, 90, 288, 20}, 12, muted(), DT_LEFT);
        label(ui, tr(L"更新内容", L"Release notes"), (cui_rect){20, 110, 320, 22}, 12, muted(), DT_LEFT);
        label(ui, update_status(), (cui_rect){16, 384, 296, 32}, 12, muted(), DT_LEFT);
        if (u->phase == UPDATE_DOWNLOADING && u->percent >= 0) {
            wchar_t percent[16]; swprintf(percent, ARRAYSIZE(percent), L"%d%%", u->percent);
            label(ui, percent, (cui_rect){309, 383, 47, 23}, 12, muted(), DT_RIGHT);
        }
        label(ui, action_label(), bounds(UPDATE_ACTION), 14,
              u->phase == UPDATE_DOWNLOADING ? ink() : app.dark ? CUI_RGB(0x102c27) : CUI_RGB(0xffffff), DT_CENTER);
    } else {
        label(ui, tr(L"取色", L"COLOR PICKING"), (cui_rect){20, 40, 320, 26}, 12, muted(), DT_LEFT);
        label(ui, tr(L"外观", L"APPEARANCE"), (cui_rect){20, 184, 320, 26}, 12, muted(), DT_LEFT);
        label(ui, tr(L"软件更新", L"UPDATES"), (cui_rect){20, 328, 320, 26}, 12, muted(), DT_LEFT);
        label(ui, tr(L"关于", L"ABOUT"), (cui_rect){20, 472, 320, 26}, 12, muted(), DT_LEFT);
        label(ui, tr(L"快捷键", L"Shortcut"), (cui_rect){54, 82, 88, 30}, 14, ink(), DT_LEFT);
        label(ui, tr(L"默认格式", L"Format"), (cui_rect){54, 133, 132, 30}, 14, ink(), DT_LEFT);
        label(ui, tr(L"语言", L"Language"), (cui_rect){54, 225, 132, 30}, 14, ink(), DT_LEFT);
        label(ui, tr(L"主题", L"Theme"), (cui_rect){54, 277, 132, 30}, 14, ink(), DT_LEFT);
        label(ui, tr(L"自动检查更新", L"Automatic updates"), (cui_rect){28, 423, 270, 32}, 14, ink(), DT_LEFT);
        label(ui, version, (cui_rect){28, 364, 176, 22}, 12, ink(), DT_LEFT);
        label(ui, update_status(), (cui_rect){28, 387, 176, 21}, 12, muted(), DT_LEFT);
        label(ui, update_has_notice() ? tr(L"查看更新", L"View update") : tr(L"检查更新", L"Check updates"),
              bounds(CHECK_UPDATE), 12, app.dark ? CUI_RGB(0x102c27) : CUI_RGB(0xffffff), DT_CENTER);
        cui_rect website = bounds(HOME), feedback = bounds(ISSUES);
        label(ui, tr(L"官网", L"Website"), (cui_rect){28, website.y, 130, website.h}, 14, ink(), DT_LEFT);
        label(ui, L"GitHub", (cui_rect){164, website.y, 176, website.h}, 14, accent(), DT_RIGHT);
        label(ui, tr(L"反馈", L"Feedback"), (cui_rect){28, feedback.y, 130, feedback.h}, 14, ink(), DT_LEFT);
        label(ui, tr(L"提交问题", L"Report an issue"), (cui_rect){164, feedback.y, 176, feedback.h}, 14, accent(), DT_RIGHT);
    }
}
static void slot(cui_ctx *ui, cui_rect rect) {
    int node = cui__absolute(ui, 0, rect.x, rect.y, rect.w, rect.h);
    cui__open(ui, node);
}
static void svg_glyph(cui_ctx *ui, cui_node *node, void *data) {
    int index = (int)(uintptr_t)data - 1;
    cui_rect r = node->rect;
    r.x += (r.w - 16) * .5f; r.y += (r.h - 16) * .5f; r.w = r.h = 16;
    image_at(ui, index, r, node->text_color);
}
static void focus_outline(cui_ctx *ui, cui_node *node, void *unused) {
    (void)unused;
    cui_rect r = node->rect;
    r.x += 2; r.y += 2; r.w -= 4; r.h -= 4;
    cui_draw_rect_border(ui, r, CUI_CORNERS(5), CUI_TRANSPARENT, accent(), 1);
}
static void custom_control(cui_ctx *ui, int hit) {
    if (!visible_hit(hit)) return;
    cui_rect r = bounds(hit);
    cui_id id = control_id(hit);
    cui_interact input = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    slot(ui, r);
    int node = cui__leaf(ui, id, CUI_PX(r.w), CUI_PX(r.h));
    cui__mark_interactive(ui, node, CUI_IF_FOCUSABLE);
    if (input.focus_visible) ui->nodes[node].paint = focus_outline;
    cui__close(ui);
    if (input.clicked) PostMessageW(app.window, MSG_PANEL_ACTION, hit, 0);
}
static void draw_controls(cui_ctx *ui) {
    cui_theme *theme = cui_theme_get(ui);
    theme->control_md = app.settings ? 32.f : 30.f;
    theme->text_body = app.settings ? 14.f : 16.f;
    theme->text_sm = 14;
    theme->radius_control = 7;
    theme->radius_panel = 10;
    theme->colors[CUI_TOKEN_SURFACE] = field();
    theme->colors[CUI_TOKEN_SURFACE_RAISED] = card();
    theme->colors[CUI_TOKEN_FOREGROUND] = ink();
    theme->colors[CUI_TOKEN_TEXT_MUTED] = muted();
    theme->colors[CUI_TOKEN_TEXT_SECONDARY] = muted();
    theme->colors[CUI_TOKEN_BORDER] = edge();
    theme->colors[CUI_TOKEN_BORDER_STRONG] = edge();
    theme->colors[CUI_TOKEN_ACCENT] = accent();
    theme->colors[CUI_TOKEN_ACCENT_STRONG] = accent();
    theme->colors[CUI_TOKEN_FOCUS_RING] = accent();
    custom_control(ui, MENU); custom_control(ui, HIDE); custom_control(ui, QUIT);
    if (!app.update_view) {
        int field_hit = app.settings ? SHORTCUT : VALUE;
        cui_rect r = bounds(field_hit);
        slot(ui, r);
        char field_id[32]; snprintf(field_id, sizeof(field_id), "control-%d", field_hit);
        int flags = cui_input_ex(ui, field_id, app.settings ? panel.shortcut : panel.value,
                                app.settings ? 64 : sizeof(panel.value), &(cui_input_opts){.width = CUI_PX(r.w)});
        cui__close(ui);
        if (app.settings) {
            if (flags & CUI_CHANGED) panel.shortcut_dirty = true;
            if (flags & (CUI_SUBMITTED | CUI_BLURRED)) commit_shortcut();
        } else if (flags & CUI_CHANGED) cui_request_redraw(ui);
        static const char *const color_formats[] = {"HEX", "HSL", "RGB", "CMYK", "HWB"};
        int selected = app.settings ? app.default_format : app.format;
        r = bounds(FORMAT); slot(ui, r);
        if (cui_select_ex(ui, "format", color_formats, FORMAT_COUNT, &selected, &(cui_select_opts){.width = CUI_PX(r.w)})) {
            app.format = selected;
            if (app.settings) { app.default_format = selected; save_option(L"format", formats[selected]); }
            refresh_value(); cui_request_redraw(ui);
        }
        cui__close(ui);
        if (app.settings) {
            const char *languages[] = {app.english ? "System default" : "系统默认", "简体中文", "English"};
            const char *themes[] = {app.english ? "System default" : "系统默认",
                                    app.english ? "Light" : "浅色", app.english ? "Dark" : "深色"};
            int language = app.language, selected_theme = app.theme;
            r = bounds(LANGUAGE); slot(ui, r);
            if (cui_select_ex(ui, "language", languages, 3, &language, &(cui_select_opts){.width = CUI_PX(r.w)})) {
                set_language(language);
                PostMessageW(app.window, MSG_PANEL_REFRESH, 0, 0);
            }
            cui__close(ui);
            r = bounds(THEME); slot(ui, r);
            if (cui_select_ex(ui, "theme", themes, 3, &selected_theme, &(cui_select_opts){.width = CUI_PX(r.w)})) {
                set_theme(selected_theme);
                PostMessageW(app.window, MSG_PANEL_REFRESH, 0, 0);
            }
            cui__close(ui);
            custom_control(ui, CHECK_UPDATE); custom_control(ui, AUTO_UPDATE);
            custom_control(ui, HOME); custom_control(ui, ISSUES);
        } else {
            custom_control(ui, PICK); custom_control(ui, COPY); custom_control(ui, NOTICE);
            for (int i = 0; i < app.count; i++) custom_control(ui, SWATCH + i);
        }
    } else {
        slot(ui, (cui_rect){24, 140, 324, 228});
        cui_textarea_ex(ui, "release-notes", panel.notes ? panel.notes : "", panel.notes ? strlen(panel.notes) + 1 : 1,
                       &(cui_input_opts){.read_only = true, .height = 228, .width = CUI_PX(324)});
        cui__close(ui);
        custom_control(ui, UPDATE_ACTION);
    }
    /* Use Lucide SVG masks in the real CUI controls instead of an icon font. */
    for (int i = 0; i < ui->node_count; i++) {
        cui_node *n = &ui->nodes[i];
        if (n->id == cui_make_id(ui, "format") && !app.settings) {
            n->pad[3] = 8; n->pad[1] = 6; n->gap = 4;
        }
        if (n->font != CUI_FONT_ICONS || !n->text) continue;
        int index = !strcmp(n->text, CUI_ICON_CHECK) ? ICON_CHECK : ICON_CHEVRON;
        n->text = NULL; n->text_len = 0; n->flags &= ~CUI_NF_TEXT;
        n->w = CUI_PX(16); n->h = CUI_PX(16);
        n->paint = svg_glyph; n->paint_data = (void *)(uintptr_t)(index + 1);
    }
}
static void paint(HDC target) {
    if (panel.history_progress < 1.f) {
        ULONGLONG elapsed = GetTickCount64() - panel.history_started;
        if (elapsed >= HISTORY_DURATION_MS)
            finish_history_animation();
        else
            panel.history_progress = (float)elapsed / HISTORY_DURATION_MS;
    }
    update_main_width();
    sync_geometry();
    PAINTSTRUCT ps;
    HDC dc = target ? target : BeginPaint(app.window, &ps);
    RECT rc;
    GetClientRect(app.window, &rc);
    int w = rc.right, h = rc.bottom;
    size_t size = (size_t)w * h * 4;
    if (size > panel.capacity) {
        void *p = realloc(panel.pixels, size);
        if (!p) {
            if (!target)
                EndPaint(app.window, &ps);
            return;
        }
        panel.pixels = p;
        panel.capacity = size;
    }
    if (panel.ui && w && h) {
        cui_set_viewport(panel.ui, (float)panel_width(), (float)h * 96.f / app.dpi, (float)app.dpi / 96);
        ULONGLONG now = GetTickCount64();
        float dt = panel.last_frame ? (float)(now - panel.last_frame) / 1000.f : 1.f / 60;
        panel.last_frame = now;
        cui_begin_frame(panel.ui, dt);
        slot(panel.ui, (cui_rect){0, 0, (float)panel_width(), (float)panel_height()});
        cui_canvas(panel.ui, "panel", CUI_PX(panel_width()), CUI_PX(panel_height()), draw_canvas, NULL, 0);
        panel.ui->nodes[panel.ui->last_node].flags &= ~CUI_NF_INTERACTIVE;
        cui__close(panel.ui);
        draw_controls(panel.ui);
        cui_end_frame(panel.ui);
        cui_sw_render(panel.renderer, cui_get_draw_data(panel.ui), panel.pixels, w, h, w * 4, CUI_SW_BGRA);
        BITMAPINFO bi = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER),
                                       .biWidth = w,
                                       .biHeight = -h,
                                       .biPlanes = 1,
                                       .biBitCount = 32,
                                       .biCompression = BI_RGB}};
        SetDIBitsToDevice(dc, 0, 0, w, h, 0, 0, 0, h, panel.pixels, &bi, DIB_RGB_COLORS);
        sync_geometry();
        float wakeup = cui_next_wakeup(panel.ui);
        if (IsWindowVisible(app.window) && (cui_needs_redraw(panel.ui) || wakeup >= 0))
            SetTimer(app.window, TIMER_CUI, cui_needs_redraw(panel.ui) ? 16 : max(16, (UINT)(wakeup * 1000)), NULL);
        else KillTimer(app.window, TIMER_CUI);
    }
    if (!target)
        EndPaint(app.window, &ps);
}
static const wchar_t *tooltip(int hit) {
    switch (hit) {
    case MENU:
        return app.settings ? tr(L"返回", L"Back") : tr(L"设置", L"Settings");
    case HOME:
        return tr(L"官网", L"Project website");
    case ISSUES:
        return tr(L"反馈", L"Report an issue");
    case HIDE:
        return tr(L"隐藏到托盘", L"Hide to tray");
    case QUIT:
        return tr(L"退出软件", L"Quit");
    case PICK:
        return tr(L"取色（C 切换格式，Esc 取消）", L"Pick a color (C changes format, Esc cancels)");
    case COPY:
        return tr(L"复制颜色值", L"Copy color value");
    case FORMAT:
        return tr(L"颜色值类型", L"Color format");
    case STATUS:
        return app.registered ? tr(L"快捷键正常", L"Shortcut registered")
                              : tr(L"快捷键无效或已被占用", L"Shortcut invalid or already in use");
    case NOTICE: return tr(L"有可用更新，点击查看", L"Update available. Click to view");
    case LANGUAGE: return tr(L"界面语言", L"Interface language");
    case THEME: return tr(L"界面主题", L"Interface theme");
    case AUTO_UPDATE: return tr(L"启动时检查，此后每 24 小时检查一次", L"Check at startup, then every 24 hours");
    case CHECK_UPDATE: return update_status();
    case UPDATE_ACTION: return action_label();
    case VALUE: return tr(L"颜色值（可编辑）", L"Color value (editable)");
    case SHORTCUT: return tr(L"输入快捷键，按回车保存", L"Type a shortcut and press Enter to save");
    default:
        return tr(L"单击查看，双击复制，右键删除", L"Click to view, double-click to copy, right-click to delete");
    }
}
static void update_tips(void) {
    for (int id = MENU; id < SWATCH + HISTORY_MAX; id++) {
        TOOLINFOW t = {.cbSize = sizeof(t), .hwnd = app.window, .uId = (UINT_PTR)id, .uFlags = TTF_SUBCLASS};
        SendMessageW(panel.tips, TTM_DELTOOLW, 0, (LPARAM)&t);
        if (!visible_hit(id) || id >= SWATCH + app.count)
            continue;
        t.rect = physical(bounds(id));
        t.lpszText = (wchar_t *)tooltip(id);
        SendMessageW(panel.tips, TTM_ADDTOOLW, 0, (LPARAM)&t);
    }
}
void panel_refresh(void) {
    if (app.settings || !IsWindowVisible(app.window)) finish_history_animation();
    panel.hover = NONE;
    cui_set_theme(panel.ui, app.dark ? CUI_THEME_DARK : CUI_THEME_LIGHT);
    panel_update_changed();
    update_tips();
    RedrawWindow(app.window, NULL, NULL, RDW_INVALIDATE);
}
void panel_update_changed(void) {
    const wchar_t *notes = update_info()->notes;
    if (!notes || !*notes)
        notes = update_has_notice() ? tr(L"此版本未提供更新说明。", L"No release notes were provided for this version.")
                                   : tr(L"检查到新版本后，可在这里查看更新内容。", L"Release notes will appear here when an update is available.");
    if (notes != panel.shown_notes) {
        char *text = utf8(notes);
        if (text) { free(panel.notes); panel.notes = text; panel.shown_notes = notes; }
    }
    if (app.update_view && IsWindowVisible(app.window) && update_busy())
        SetTimer(app.window, TIMER_ACTIVITY, 40, NULL);
    else KillTimer(app.window, TIMER_ACTIVITY);
    update_tips();
    InvalidateRect(app.window, NULL, FALSE);
}
void panel_open_updates(void) {
    commit_shortcut();
    close_popups();
    app.settings = true; app.update_view = true;
    panel_layout(); show_panel(); SetFocus(app.window);
}
static bool load_icons(void) {
    int size = px(48);
    if (size == panel.icon_size)
        return true;
    unsigned char *rgba = malloc((size_t)size * size * 4);
    if (!rgba)
        return false;
    for (int i = 0; i < ICON_COUNT; i++) {
        if (!icon_pixels(i, size, rgba)) { free(rgba); return false; }
        cui_texture texture = panel.iface.texture_create(panel.iface.user, size, size, CUI_TEXTURE_RGBA8, rgba);
        if (!texture) { free(rgba); return false; }
        if (panel.icons[i])
            panel.iface.texture_destroy(panel.iface.user, panel.icons[i]);
        panel.icons[i] = texture;
    }
    free(rgba);
    panel.icon_size = size;
    return true;
}
static void sync_geometry(void) {
    bool expanded = !app.settings && panel.menu_pending;
    for (int i = 0; !app.settings && i < panel.ui->popup_count; i++)
        if (panel.ui->popups[i].open || panel.ui->popups[i].s.x > .01f) expanded = true;
    panel.menu_extent = expanded;
    int height = expanded ? MENU_HEIGHT : panel_height();
    int width = panel_width();
    RECT client; GetClientRect(app.window, &client);
    if (client.bottom != px(height) || client.right != px(width)) {
        RECT rect; GetWindowRect(app.window, &rect);
        MONITORINFO monitor = {.cbSize = sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(app.window, MONITOR_DEFAULTTONEAREST), &monitor);
        int x = max(monitor.rcWork.left, min(rect.left, monitor.rcWork.right - px(width)));
        int y = max(monitor.rcWork.top, min(rect.top, monitor.rcWork.bottom - px(height)));
        SetWindowPos(app.window, NULL, x, y, px(width), px(height), SWP_NOZORDER | SWP_NOACTIVATE);
        update_tips();
    }
    HRGN region = CreateRoundRectRgn(0, 0, px(width) + 1, px(panel_height()) + 1, px(16), px(16));
    if (expanded) {
        for (int i = 0; i < panel.ui->popup_count; i++) {
            cui_popup *popup = &panel.ui->popups[i];
            if (!popup->open && popup->s.x <= .01f) continue;
            RECT r = physical(cui_get_rect(panel.ui, popup->id));
            HRGN menu = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, px(20), px(20));
            CombineRgn(region, region, menu, RGN_OR); DeleteObject(menu);
        }
    }
    HRGN current = CreateRectRgn(0, 0, 0, 0);
    if (GetWindowRgn(app.window, current) != ERROR && EqualRgn(current, region)) DeleteObject(region);
    else SetWindowRgn(app.window, region, FALSE);
    DeleteObject(current);
}
void panel_layout(void) {
    load_icons();
    update_main_width();
    RECT rect; GetWindowRect(app.window, &rect);
    MONITORINFO monitor = {.cbSize = sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(app.window, MONITOR_DEFAULTTONEAREST), &monitor);
    int width = px(panel_width()), height = px(panel.menu_extent && !app.settings ? MENU_HEIGHT : panel_height());
    int x = max(monitor.rcWork.left, min(rect.left, monitor.rcWork.right - width));
    int y = max(monitor.rcWork.top, min(rect.top, monitor.rcWork.bottom - height));
    SetWindowPos(app.window, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    sync_geometry(); panel_refresh();
}
static void commit_shortcut(void) {
    if (!panel.shortcut_dirty) return;
    wchar_t input[64] = L"";
    MultiByteToWideChar(CP_UTF8, 0, panel.shortcut, -1, input, ARRAYSIZE(input));
    app.registered = set_hotkey(input, true);
    panel.shortcut_dirty = false;
    update_tips(); InvalidateRect(app.window, NULL, FALSE);
}
bool panel_init(void) {
    panel.renderer = cui_sw_create();
    if (!panel.renderer) return false;
    cui_sw_set_threads(panel.renderer, 1);
    panel.iface = cui_sw_iface(panel.renderer);
    panel.ui = cui_create(&(cui_ctx_desc){.renderer = panel.iface, .no_default_fonts = true});
    if (!panel.ui || !load_icons() || !load_font()) return false;
    cui_platform_iface platform = {.set_clipboard = set_clipboard, .get_clipboard = get_clipboard, .set_text_input_rect = ime_caret};
    cui_set_platform(panel.ui, &platform);
    WideCharToMultiByte(CP_UTF8, 0, app.hotkey, -1, panel.shortcut, sizeof(panel.shortcut), NULL, NULL);
    panel.tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                app.window, NULL, app.instance, NULL);
    SendMessageW(panel.tips, TTM_SETMAXTIPWIDTH, 0, px(320));
    refresh_value(); panel_layout();
    return true;
}
void panel_destroy(void) {
    finish_history_animation();
    KillTimer(app.window, TIMER_CUI);
    if (panel.ui) {
        for (int i = 0; i < ICON_COUNT; i++)
            if (panel.icons[i]) panel.iface.texture_destroy(panel.iface.user, panel.icons[i]);
        cui_destroy(panel.ui);
    }
    if (panel.renderer) cui_sw_destroy(panel.renderer);
    free(panel.pixels); free(panel.notes); free(panel.clipboard);
}
static void activate(int hit) {
    switch (hit) {
    case MENU:
        commit_shortcut(); close_popups();
        if (app.update_view)
            app.update_view = false;
        else
            app.settings = !app.settings;
        if (app.settings) {
            WideCharToMultiByte(CP_UTF8, 0, app.hotkey, -1, panel.shortcut, sizeof(panel.shortcut), NULL, NULL);
            app.registered = set_hotkey(app.hotkey, false);
        }
        panel_layout();
        break;
    case HOME:
    case ISSUES:
        ShellExecuteW(app.window, L"open",
                      hit == HOME ? L"https://github.com/Hunlongyu/color-picker"
                                  : L"https://github.com/Hunlongyu/color-picker/issues",
                      NULL, NULL, SW_SHOWNORMAL);
        break;
    case HIDE:
        ShowWindow(app.window, SW_HIDE);
        break;
    case QUIT:
        PostMessageW(app.window, WM_CLOSE, 0, 0);
        break;
    case PICK:
        start_picker();
        break;
    case COPY:
        copy_value();
        break;
    case NOTICE:
        panel_open_updates();
        break;
    case CHECK_UPDATE:
        start_update(true);
        break;
    case UPDATE_ACTION:
        update_action();
        break;
    case AUTO_UPDATE:
        app.auto_update = !app.auto_update;
        save_option(L"autoUpdate", app.auto_update ? L"1" : L"0");
        panel_refresh();
        if (app.auto_update)
            start_update(false);
        break;
    default:
        if (hit >= SWATCH && hit < SWATCH + app.count) {
            app.color = app.history[hit - SWATCH];
            app.has_color = true;
            refresh_value();
        }
    }
}
static cui_key input_key(WPARAM key) {
    if (key >= 'A' && key <= 'Z') return (cui_key)(CUI_KEY_A + key - 'A');
    if (key >= '0' && key <= '9') return (cui_key)(CUI_KEY_0 + key - '0');
    if (key >= VK_F1 && key <= VK_F12) return (cui_key)(CUI_KEY_F1 + key - VK_F1);
    switch (key) {
    case VK_TAB: return CUI_KEY_TAB; case VK_RETURN: return CUI_KEY_ENTER;
    case VK_ESCAPE: return CUI_KEY_ESCAPE; case VK_BACK: return CUI_KEY_BACKSPACE;
    case VK_DELETE: return CUI_KEY_DELETE; case VK_SPACE: return CUI_KEY_SPACE;
    case VK_LEFT: return CUI_KEY_LEFT; case VK_RIGHT: return CUI_KEY_RIGHT;
    case VK_UP: return CUI_KEY_UP; case VK_DOWN: return CUI_KEY_DOWN;
    case VK_HOME: return CUI_KEY_HOME; case VK_END: return CUI_KEY_END;
    case VK_PRIOR: return CUI_KEY_PAGE_UP; case VK_NEXT: return CUI_KEY_PAGE_DOWN;
    default: return CUI_KEY_NONE;
    }
}
static int input_mods(void) {
    return (GetKeyState(VK_SHIFT) < 0 ? CUI_MOD_SHIFT : 0) |
           (GetKeyState(VK_CONTROL) < 0 ? CUI_MOD_CTRL : 0) |
           (GetKeyState(VK_MENU) < 0 ? CUI_MOD_ALT : 0) |
           (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0 ? CUI_MOD_SUPER : 0);
}
static void input_frame(void) {
    RedrawWindow(app.window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}
static void prepare_menu(void) {
    if (!app.settings && !panel.menu_extent) {
        panel.menu_pending = panel.menu_extent = true;
        panel_layout();
    }
}
bool panel_message(UINT msg, WPARAM w, LPARAM l, LRESULT *result) {
    *result = 0;
    switch (msg) {
    case MSG_PANEL_ACTION:
        activate((int)w);
        break;
    case MSG_PANEL_REFRESH:
        tray_update_changed(); panel_refresh();
        break;
    case WM_TIMER:
        if (w == TIMER_CUI) {
            KillTimer(app.window, TIMER_CUI);
            InvalidateRect(app.window, NULL, FALSE);
        } else if (w == TIMER_ACTIVITY) {
            RECT r = physical((cui_rect){16, 424, 340, 4});
            InvalidateRect(app.window, &r, FALSE);
        } else if (w == TIMER_HISTORY) {
            if (app.settings || !IsWindowVisible(app.window)) finish_history_animation();
            RECT r = physical((cui_rect){2, 78, (float)panel_width() - 4, 49});
            InvalidateRect(app.window, &r, FALSE);
        } else return false;
        break;
    case WM_SHOWWINDOW:
        if (!w) {
            finish_history_animation(); commit_shortcut(); close_popups();
            cui_input_focus(panel.ui, false);
            KillTimer(app.window, TIMER_ACTIVITY); KillTimer(app.window, TIMER_CUI);
            sync_geometry();
        } else {
            if (app.update_view && update_busy()) SetTimer(app.window, TIMER_ACTIVITY, 40, NULL);
            InvalidateRect(app.window, NULL, FALSE);
        }
        return false;
    case WM_PAINT: paint(NULL); break;
    case WM_PRINTCLIENT: paint((HDC)w); break;
    case WM_ERASEBKGND: *result = 1; break;
    case WM_SETFOCUS:
        cui_input_focus(panel.ui, true); InvalidateRect(app.window, NULL, FALSE); break;
    case WM_KILLFOCUS:
        commit_shortcut(); close_popups(); cui_input_focus(panel.ui, false);
        sync_geometry(); InvalidateRect(app.window, NULL, FALSE); break;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP: {
        bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        if (w == VK_F4 && (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)) return false;
        if (down && w == VK_ESCAPE && !popup_active()) {
            if (panel.ui->focus == control_id(SHORTCUT)) {
                WideCharToMultiByte(CP_UTF8, 0, app.hotkey, -1, panel.shortcut, sizeof(panel.shortcut), NULL, NULL);
                panel.shortcut_dirty = false; panel.ui->focus = 0; input_frame(); break;
            }
            if (!cui_wants_keyboard(panel.ui)) {
                if (app.settings) activate(MENU); else ShowWindow(app.window, SW_HIDE);
                break;
            }
        }
        if (down && panel.ui->focus == cui_make_id(panel.ui, "format") &&
            (w == VK_RETURN || w == VK_SPACE || w == VK_DOWN || w == VK_UP))
            prepare_menu();
        cui_input_key(panel.ui, input_key(w), down, input_mods());
        input_frame();
        if (down) { panel.menu_pending = false; sync_geometry(); }
        break;
    }
    case WM_CHAR: {
        wchar_t chars[3] = {0}; wchar_t c = (wchar_t)w;
        if (c >= 0xd800 && c <= 0xdbff) { panel.surrogate = c; break; }
        if (c < 32 || c == 127) break;
        if (c >= 0xdc00 && c <= 0xdfff) {
            if (!panel.surrogate) break;
            chars[0] = panel.surrogate; chars[1] = c;
        } else chars[0] = c;
        panel.surrogate = 0;
        char text[12];
        if (WideCharToMultiByte(CP_UTF8, 0, chars, -1, text, sizeof(text), NULL, NULL))
            cui_input_text(panel.ui, text);
        input_frame(); break;
    }
    case WM_IME_COMPOSITION:
        if (l & GCS_RESULTSTR) {
            HIMC imc = ImmGetContext(app.window);
            LONG bytes = imc ? ImmGetCompositionStringW(imc, GCS_RESULTSTR, NULL, 0) : 0;
            wchar_t *text = bytes > 0 ? calloc((size_t)bytes + sizeof(wchar_t), 1) : NULL;
            if (text) {
                ImmGetCompositionStringW(imc, GCS_RESULTSTR, text, (DWORD)bytes);
                char *input = utf8(text); if (input) { cui_input_text(panel.ui, input); free(input); }
                free(text);
            }
            if (imc) ImmReleaseContext(app.window, imc);
            input_frame(); break;
        }
        return false;
    case WM_MOUSEWHEEL:
        cui_input_scroll(panel.ui, 0, (float)GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA);
        input_frame(); break;
    case WM_MOUSEMOVE: {
        cui_input_mouse_move(panel.ui, (float)GET_X_LPARAM(l) * 96 / app.dpi, (float)GET_Y_LPARAM(l) * 96 / app.dpi);
        panel.hover = popup_active() ? NONE : hit_test(l);
        InvalidateRect(app.window, NULL, FALSE);
        SetCursor(LoadCursorW(NULL, panel.hover == VALUE || panel.hover == SHORTCUT ? IDC_IBEAM :
                              panel.hover && panel.hover != STATUS ? IDC_HAND : IDC_ARROW));
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, app.window, 0}; TrackMouseEvent(&track);
        break;
    }
    case WM_MOUSELEAVE:
        panel.hover = NONE; cui_input_mouse_leave(panel.ui); InvalidateRect(app.window, NULL, FALSE); break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        SetFocus(app.window);
        int hit = hit_test(l);
        if (!hit && GET_Y_LPARAM(l) < px(31) && !popup_active()) {
            commit_shortcut(); SendMessageW(app.window, WM_NCLBUTTONDOWN, HTCAPTION, 0); break;
        }
        bool double_swatch = msg == WM_LBUTTONDBLCLK && hit >= SWATCH && !popup_active();
        if (hit == FORMAT) prepare_menu();
        cui_input_mouse_move(panel.ui, (float)GET_X_LPARAM(l) * 96 / app.dpi, (float)GET_Y_LPARAM(l) * 96 / app.dpi);
        cui_input_mouse_button(panel.ui, CUI_MOUSE_LEFT, true);
        SetCapture(app.window); input_frame();
        if (double_swatch) { activate(hit); copy_value(); }
        break;
    }
    case WM_LBUTTONUP:
        cui_input_mouse_move(panel.ui, (float)GET_X_LPARAM(l) * 96 / app.dpi, (float)GET_Y_LPARAM(l) * 96 / app.dpi);
        cui_input_mouse_button(panel.ui, CUI_MOUSE_LEFT, false);
        panel.menu_pending = false; input_frame();
        panel.ui->active = 0;
        if (GetCapture() == app.window) ReleaseCapture();
        break;
    case WM_CAPTURECHANGED:
        cui_input_mouse_button(panel.ui, CUI_MOUSE_LEFT, false);
        panel.ui->active = 0;
        break;
    case WM_RBUTTONDOWN:
        panel.suppress_history = popup_active();
        cui_input_mouse_move(panel.ui, (float)GET_X_LPARAM(l) * 96 / app.dpi, (float)GET_Y_LPARAM(l) * 96 / app.dpi);
        cui_input_mouse_button(panel.ui, CUI_MOUSE_RIGHT, true); input_frame(); break;
    case WM_RBUTTONUP: {
        cui_input_mouse_button(panel.ui, CUI_MOUSE_RIGHT, false);
        int index = hit_test(l) - SWATCH;
        if (!panel.suppress_history && index >= 0 && index < app.count) {
            animate_history_removal(index); app.count--;
            memmove(app.history + index, app.history + index + 1, (size_t)(app.count - index) * sizeof(COLORREF));
            save_history(); panel_refresh();
        }
        panel.suppress_history = false; input_frame(); break;
    }
    default: return false;
    }
    return true;
}
