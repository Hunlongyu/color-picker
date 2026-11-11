#include "app.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

const wchar_t *const formats[] = {L"HEX", L"HSL", L"RGB", L"CMYK", L"HWB"};

static void decimal(double value, wchar_t out[16]) {
    int tenths = (int)floor(value * 10);
    if (fma(value, 10, -tenths) >= 0.5)
        tenths++;
    if (tenths % 10)
        swprintf(out, 16, L"%d.%d", tenths / 10, tenths % 10);
    else
        swprintf(out, 16, L"%d", tenths / 10);
}

void color_text(COLORREF color, int format, wchar_t out[96]) {
    int red = GetRValue(color), green = GetGValue(color), blue = GetBValue(color);
    if (format == HEX) {
        swprintf(out, 96, L"#%02X%02X%02X", red, green, blue);
        return;
    }
    if (format == RGB_FORMAT) {
        swprintf(out, 96, L"rgb(%d, %d, %d)", red, green, blue);
        return;
    }
    double r = red / 255., g = green / 255., b = blue / 255.;
    double hi = fmax(r, fmax(g, b)), lo = fmin(r, fmin(g, b)), d = hi - lo;
    if (format == CMYK) {
        double k = 1 - hi, divisor = 1 - k;
        swprintf(out, 96, L"%d,%d,%d,%d", divisor ? (int)round((1 - r - k) / divisor * 100) : 0,
                 divisor ? (int)round((1 - g - k) / divisor * 100) : 0,
                 divisor ? (int)round((1 - b - k) / divisor * 100) : 0, (int)round(k * 100));
        return;
    }
    double hue = 0;
    if (d) {
        if (hi == r)
            hue = (g - b) / d;
        else if (hi == g)
            hue = 2 + (b - r) / d;
        else
            hue = 4 + (r - g) / d;
        hue *= 60;
        if (hue < 0)
            hue += 360;
    }
    double light = (hi + lo) / 2;
    bool hsl = format == HSL;
    wchar_t h[16], a[16], bstr[16];
    double sat = d ? d / (light <= 0.5 ? hi + lo : 2 - hi - lo) : 0;
    decimal(fmod(hue + 360, 360), h);
    decimal(100 * (hsl ? sat : lo), a);
    decimal(100 * (hsl ? light : 1 - hi), bstr);
    swprintf(out, 96, L"%ls(%ls, %ls%%, %ls%%)", hsl ? L"hsl" : L"hwb", h, a, bstr);
}

static int parse_color(const wchar_t *s, COLORREF *color) {
    if (wcslen(s) != 7 || *s++ != L'#')
        return 0;
    for (int i = 0; i < 6; i++)
        if (!iswxdigit(s[i]))
            return 0;
    unsigned long rgb = wcstoul(s, NULL, 16);
    *color = RGB(rgb >> 16, (rgb >> 8) & 255, rgb & 255);
    return 1;
}

bool refresh_appearance(void) {
    bool english = app.language == LANGUAGE_EN;
    bool dark = app.theme == THEME_DARK;
    if (app.language == LANGUAGE_SYSTEM)
        english = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_CHINESE;
    if (app.theme == THEME_SYSTEM) {
        /* Use the Windows application mode, which can differ from the taskbar
           mode. Missing settings on older systems fall back to light. */
        DWORD light = 1, size = sizeof(light);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                         L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &light, &size) == ERROR_SUCCESS)
            dark = light == 0;
    }
    bool changed = app.english != english || app.dark != dark;
    app.english = english; app.dark = dark;
    return changed;
}
static void load_appearance(void) {
    wchar_t setting[128];
    GetPrivateProfileStringW(L"settings", L"language", L"system", setting, ARRAYSIZE(setting), app.ini);
    app.language = !_wcsicmp(setting, L"zh-CN") ? LANGUAGE_ZH :
                   !_wcsicmp(setting, L"en") ? LANGUAGE_EN : LANGUAGE_SYSTEM;
    GetPrivateProfileStringW(L"settings", L"theme", L"system", setting, ARRAYSIZE(setting), app.ini);
    app.theme = !_wcsicmp(setting, L"light") ? THEME_LIGHT :
                !_wcsicmp(setting, L"dark") ? THEME_DARK : THEME_SYSTEM;
    refresh_appearance();
}
void set_language(int language) {
    if (language < LANGUAGE_SYSTEM || language > LANGUAGE_EN) return;
    app.language = language;
    save_option(L"language", language == LANGUAGE_ZH ? L"zh-CN" : language == LANGUAGE_EN ? L"en" : L"system");
    refresh_appearance();
}
void set_theme(int theme) {
    if (theme < THEME_SYSTEM || theme > THEME_DARK) return;
    app.theme = theme;
    save_option(L"theme", theme == THEME_LIGHT ? L"light" : theme == THEME_DARK ? L"dark" : L"system");
    refresh_appearance();
}

