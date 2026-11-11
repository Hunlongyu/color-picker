#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <win_autoupdate.h>
int wmain(int argc, wchar_t **argv) {
    au_wait_for_predecessor_w(argc, argv, 10000);
    wchar_t path[MAX_PATH + 8];
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return 1;
    wcscpy(path + length, L".old");
    if (!DeleteFileW(path)) return 2;
    FILE *marker = fopen("restarted.txt", "wb");
    if (!marker) return 3;
    fputs("replacement started; predecessor exited; backup cleaned", marker);
    fclose(marker);
    return 0;
}
