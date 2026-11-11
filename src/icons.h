#ifndef COLOR_PICKER_ICONS_H
#define COLOR_PICKER_ICONS_H
#include <stdbool.h>
#include <windows.h>
enum Icon { ICON_SETTINGS, ICON_BACK, ICON_LINK, ICON_BUG, ICON_MINUS, ICON_CLOSE,
            ICON_PICK, ICON_COPY, ICON_DOWNLOAD, ICON_REFRESH, ICON_CHECK,
            ICON_SAVE, ICON_RESTART, ICON_CHEVRON, ICON_KEYBOARD, ICON_PALETTE,
            ICON_LANGUAGE, ICON_THEME, ICON_COUNT };
bool icon_pixels(int index, int size, unsigned char *rgba);
HICON icon_update_badge(HICON original);
#endif