bool load_settings(void) {
    wchar_t exe[MAX_PATH], setting[128];
    DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!len || len >= MAX_PATH)
        return false;
    wchar_t *name = wcsrchr(exe, L'\\');
    if (!name || (size_t)(name - exe) + 1 + wcslen(L"color-picker.ini") >= MAX_PATH)
        return false;
    wcscpy(name + 1, L"color-picker.ini");
    wcscpy(app.ini, exe);
    load_appearance();
    app.auto_update = GetPrivateProfileIntW(L"settings", L"autoUpdate", 1, exe) != 0;
    GetPrivateProfileStringW(L"settings", L"shortcut", L"F2", app.hotkey, 64, exe);
    GetPrivateProfileStringW(L"settings", L"format", L"HEX", setting, 128, exe);
    for (int i = 0; i < FORMAT_COUNT; i++)
        if (!_wcsicmp(setting, formats[i]))
            app.default_format = i;
    app.format = app.default_format;
    GetPrivateProfileStringW(L"history", L"colors", L"", setting, 128, exe);
    wchar_t *s = setting;
    while (*s && app.count < HISTORY_MAX) {
        wchar_t *end = wcschr(s, L',');
        if (end)
            *end = 0;
        COLORREF c;
        if (parse_color(s, &c)) {
            int j = 0;
            while (j < app.count && app.history[j] != c)
                j++;
            if (j == app.count)
                app.history[app.count++] = c;
        }
        if (!end)
            break;
        s = end + 1;
    }
    app.has_color = app.count > 0;
    app.color = app.history[0];
    return true;
}

static void persist(const wchar_t *section, const wchar_t *key, const wchar_t *value) {
    if (!WritePrivateProfileStringW(section, key, value, app.ini))
        MessageBoxW(
            app.window,
            tr(L"无法写入同目录的 color-picker.ini。", L"Could not write color-picker.ini beside the executable."),
            L"Color Picker", MB_ICONERROR);
}
void save_option(const wchar_t *key, const wchar_t *value) {
    persist(L"settings", key, value);
}
void save_history(void) {
    wchar_t list[HISTORY_MAX * 8 + 1] = L"", hex[96];
    for (int i = 0; i < app.count; i++) {
        color_text(app.history[i], HEX, hex);
        if (i)
            wcscat(list, L",");
        wcscat(list, hex);
    }
    persist(L"history", L"colors", list);
}
void refresh_value(void) {
    wchar_t text[96] = L"";
    if (app.has_color)
        color_text(app.color, app.format, text);
    panel_set_value(text);
    InvalidateRect(app.window, NULL, FALSE);
}
void remember_color(COLORREF c) {
    int i = 0;
    while (i < app.count && app.history[i] != c)
        i++;
    panel_animate_history(i);
    if (i == app.count && app.count < HISTORY_MAX)
        app.count++;
    if (i == HISTORY_MAX)
        i--;
    memmove(app.history + 1, app.history, (size_t)i * sizeof(c));
    app.history[0] = app.color = c;
    app.has_color = true;
    refresh_value();
    panel_refresh();
    save_history();
    copy_value();
}
void copy_value(void) {
    const char *value = panel_value();
    int length = MultiByteToWideChar(CP_UTF8, 0, value, -1, NULL, 0);
    wchar_t *text = length > 1 ? malloc((size_t)length * sizeof(wchar_t)) : NULL;
    if (text) {
        MultiByteToWideChar(CP_UTF8, 0, value, -1, text, length);
        copy_text(text);
        free(text);
    }
}
void copy_text(const wchar_t *value) {
    size_t count = wcslen(value);
    if (!count)
        return;
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, (count + 1) * sizeof(wchar_t));
    if (!data)
        return;
    wchar_t *text = GlobalLock(data);
    if (text) {
        memcpy(text, value, (count + 1) * sizeof(wchar_t));
        GlobalUnlock(data);
        for (int i = 0; i < 5; i++) {
            if (OpenClipboard(app.window)) {
                if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data))
                    data = NULL;
                CloseClipboard();
                break;
            }
            Sleep(5);
        }
    }
    if (data) {
        GlobalFree(data);
        MessageBeep(MB_ICONWARNING);
    }
}

