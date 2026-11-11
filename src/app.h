#ifndef COLOR_PICKER_APP_H
#define COLOR_PICKER_APP_H
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>
#if !defined(_M_X64) && !defined(__x86_64__)
#error The release asset targets Windows x64.
#endif

enum { PANEL_WIDTH = 372, PANEL_MIN_WIDTH = 260, PANEL_HEIGHT = 129, PANEL_COMPACT_HEIGHT = 84,
       SETTINGS_HEIGHT = 632, UPDATE_HEIGHT = 488, HISTORY_MAX = 12 };
enum Format { HEX, HSL, RGB_FORMAT, CMYK, HWB, FORMAT_COUNT };
enum Language { LANGUAGE_SYSTEM, LANGUAGE_ZH, LANGUAGE_EN };
enum Theme { THEME_SYSTEM, THEME_LIGHT, THEME_DARK };
enum { MSG_TRAY = WM_APP + 1, MSG_UPDATE, MSG_SHOW, MSG_UPDATE_PROGRESS, MSG_PANEL_ACTION, MSG_PANEL_REFRESH };
typedef struct {
    HINSTANCE instance;
    HWND window;
    UINT dpi;
    bool english, dark, auto_update, settings, update_view, has_color, registered;
    int format, default_format, count;
    int language, theme;
    COLORREF color, history[HISTORY_MAX];
    wchar_t hotkey[64], ini[MAX_PATH];
} App;
extern App app;
extern const wchar_t *const formats[FORMAT_COUNT];
static inline const wchar_t *tr(const wchar_t *zh, const wchar_t *en) {
    return app.english ? en : zh;
}
static inline int px(int dip) {
    return MulDiv(dip, app.dpi, 96);
}
void color_text(COLORREF color, int format, wchar_t out[96]);
bool load_settings(void);
bool refresh_appearance(void);
void set_language(int language);
void set_theme(int theme);
void save_history(void);
void save_option(const wchar_t *key, const wchar_t *value);
void remember_color(COLORREF color);
bool set_hotkey(const wchar_t *text, bool persist);
void refresh_value(void);
void copy_value(void);
void copy_text(const wchar_t *text);
void panel_set_value(const wchar_t *text);
const char *panel_value(void);
bool panel_init(void);
void panel_layout(void);
void panel_refresh(void);
int panel_height(void);
int panel_width(void);
void panel_open_updates(void);
void panel_update_changed(void);
void panel_animate_history(int previous_index);
void panel_destroy(void);
bool panel_message(UINT message, WPARAM w, LPARAM l, LRESULT *result);
void start_picker(void);
void cycle_picker_format(void);
bool picker_active(void);
void cancel_picker(void);
void show_panel(void);
void tray_update_changed(void);
enum UpdatePhase { UPDATE_IDLE, UPDATE_CHECKING, UPDATE_CURRENT, UPDATE_AVAILABLE,
                   UPDATE_DOWNLOADING, UPDATE_READY, UPDATE_FAILED, UPDATE_CANCELED };
typedef struct {
    enum UpdatePhase phase;
    int percent;
    wchar_t version[64];
    wchar_t *notes;
} UpdateInfo;
const UpdateInfo *update_info(void);
const wchar_t *update_status(void);
bool update_has_notice(void);
void start_update(bool manual);
void update_finished(void);
void update_progress(void);
void update_action(void);
void update_destroy(void);
bool update_busy(void);
bool update_close(void);
#endif
