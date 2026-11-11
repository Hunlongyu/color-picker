#include "app.h"
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <win_autoupdate.h>

/* UI owns the state machine; workers publish progress through atomics/messages. */
static UpdateInfo info;
static struct {
    au_ctx_t *ctx;
    au_release_t *release;
    const au_asset_t *asset;
    HANDLE thread;
    bool downloading, closing;
    volatile LONG percent;
    au_err_t result;
    char package[4096];
} job;

const UpdateInfo *update_info(void) { return &info; }
bool update_busy(void) { return job.thread != NULL; }
bool update_has_notice(void) { return job.asset != NULL; }
static void changed(void) {
    panel_update_changed();
    tray_update_changed();
}
static bool valid_digest(const char *digest) {
    if (!digest || strlen(digest) != 64)
        return false;
    for (int i = 0; i < 64; i++)
        if (!((digest[i] >= '0' && digest[i] <= '9') ||
              (digest[i] >= 'a' && digest[i] <= 'f') || (digest[i] >= 'A' && digest[i] <= 'F')))
            return false;
    return true;
}
static wchar_t *wide(const char *text) {
    if (!text || !*text) return NULL;
    int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *out = count > 0 ? calloc((size_t)count, sizeof(wchar_t)) : NULL;
    if (out) MultiByteToWideChar(CP_UTF8, 0, text, -1, out, count);
    return out;
}
static void discard_package(void) {
    if (job.package[0]) {
        wchar_t *path = wide(job.package);
        if (path) { DeleteFileW(path); free(path); }
        job.package[0] = 0;
    }
}
void update_destroy(void) {
    if (job.thread) return;
    discard_package();
    au_release_free(job.release);
    au_destroy(job.ctx);
    free(info.notes);
    ZeroMemory(&job, sizeof(job));
    ZeroMemory(&info, sizeof(info));
}
static int progress(uint64_t total, uint64_t done, void *unused) {
    (void)unused;
    LONG percent = total ? (LONG)(100.0 * (double)done / (double)total) : -1;
    /* 100% is reserved for successful SHA-256 verification. */
    if (percent > 99) percent = 99;
    if (InterlockedExchange(&job.percent, percent) != percent)
        PostMessageW(app.window, MSG_UPDATE_PROGRESS, 0, 0);
    return 0;
}
void update_progress(void) {
    if (info.phase == UPDATE_DOWNLOADING) {
        info.percent = (int)InterlockedCompareExchange(&job.percent, 0, 0);
        panel_update_changed();
    }
}
static unsigned __stdcall worker(void *unused) {
    (void)unused;
    if (job.downloading)
        job.result = au_download(job.ctx, job.asset, job.package, sizeof(job.package));
    else
        job.result = au_fetch_latest(job.ctx, &job.release);
    PostMessageW(app.window, MSG_UPDATE, 0, 0);
    return 0;
}
static void launch(void) {
    job.thread = (HANDLE)_beginthreadex(NULL, 0, worker, NULL, 0, NULL);
    if (!job.thread) { job.result = AU_ERR_NOMEM; info.phase = UPDATE_FAILED; }
    changed();
}
void start_update(bool manual) {
    if (manual) panel_open_updates();
    if (update_busy() || update_has_notice()) return;
    update_destroy();
    const char *mirrors[] = {"{url}"};
    au_config_t cfg = {.owner = "Hunlongyu", .repo = "color-picker",
        .current_version = APP_VERSION, .asset_pattern = "color-picker-x64.exe",
        .mode = AU_MODE_EXE, .mirrors = mirrors, .mirror_count = 1,
        .timeout_ms = 10000, .on_progress = progress};
    job.ctx = au_create(&cfg);
    if (!job.ctx) {
        job.result = AU_ERR_NOMEM; info.phase = UPDATE_FAILED; changed(); return;
    }
    info.phase = UPDATE_CHECKING;
    info.percent = -1;
    launch();
}
bool update_close(void) {
    if (!job.thread) return true;
    job.closing = true;
    au_cancel(job.ctx);
    ShowWindow(app.window, SW_HIDE);
    return false;
}
const wchar_t *update_status(void) {
    switch (info.phase) {
    case UPDATE_IDLE: return tr(L"从 GitHub 获取稳定版本", L"Stable releases from GitHub");
    case UPDATE_CHECKING: return tr(L"正在检查更新…", L"Checking for updates…");
    case UPDATE_CURRENT: return tr(L"已是最新版本", L"You're up to date");
    case UPDATE_AVAILABLE: return tr(L"发现新版本，查看更新内容", L"New version available");
    case UPDATE_DOWNLOADING:
        return info.percent == 99 ? tr(L"正在完成下载并校验…", L"Finishing download and verifying…")
                                 : tr(L"正在下载更新…", L"Downloading update…");
    case UPDATE_READY: return tr(L"校验通过，重启即可安装", L"Verified. Ready to restart");
    case UPDATE_CANCELED: return tr(L"下载已取消，可重新下载", L"Download canceled. You can retry");
    default: break;
    }
    static const wchar_t *const errors[] = {
        L"", L"更新参数无效", L"内存不足，请稍后重试", L"无法连接 GitHub，请检查网络",
        L"GitHub 请求失败，请稍后重试", L"GitHub 请求过于频繁，请稍后重试",
        L"暂时没有可用的稳定版发布信息", L"发布中缺少 Windows x64 更新文件",
        L"文件读写失败，请检查目录权限", L"更新文件缺少校验值或 SHA-256 不匹配",
        L"无法解压更新文件", L"程序目录不可写", L"替换程序失败，请重试",
        L"无法启动新版本，请重试", L"已取消", L"更新状态异常，请重试"};
    static wchar_t error[256];
    if (!app.english && (unsigned)job.result < ARRAYSIZE(errors)) return errors[job.result];
    swprintf(error, ARRAYSIZE(error), L"Update failed: %hs", au_strerror(job.result));
    return error;
}
void update_finished(void) {
    if (!job.ctx || !job.thread) return;
    WaitForSingleObject(job.thread, INFINITE);
    CloseHandle(job.thread);
    job.thread = NULL;
    if (job.closing) { update_destroy(); DestroyWindow(app.window); return; }
    if (job.downloading) {
        info.phase = job.result == AU_OK ? UPDATE_READY
            : job.result == AU_ERR_CANCELED ? UPDATE_CANCELED : UPDATE_FAILED;
        if (job.result == AU_OK) info.percent = 100;
        else discard_package();
    } else if (job.result == AU_OK && au_compare_current(job.ctx, job.release) > 0) {
        const au_asset_t *asset = au_pick_asset(job.release, "color-picker-x64.exe");
        if (!asset) job.result = AU_ERR_NO_ASSET;
        else if (!valid_digest(asset->sha256)) job.result = AU_ERR_HASH;
        if (job.result == AU_OK) {
            job.asset = asset;
            MultiByteToWideChar(CP_UTF8, 0, job.release->ver.raw, -1, info.version, ARRAYSIZE(info.version));
            info.notes = wide(job.release->changelog);
            info.phase = UPDATE_AVAILABLE;
        } else info.phase = UPDATE_FAILED;
    } else info.phase = job.result == AU_OK ? UPDATE_CURRENT : UPDATE_FAILED;
    changed();
}
void update_action(void) {
    if (update_busy()) {
        if (info.phase == UPDATE_DOWNLOADING) au_cancel(job.ctx);
        return;
    }
    if (info.phase == UPDATE_READY) {
        job.result = au_apply(job.ctx, job.package); /* Exits on successful restart. */
        discard_package();
        info.phase = UPDATE_FAILED;
        changed();
    } else if (job.asset) {
        au_reset_cancel(job.ctx);
        job.downloading = true;
        info.phase = UPDATE_DOWNLOADING;
        info.percent = -1;
        InterlockedExchange(&job.percent, -1);
        launch();
    } else start_update(true);
}
