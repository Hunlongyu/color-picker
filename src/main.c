#include "app.h"
#include "icons.h"
#include <commctrl.h>
#include <shellapi.h>
#include <win_autoupdate.h>

App app;
static const wchar_t main_class[] = L"Hunlongyu.ColorPicker.Panel";
static UINT taskbar_created;
static bool panel_ready, tray_added;
static HICON update_icon;
enum {
    TRAY_SHOW = 1,
    TRAY_HIDE,
    TRAY_PICK,
    TRAY_LANGUAGE_SYSTEM,
    TRAY_ZH,
    TRAY_EN,
    TRAY_THEME_SYSTEM,
    TRAY_LIGHT,
    TRAY_DARK,
    TRAY_AUTO,
    TRAY_CHECK,
    TRAY_EXIT
};
enum { TIMER_START = 1, TIMER_DAILY, TIMER_TRAY, TIMER_UPDATE_READY };
static NOTIFYICONDATAW tray_data(void) {
    HICON original = LoadIconW(app.instance, MAKEINTRESOURCEW(1));
    if (update_has_notice() && !update_icon) update_icon = icon_update_badge(original);
    NOTIFYICONDATAW n = {.cbSize = sizeof(n),
                         .hWnd = app.window,
                         .uID = 1,
                         .uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE,
                         .uCallbackMessage = MSG_TRAY,
                         .hIcon = update_has_notice() && update_icon ? update_icon : original};
    wcscpy(n.szTip, update_has_notice() ? tr(L"Color Picker · 有可用更新，点击查看", L"Color Picker · Update available. Click to view")
                                       : tr(L"Color Picker 取色器", L"Color Picker"));
    return n;
}
void tray_update_changed(void) {
    if (tray_added) {
        NOTIFYICONDATAW n = tray_data();
        Shell_NotifyIconW(NIM_MODIFY, &n);
    }
}
static void add_tray(void) {
    NOTIFYICONDATAW n = tray_data();
    tray_added = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
    if (tray_added)
        KillTimer(app.window, TIMER_TRAY);
    else
        SetTimer(app.window, TIMER_TRAY, 3000, NULL);
}
static void sync_system_appearance(void) {
    if (panel_ready && refresh_appearance()) {
        tray_update_changed();
        panel_refresh();
    }
}
void show_panel(void) {
    sync_system_appearance();
    if (IsIconic(app.window))
        ShowWindow(app.window, SW_RESTORE);
    ShowWindow(app.window, SW_SHOW);
    SetForegroundWindow(app.window);
}
static void tray_menu(void) {
    sync_system_appearance();
    HMENU menu = CreatePopupMenu(), lang = CreatePopupMenu(), theme = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IsWindowVisible(app.window) ? TRAY_HIDE : TRAY_SHOW,
                IsWindowVisible(app.window) ? tr(L"隐藏界面", L"Hide panel") : tr(L"显示界面", L"Show panel"));
    AppendMenuW(menu, MF_STRING, TRAY_PICK, tr(L"取色", L"Pick a color"));
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(lang, MF_STRING, TRAY_LANGUAGE_SYSTEM, tr(L"系统默认", L"System default"));
    AppendMenuW(lang, MF_STRING, TRAY_ZH, L"简体中文");
    AppendMenuW(lang, MF_STRING, TRAY_EN, L"English");
    CheckMenuRadioItem(lang, TRAY_LANGUAGE_SYSTEM, TRAY_EN, TRAY_LANGUAGE_SYSTEM + app.language, MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)lang, tr(L"语言", L"Language"));
    AppendMenuW(theme, MF_STRING, TRAY_THEME_SYSTEM, tr(L"系统默认", L"System default"));
    AppendMenuW(theme, MF_STRING, TRAY_LIGHT, tr(L"浅色", L"Light"));
    AppendMenuW(theme, MF_STRING, TRAY_DARK, tr(L"深色", L"Dark"));
    CheckMenuRadioItem(theme, TRAY_THEME_SYSTEM, TRAY_DARK, TRAY_THEME_SYSTEM + app.theme, MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)theme, tr(L"主题", L"Theme"));
    AppendMenuW(menu, MF_STRING | (app.auto_update ? MF_CHECKED : 0), TRAY_AUTO,
                tr(L"自动检查更新", L"Automatically check for updates"));
    AppendMenuW(menu, MF_STRING, TRAY_CHECK,
                update_has_notice() ? tr(L"查看更新", L"View update")
                : update_busy() ? tr(L"正在检查…", L"Checking…")
                              : tr(L"检查更新", L"Check for updates"));
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, TRAY_EXIT, tr(L"退出软件", L"Quit"));
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(app.window);
    int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, app.window, NULL);
    PostMessageW(app.window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    switch (id) {
    case TRAY_SHOW:
        show_panel();
        break;
    case TRAY_HIDE:
        ShowWindow(app.window, SW_HIDE);
        break;
    case TRAY_PICK:
        start_picker();
        break;
    case TRAY_LANGUAGE_SYSTEM:
    case TRAY_ZH:
    case TRAY_EN:
        set_language(id - TRAY_LANGUAGE_SYSTEM);
        tray_update_changed();
        panel_refresh();
        break;
    case TRAY_THEME_SYSTEM:
    case TRAY_LIGHT:
    case TRAY_DARK:
        set_theme(id - TRAY_THEME_SYSTEM);
        tray_update_changed();
        panel_refresh();
        break;
    case TRAY_AUTO:
        app.auto_update = !app.auto_update;
        save_option(L"autoUpdate", app.auto_update ? L"1" : L"0");
        if (app.auto_update)
            start_update(false);
        panel_refresh();
        break;
    case TRAY_CHECK:
        start_update(true);
        break;
    case TRAY_EXIT:
        PostMessageW(app.window, WM_CLOSE, 0, 0);
        break;
    }
}
static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (taskbar_created && msg == taskbar_created) {
        add_tray();
        return 0;
    }
    switch (msg) {
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        sync_system_appearance();
        return 0;
    case WM_ACTIVATEAPP:
        if (w) sync_system_appearance();
        break;
    case WM_HOTKEY:
        if (picker_active() && HIWORD(l) == 'C' && !(LOWORD(l) & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN)))
            cycle_picker_format();
        else
            start_picker();
        return 0;
    case MSG_SHOW:
        show_panel();
        return 0;
    case MSG_TRAY:
        if (l == WM_LBUTTONUP) {
            if (update_has_notice()) panel_open_updates();
            else show_panel();
        }
        else if (l == WM_RBUTTONUP)
            tray_menu();
        return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_MINIMIZE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;
    case WM_SIZE:
        if (w == SIZE_MINIMIZED)
            ShowWindow(hwnd, SW_HIDE);
        break;
    case WM_DPICHANGED: {
        app.dpi = HIWORD(w);
        RECT *r = (RECT *)l;
        SetWindowPos(hwnd, NULL, r->left, r->top, px(panel_width()), px(panel_height()),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (panel_ready)
            panel_layout();
        return 0;
    }
    case WM_TIMER:
        if (w == TIMER_START || w == TIMER_DAILY) {
            if (w == TIMER_START)
                KillTimer(hwnd, TIMER_START);
            if (app.auto_update)
                start_update(false);
        } else if (w == TIMER_TRAY)
            add_tray();
        else if (w == TIMER_UPDATE_READY) {
            if (!picker_active()) {
                KillTimer(hwnd, TIMER_UPDATE_READY);
                update_finished();
            }
        } else
            break;
        return 0;
    case MSG_UPDATE:
        if (picker_active())
            SetTimer(hwnd, TIMER_UPDATE_READY, 150, NULL);
        else
            update_finished();
        return 0;
    case MSG_UPDATE_PROGRESS:
        update_progress();
        return 0;
    case WM_CLOSE:
        cancel_picker();
        if (update_close())
            DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: {
        if (tray_added) {
            NOTIFYICONDATAW n = tray_data();
            Shell_NotifyIconW(NIM_DELETE, &n);
        }
        panel_ready = false;
        PostQuitMessage(0);
        return 0;
    }
    }
    LRESULT result;
    if (panel_ready && panel_message(msg, w, l, &result))
        return result;
    return DefWindowProcW(hwnd, msg, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR args, int show) {
    (void)previous;
    (void)args;
    (void)show;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    au_wait_for_predecessor_w(argc, argv, 10000);
    bool restarted = false;
    for (int i = 1; i < argc; i++)
        if (!wcsncmp(argv[i], L"--au-wait-pid=", 14))
            restarted = true;
    LocalFree(argv);
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\Hunlongyu.ColorPicker.Portable");
    if (!mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND panel = NULL;
        for (int i = 0; i < 20 && !panel; i++) {
            panel = FindWindowW(main_class, NULL);
            if (!panel)
                Sleep(50);
        }
        if (panel)
            PostMessageW(panel, MSG_SHOW, 0, 0);
        CloseHandle(mutex);
        return 0;
    }
    if (restarted) {
        /* EXE mode leaves only <self>.old. The generic ZIP cleanup would also
           remove a directory named _au_tmp in the user's portable folder. */
        wchar_t backup[MAX_PATH + 4];
        DWORD length = GetModuleFileNameW(NULL, backup, MAX_PATH);
        if (length && length < MAX_PATH) {
            wcscpy(backup + length, L".old");
            DeleteFileW(backup);
        }
    }
    app.instance = instance;
    if (!load_settings()) {
        MessageBoxW(NULL, L"Color Picker: the portable folder path is too long.", L"Color Picker", MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW cls = {.style = CS_DBLCLKS,
                     .hInstance = instance,
                     .lpfnWndProc = main_proc,
                     .lpszClassName = main_class,
                     .hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)),
                     .hCursor = LoadCursorW(NULL, IDC_ARROW)};
    if (!RegisterClassW(&cls))
        return 1;
    app.dpi = GetDpiForSystem();
    RECT area;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    int width = px(panel_width()), height = px(panel_height());
    app.window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, main_class, L"Color Picker",
                                 WS_POPUP | WS_CLIPCHILDREN, (area.left + area.right - width) / 2,
                                 (area.top + area.bottom - height) / 2, width, height, NULL, NULL, instance, NULL);
    if (!app.window || !panel_init()) {
        MessageBoxW(NULL, tr(L"无法初始化取色器界面。", L"Could not initialize the panel."), L"Color Picker",
                    MB_ICONERROR);
        return 1;
    }
    panel_ready = true;
    app.registered = set_hotkey(app.hotkey, false);
    panel_refresh();
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(app.window, taskbar_created, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(app.window, MSG_TRAY, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(app.window, WM_SETTINGCHANGE, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(app.window, WM_THEMECHANGED, MSGFLT_ALLOW, NULL);
    add_tray();
    show_panel();
    SetTimer(app.window, TIMER_START, 2000, NULL);
    SetTimer(app.window, TIMER_DAILY, 24u * 60 * 60 * 1000, NULL);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    panel_destroy();
    if (update_icon) DestroyIcon(update_icon);
    update_destroy();
    CloseHandle(mutex);
    return 0;
}