static UINT key_code(const wchar_t *s) {
    if (!s[1] && ((*s >= L'A' && *s <= L'Z') || (*s >= L'0' && *s <= L'9')))
        return (UINT)*s;
    if (*s == L'F') {
        wchar_t *end;
        long n = wcstol(s + 1, &end, 10);
        if (!*end && n >= 1 && n <= 24)
            return (UINT)(VK_F1 + n - 1);
    }
    static const struct {
        const wchar_t *name;
        UINT vk;
    } named[] = {
        {L"SPACE", VK_SPACE}, {L"INSERT", VK_INSERT}, {L"DELETE", VK_DELETE}, {L"HOME", VK_HOME},
        {L"END", VK_END},     {L"PAGEUP", VK_PRIOR},  {L"PAGEDOWN", VK_NEXT}, {L"PRINTSCREEN", VK_SNAPSHOT},
        {L"UP", VK_UP},       {L"DOWN", VK_DOWN},     {L"LEFT", VK_LEFT},     {L"RIGHT", VK_RIGHT}};
    for (size_t i = 0; i < ARRAYSIZE(named); i++)
        if (!wcscmp(s, named[i].name))
            return named[i].vk;
    return 0;
}
static int registered_id;
bool set_hotkey(const wchar_t *text, bool persist_key) {
    wchar_t input[64];
    if (!*text || wcslen(text) >= ARRAYSIZE(input))
        return false;
    int n = 0;
    for (const wchar_t *p = text; *p; p++)
        if (!iswspace(*p))
            input[n++] = (wchar_t)towupper(*p);
    input[n] = 0;
    UINT mods = MOD_NOREPEAT;
    wchar_t *s = input, *plus;
    while ((plus = wcschr(s, L'+'))) {
        *plus = 0;
        if (!wcscmp(s, L"CTRL") || !wcscmp(s, L"CONTROL"))
            mods |= MOD_CONTROL;
        else if (!wcscmp(s, L"SHIFT"))
            mods |= MOD_SHIFT;
        else if (!wcscmp(s, L"ALT"))
            mods |= MOD_ALT;
        else if (!wcscmp(s, L"WIN") || !wcscmp(s, L"SUPER"))
            mods |= MOD_WIN;
        else
            return false;
        s = plus + 1;
    }
    UINT vk = *s ? key_code(s) : 0;
    if (!vk)
        return false;
    static UINT current_mods, current_vk;
    if (registered_id && mods == current_mods && vk == current_vk)
        return true;
    int new_id = registered_id == 1 ? 2 : 1;
    if (!RegisterHotKey(app.window, new_id, mods, vk))
        return false;
    if (registered_id)
        UnregisterHotKey(app.window, registered_id);
    registered_id = new_id;
    current_mods = mods;
    current_vk = vk;
    if (persist_key) {
        wcscpy(app.hotkey, text);
        save_option(L"shortcut", text);
    }
    return true;
}
