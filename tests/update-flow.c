/* Offline integration test. Only HTTP transport is replaced; release parsing,
   SHA-256, download cleanup, UI dispatch, and EXE replacement use real code. */
#include "../src/app.h"
#include <stdio.h>
#include <stdlib.h>
#include "au_internal.h"
static wchar_t opened_link[256];
static HINSTANCE WINAPI capture_link(HWND window, LPCWSTR verb, LPCWSTR path, LPCWSTR args, LPCWSTR dir, INT show) {
    (void)window; (void)verb; (void)args; (void)dir; (void)show;
    wcsncpy(opened_link, path, ARRAYSIZE(opened_link) - 1);
    return (HINSTANCE)(INT_PTR)42;
}
#define ShellExecuteW capture_link
#include "../src/panel.c"
#undef ShellExecuteW
#include "../src/main.c"
static LANGID system_language = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
static DWORD system_light = 1;
static bool theme_unavailable;
static LANGID WINAPI test_ui_language(void) { return system_language; }
static LSTATUS WINAPI test_theme(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD flags,
                                LPDWORD type, PVOID data, LPDWORD size) {
    (void)key; (void)subkey; (void)flags;
    if (theme_unavailable || wcscmp(value, L"AppsUseLightTheme")) return ERROR_FILE_NOT_FOUND;
    if (*size < sizeof(DWORD)) return ERROR_MORE_DATA;
    memcpy(data, &system_light, sizeof(DWORD)); *size = sizeof(DWORD);
    if (type) *type = REG_DWORD;
    return ERROR_SUCCESS;
}
static BOOL no_clipboard(HWND window) { (void)window; return FALSE; }
static BOOL no_beep(UINT kind) { (void)kind; return TRUE; }
#define OpenClipboard no_clipboard
#define MessageBeep no_beep
#define GetUserDefaultUILanguage test_ui_language
#define RegGetValueW test_theme
#include "../src/state.c"
#undef GetUserDefaultUILanguage
#undef RegGetValueW
#undef OpenClipboard
#undef MessageBeep
#include "../src/update.c"

static int failures, fixture, transfers;
static bool corrupt, unknown_size;
static char digest[65];
static const char release_notes[] = "## What's new\n\n- Lucide SVG icons\n- Updated settings\n- Download progress and restart\n\nChinese notes: \\u65b0\\u7248\\u66f4\\u65b0\\u8bf4\\u660e";
static void check(bool ok, const char *name) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name); fflush(stdout);
    if (!ok) failures++;
}
au_err_t __wrap_au_http_get_mem(au_ctx_t *ctx, const wchar_t *url, char **out, size_t *len, au_http_meta *meta) {
    (void)ctx; (void)url;
    Sleep(60);
    if (fixture == 1) return AU_ERR_NET;
    au_version_t current;
    au_version_parse(APP_VERSION, &current);
    char newer[64];
    snprintf(newer, sizeof(newer), "v%d.%d.%d", current.major, current.minor, current.patch + 1);
    const char *version = fixture == 2 ? APP_VERSION : newer;
    char notes[2048]; size_t n = 0;
    for (size_t i = 0; release_notes[i]; i++) {
        if (release_notes[i] == '\n') { notes[n++] = '\\'; notes[n++] = 'n'; }
        else notes[n++] = release_notes[i];
    }
    notes[n] = 0;
    char json[8192];
    snprintf(json, sizeof(json), "[{\"tag_name\":\"%s\",\"draft\":false,\"prerelease\":false,\"body\":\"%s\",\"assets\":[{\"name\":\"color-picker-x64.exe\",\"browser_download_url\":\"https://github.com/Hunlongyu/color-picker/releases/download/v0.3.1/color-picker-x64.exe\",\"digest\":\"sha256:%s\"}]}]", version, notes, fixture == 3 ? "invalid" : digest);
    *len = strlen(json); *out = _strdup(json);
    memset(meta, 0, sizeof(*meta)); meta->status = 200;
    return AU_OK;
}
au_err_t __wrap_au_http_get_file(au_ctx_t *ctx, const wchar_t *url, HANDLE target, au_http_meta *meta) {
    (void)url;
    FILE *file = fopen("next.bin", "rb"); if (!file) return AU_ERR_IO;
    transfers++;
    fseek(file, 0, SEEK_END); uint64_t total = (uint64_t)ftell(file); rewind(file);
    char buffer[4096]; size_t n; uint64_t done = 0; DWORD written;
    while ((n = fread(buffer, 1, sizeof(buffer), file))) {
        if (InterlockedCompareExchange(&ctx->cancel_flag, 0, 0)) { fclose(file); return AU_ERR_CANCELED; }
        if (corrupt && !done) buffer[0] ^= 1;
        if (!WriteFile(target, buffer, (DWORD)n, &written, NULL) || written != n) { fclose(file); return AU_ERR_IO; }
        done += n;
        if (ctx->on_progress) ctx->on_progress(unknown_size ? 0 : total, done, ctx->userdata);
        Sleep(8);
    }
    fclose(file); memset(meta, 0, sizeof(*meta)); return AU_OK;
}
static void pump(DWORD duration) {
    DWORD start = GetTickCount(); MSG msg;
    do {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(2);
    } while (GetTickCount() - start < duration);
}
static void settle(void) {
    DWORD start = GetTickCount();
    while (update_busy() && GetTickCount() - start < 15000) pump(10);
    check(!update_busy(), "worker completes without blocking UI");
}
static void click_hit(int hit) {
    RECT r = physical(bounds(hit)); LPARAM point = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(app.window, WM_LBUTTONUP, 0, point);
    pump(10);
}
static void snapshot(const char *name) {
    RECT r; GetClientRect(app.window, &r);
    HDC screen = GetDC(app.window), dc = CreateCompatibleDC(screen);
    BITMAPINFO bi = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER), .biWidth = r.right,
        .biHeight = -r.bottom, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
    BYTE *pixels; HBITMAP bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    HGDIOBJ old = SelectObject(dc, bitmap);
    SendMessageW(app.window, WM_PRINT, (WPARAM)dc, PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    GdiFlush();
    for (int i = 0; i < r.right * r.bottom; i++) {
        BYTE blue = pixels[i*4]; pixels[i*4] = pixels[i*4+2]; pixels[i*4+2] = blue; pixels[i*4+3] = 255;
    }
    check(cui_write_png(name, pixels, r.right, r.bottom, r.right * 4), name);
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(app.window, screen);
}
static char copied[256];
static void test_copy(void *unused, const char *text) { (void)unused; snprintf(copied, sizeof(copied), "%s", text); }
static const char *test_paste(void *unused) { (void)unused; return "#00AACC"; }
static void key_tap(WPARAM key, bool ctrl) {
    BYTE keys[256] = {0}; if (ctrl) keys[VK_CONTROL] = 0x80;
    SetKeyboardState(keys);
    SendMessageW(app.window, WM_KEYDOWN, key, 0); SendMessageW(app.window, WM_KEYUP, key, 0);
    ZeroMemory(keys, sizeof(keys)); SetKeyboardState(keys);
    if (ctrl) SendMessageW(app.window, WM_KEYUP, VK_CONTROL, 0);
    pump(20);
}
static void type_text(const wchar_t *text) {
    for (; *text; text++) SendMessageW(app.window, WM_CHAR, *text, 0);
    pump(20);
}
static void choose_item(const char *text) {
    pump(250);
    for (int i = panel.ui->node_count - 1; i >= 0; i--) {
        cui_node *n = &panel.ui->nodes[i];
        if (!n->text || strcmp(n->text, text) || n->parent < 0) continue;
        cui_node *item = &panel.ui->nodes[n->parent];
        if (!(item->flags & CUI_NF_INTERACTIVE)) continue;
        RECT r = physical(item->rect); LPARAM p = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
        SendMessageW(app.window, WM_LBUTTONDOWN, MK_LBUTTON, p);
        SendMessageW(app.window, WM_LBUTTONUP, 0, p); pump(400); return;
    }
    check(false, "requested CUI menu item exists");
}
static void appearance_check(const wchar_t *directory) {
    App saved = app;
    swprintf(app.ini, MAX_PATH, L"%ls\\appearance-options.ini", directory);
    load_appearance();
    check(app.language == LANGUAGE_SYSTEM && app.theme == THEME_SYSTEM && !app.english && !app.dark,
          "new preferences default to system language and theme");
    system_language = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US); system_light = 0;
    check(refresh_appearance() && app.english && app.dark, "system mode resolves current Windows appearance");
    set_language(LANGUAGE_ZH); set_theme(THEME_LIGHT);
    load_appearance();
    check(app.language == LANGUAGE_ZH && app.theme == THEME_LIGHT && !app.english && !app.dark,
          "existing zh-CN and light preferences remain explicit overrides");
    set_language(LANGUAGE_EN); set_theme(THEME_DARK);
    system_language = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED); system_light = 1;
    check(!refresh_appearance() && app.english && app.dark, "manual choices ignore system changes");
    load_appearance();
    check(app.language == LANGUAGE_EN && app.theme == THEME_DARK, "existing en and dark values reload correctly");
    set_language(LANGUAGE_SYSTEM); set_theme(THEME_SYSTEM);
    wchar_t language[32], theme[32];
    GetPrivateProfileStringW(L"settings", L"language", L"", language, ARRAYSIZE(language), app.ini);
    GetPrivateProfileStringW(L"settings", L"theme", L"", theme, ARRAYSIZE(theme), app.ini);
    check(!wcscmp(language, L"system") && !wcscmp(theme, L"system"), "system selections persist as system in INI");
    load_appearance();
    check(app.language == LANGUAGE_SYSTEM && app.theme == THEME_SYSTEM && !app.english && !app.dark,
          "system selections survive reload without becoming explicit values");
    theme_unavailable = true; system_light = 0; refresh_appearance();
    check(!app.dark, "missing system theme setting falls back to light");
    system_language = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT); refresh_appearance();
    check(app.english, "unsupported system languages fall back to English");
    theme_unavailable = false; system_light = 1;
    system_language = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
    app = saved; load_appearance();
}
static void responsive_check(void) {
    pump(60);
    RECT client; GetClientRect(app.window, &client);
    int hex_width = client.right;
    check(!app.count && client.bottom == px(PANEL_COMPACT_HEIGHT) && client.right < px(PANEL_WIDTH),
          "empty startup uses two rows and compact HEX width");
    snapshot("panel-empty.png");
    click_hit(FORMAT); choose_item("HWB");
    GetClientRect(app.window, &client);
    check(client.right > hex_width && client.bottom == px(PANEL_COMPACT_HEIGHT),
          "empty panel dropdown expands for long formats and returns to two rows");
    click_hit(FORMAT); choose_item("HEX");
    remember_color(RGB(14, 195, 65)); pump(280);
    GetClientRect(app.window, &client);
    check(app.count == 1 && client.bottom == px(PANEL_HEIGHT), "first picked color reveals history row");
    snapshot("panel-first-color.png");
    static const char *const names[] = {"panel-hex.png", "panel-hsl.png", "panel-rgb.png", "panel-cmyk.png", "panel-hwb.png"};
    for (int i = 0; i < FORMAT_COUNT; i++) {
        app.format = i; refresh_value(); pump(50);
        GetClientRect(app.window, &client);
        cui_rect input = cui_get_rect(panel.ui, control_id(VALUE)), format = cui_get_rect(panel.ui, cui_make_id(panel.ui, "format"));
        float text = cui_measure_text(panel.ui, CUI_FONT_BODY, 16, panel_value(), -1).width;
        check(input.w >= text + 24 && input.x + input.w <= bounds(COPY).x + 1 &&
              format.x + format.w <= (float)client.right * 96.f / app.dpi - 7,
              "color text, copy button and format menu fit adaptive width");
        snapshot(names[i]);
    }
    wchar_t long_value[512];
    for (int i = 0; i < 511; i++) long_value[i] = L'W';
    long_value[511] = 0; panel_set_value(long_value); InvalidateRect(app.window, NULL, FALSE); pump(50);
    GetClientRect(app.window, &client);
    check(client.right == px(panel.main_max_width), "long edited values stop at maximum width");
    app.format = HEX; refresh_value(); pump(50);
    GetClientRect(app.window, &client); check(client.right == hex_width, "HEX restores compact width");
    click_hit(MENU); GetClientRect(app.window, &client);
    check(client.right == px(PANEL_WIDTH), "settings retains its full width");
    click_hit(MENU); GetClientRect(app.window, &client);
    check(client.right == hex_width, "return from settings restores adaptive main width");
    RECT r = physical(bounds(SWATCH)); LPARAM point = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    SendMessageW(app.window, WM_RBUTTONDOWN, MK_RBUTTON, point);
    SendMessageW(app.window, WM_RBUTTONUP, 0, point);
    GetClientRect(app.window, &client);
    check(!app.count && panel.history_removed_count && client.bottom == px(PANEL_HEIGHT),
          "last color stays visible during deletion animation");
    pump(300); GetClientRect(app.window, &client);
    check(!app.count && !panel.history_removed_count && client.bottom == px(PANEL_COMPACT_HEIGHT),
          "history row collapses after last deletion finishes");
    snapshot("panel-empty-after-delete.png");
    app.dpi = 144; panel_layout(); pump(50); GetClientRect(app.window, &client);
    check(client.right == MulDiv(hex_width, 144, 96) && client.bottom == px(PANEL_COMPACT_HEIGHT),
          "compact main panel scales at 150 percent DPI");
    snapshot("panel-empty-150.png");
    app.dpi = 96; app.has_color = false; refresh_value(); panel_layout(); pump(50);
}
static void controls_check(void) {
    cui_platform_iface platform = {.set_clipboard = test_copy, .get_clipboard = test_paste, .set_text_input_rect = ime_caret};
    cui_set_platform(panel.ui, &platform);
    click_hit(VALUE); key_tap('A', true); type_text(L"#123456");
    check(!strcmp(panel_value(), "#123456"), "CUI input accepts replacement text through WM_CHAR");
    key_tap('A', true); key_tap('C', true);
    check(!strcmp(copied, "#123456"), "CUI Ctrl+C copies selection");
    key_tap('V', true); check(!strcmp(panel_value(), "#00AACC"), "CUI Ctrl+V pastes text");
    key_tap('Z', true); check(!strcmp(panel_value(), "#123456"), "CUI input supports undo");
    key_tap('A', true); type_text(L"颜色值 Ω");
    check(!strcmp(panel_value(), "颜色值 Ω"), "CUI input preserves Unicode");
    refresh_value(); input_frame();
    click_hit(FORMAT); pump(250);
    check(popup_active() && panel.menu_extent, "main CUI dropdown opens beyond compact panel");
    snapshot("main-format-open.png");
    choose_item("HWB");
    RECT client; GetClientRect(app.window, &client);
    check(app.format == HWB && client.bottom == px(PANEL_HEIGHT), "mouse selects last option and compact window size returns");
    click_hit(FORMAT); key_tap(VK_ESCAPE, false); pump(400);
    check(!popup_active() && IsWindowVisible(app.window), "Escape dismisses dropdown without hiding panel");
    app.format = HEX; refresh_value(); input_frame();
    click_hit(MENU); pump(200);
    click_hit(SHORTCUT); key_tap('A', true); type_text(L"Ctrl+Shift+F9"); key_tap(VK_RETURN, false);
    check(app.registered && !wcscmp(app.hotkey, L"Ctrl+Shift+F9"), "CUI shortcut input commits on Enter");
    key_tap('A', true); type_text(L"bad shortcut"); key_tap(VK_ESCAPE, false);
    check(!strcmp(panel.shortcut, "Ctrl+Shift+F9") && app.registered, "Escape restores saved shortcut");
    click_hit(FORMAT); key_tap(VK_DOWN, false); key_tap(VK_RETURN, false); pump(350);
    check(app.default_format == HSL, "keyboard selects CUI default format");
    click_hit(LANGUAGE); choose_item("English");
    check(app.english, "CUI language menu switches language");
    click_hit(THEME); choose_item("Dark");
    check(app.dark, "CUI theme menu switches theme");
    snapshot("settings-cui-dark-en.png");
    click_hit(HOME); check(!wcscmp(opened_link, L"https://github.com/Hunlongyu/color-picker"), "About website row opens project URL");
    click_hit(ISSUES); check(!wcscmp(opened_link, L"https://github.com/Hunlongyu/color-picker/issues"), "About feedback row opens issues URL");
    SendMessageW(app.window, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet"); pump(20);
    check(app.english && app.dark, "settings notification preserves manual language and theme");
    click_hit(LANGUAGE); choose_item("System default");
    check(app.language == LANGUAGE_SYSTEM && !app.english && app.dark, "CUI language can independently follow system");
    click_hit(THEME); choose_item("系统默认");
    check(app.theme == THEME_SYSTEM && !app.dark, "CUI theme can follow system");
    snapshot("settings-system.png");
    system_language = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US); system_light = 0;
    SendMessageW(app.window, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet"); pump(50);
    check(app.english && app.dark && app.language == LANGUAGE_SYSTEM && app.theme == THEME_SYSTEM,
          "system change updates visible appearance while keeping system options selected");
    snapshot("settings-system-dark-en.png");
    system_language = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED); system_light = 1;
    SendMessageW(app.window, WM_THEMECHANGED, 0, 0); pump(50);
    check(!app.english && !app.dark, "theme notification refreshes followed appearance");
    app.format = app.default_format = HEX;
    panel_refresh(); refresh_value(); click_hit(MENU); pump(200);
}
int wmain(int argc, wchar_t **argv) {
    if (argc < 2) return 2;
    SetCurrentDirectoryW(argv[1]);
    wchar_t desktop_name[80]; swprintf(desktop_name, 80, L"CPUpdateQA%lu", GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(desktop_name, NULL, NULL, 0, GENERIC_ALL, NULL);
    if (!desktop || !SetThreadDesktop(desktop)) return 2;
    app.instance = GetModuleHandleW(NULL); app.dpi = 96;
    swprintf(app.ini, MAX_PATH, L"%ls\\color-picker.ini", argv[1]);
    appearance_check(argv[1]);
    wcscpy(app.hotkey, L"Ctrl+Shift+F8"); app.registered = true; app.auto_update = true;
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_WIN95_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSW cls = {.style = CS_DBLCLKS, .hInstance = app.instance, .lpfnWndProc = main_proc,
                     .lpszClassName = L"CPUpdatePanelQA"}; RegisterClassW(&cls);
    app.window = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"", WS_POPUP | WS_CLIPCHILDREN,
                                0, 0, PANEL_WIDTH, PANEL_HEIGHT, NULL, NULL, app.instance, NULL);
    if (!panel_init()) return 3;
    panel_ready = true; ShowWindow(app.window, SW_SHOWNOACTIVATE);
    check(au_sha256_file(L"next.bin", digest) == AU_OK, "fixture SHA-256 computed");
    responsive_check();
    const COLORREF swatches[] = {RGB(108,88,179), RGB(158,89,185), RGB(207,90,143), RGB(231,102,106),
        RGB(240,144,96), RGB(239,183,97), RGB(218,204,114), RGB(166,191,130), RGB(107,170,151),
        RGB(98,159,178), RGB(96,134,176), RGB(8,127,112)};
    for (int i = 0; i < 12; i++) remember_color(swatches[i]);
    pump(260);
    RECT main_client; GetClientRect(app.window, &main_client);
    cui_rect last_swatch = bounds(SWATCH + HISTORY_MAX - 1);
    check(app.count == HISTORY_MAX && last_swatch.x + last_swatch.w <= (float)main_client.right - 7 + .01f,
          "all twelve history colors fit the compact width");
    controls_check();
    snapshot("panel.png");
    click_hit(MENU); snapshot("settings-light.png");
    check(panel_height() == SETTINGS_HEIGHT && cui_get_rect(panel.ui, control_id(SHORTCUT)).w > 100,
          "settings expands and shows CUI shortcut input");
    system_language = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US); system_light = 0;
    refresh_appearance(); panel_refresh(); snapshot("settings-dark-en.png");
    system_language = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED); system_light = 1;
    refresh_appearance(); panel_refresh();
    click_hit(MENU);
    fixture = 1; start_update(false); settle();
    check(info.phase == UPDATE_FAILED && !app.settings && !update_has_notice(), "automatic failure is quiet");
    fixture = 2; click_hit(MENU); click_hit(CHECK_UPDATE); settle();
    check(info.phase == UPDATE_CURRENT && app.update_view && !update_has_notice(), "manual check reports current version");
    snapshot("update-current.png");
    fixture = 3; start_update(true); settle();
    check(info.phase == UPDATE_FAILED && !update_has_notice(), "invalid release digest cannot enable installation");
    click_hit(MENU); click_hit(MENU);
    fixture = 0; ShowWindow(app.window, SW_HIDE); start_update(false); settle();
    check(info.phase == UPDATE_AVAILABLE && !IsWindowVisible(app.window) && !app.settings && transfers == 0,
          "startup check marks update without showing panel or downloading");
    check(update_has_notice() && wcsstr(info.notes, L"新版更新说明"), "badge and Unicode release notes are available");
    show_panel(); snapshot("panel-update-badge.png"); click_hit(NOTICE);
    check(app.update_view && cui_get_rect(panel.ui, cui_make_id(panel.ui, "release-notes")).h > 0,
          "update icon opens CUI release notes");
    snapshot("update-available.png");
    click_hit(UPDATE_ACTION); pump(40); update_progress();
    check(info.phase == UPDATE_DOWNLOADING && info.percent > 0 && info.percent < 100, "download reports intermediate progress");
    snapshot("update-progress.png"); click_hit(UPDATE_ACTION); settle();
    check(info.phase == UPDATE_CANCELED && update_has_notice(), "cancel keeps release available for retry");
    wchar_t temp[MAX_PATH + 16]; GetModuleFileNameW(NULL, temp, MAX_PATH); wcscat(temp, L".autmp");
    check(GetFileAttributesW(temp) == INVALID_FILE_ATTRIBUTES, "cancel removes partial download");
    corrupt = true; click_hit(UPDATE_ACTION); settle();
    check(info.phase == UPDATE_FAILED && job.result == AU_ERR_HASH, "hash mismatch blocks installation");
    check(GetFileAttributesW(temp) == INVALID_FILE_ATTRIBUTES, "hash failure removes download");
    snapshot("update-error.png");
    corrupt = false; unknown_size = true; click_hit(UPDATE_ACTION); pump(40);
    check(info.phase == UPDATE_DOWNLOADING && info.percent == -1, "unknown content length uses indeterminate progress");
    settle(); check(info.phase == UPDATE_READY && info.percent == 100, "verified download waits for restart action");
    check(update_has_notice() && !wcscmp(action_label(), L"重启安装"), "ready state offers explicit restart");
    snapshot("update-ready.png");
    system_language = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US); system_light = 0; refresh_appearance();
    app.dpi = 144; panel_layout(); snapshot("update-ready-dark-150.png");
    RECT client; GetClientRect(app.window, &client);
    check(client.right == 558 && client.bottom == 732 && panel.icon_size == 72, "DPI change rerasterizes SVGs and resizes details");
    if (argc > 2 && !wcscmp(argv[2], L"--install")) {
        check(failures == 0, "pre-install checks pass");
        if (failures) return 1;
        update_action();
        check(false, "installer must replace and restart the test process");
    }
    update_destroy(); fixture = 0; app.dpi = 96;
    system_language = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED); system_light = 1; refresh_appearance();
    start_update(true); settle(); update_action(); pump(30);
    check(!update_close(), "exit cancels worker before destroying its context"); settle();
    check(!IsWindow(app.window), "window exits after cancellation completes");
    panel_destroy(); CloseDesktop(desktop);
    printf("Failures: %d\n", failures); return failures ? 1 : 0;
}
