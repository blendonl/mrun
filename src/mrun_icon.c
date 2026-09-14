#define COBJMACROS
#include "mrun.h"

#define ICON_MAX_ENTRIES 2048
#define ICON_MAX_JOBS    64
#define ICON_JOIN_MS     2000

typedef enum {
    ICON_IDLE = 0,
    ICON_QUEUED,
    ICON_READY,
    ICON_MISSING,
} IconState;

typedef struct {
    wchar_t  *path;
    unsigned  hash;
    IconState state;
    HBITMAP   bitmap;
} IconEntry;

typedef struct {
    wchar_t path[MAX_PATH];
    int     slot;
    int     size;
    WORD    generation;
} IconJob;

typedef enum {
    TAKE_NONE,
    TAKE_JOB,
    TAKE_STOP,
} IconTake;

static IconEntry *s_entries;
static int        s_entry_count;
static int        s_entry_cap;
static int        s_size;
static WORD       s_generation;

static CRITICAL_SECTION s_lock;
static HANDLE           s_wake;
static HANDLE           s_thread;
static HWND             s_notify;
static bool             s_stop;
static bool             s_unavailable;
static IconJob          s_jobs[ICON_MAX_JOBS];
static int              s_job_base;
static int              s_job_count;

static unsigned path_hash(const wchar_t *path) {
    unsigned hash = 2166136261u;
    for (; *path; path++) {
        hash ^= (unsigned)towlower(*path);
        hash *= 16777619u;
    }
    return hash;
}

static void strip_quotes(wchar_t *s) {
    if (s[0] != L'"') return;
    memmove(s, s + 1, wcslen(s) * sizeof(wchar_t));
    wchar_t *close = wcschr(s, L'"');
    if (close) *close = L'\0';
}

static bool app_paths_lookup(const wchar_t *name, wchar_t *out, DWORD cap) {
    wchar_t key[MAX_PATH];
    int n = _snwprintf(key, MAX_PATH,
                       L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                       L"App Paths\\%ls%ls",
                       name, wcschr(name, L'.') ? L"" : L".exe");
    if (n <= 0 || n >= MAX_PATH) return false;

    const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    for (size_t i = 0; i < ARRAYSIZE(roots); i++) {
        DWORD bytes = cap * sizeof(wchar_t);
        if (RegGetValueW(roots[i], key, NULL, RRF_RT_REG_SZ, NULL, out,
                         &bytes) == ERROR_SUCCESS) {
            strip_quotes(out);
            if (out[0]) return true;
        }
    }
    return false;
}

static void resolve_source(const wchar_t *source, wchar_t *out, DWORD cap) {
    DWORD n = ExpandEnvironmentStringsW(source, out, cap);
    if (n == 0 || n > cap) mrun_copy_w(out, cap, source);
    strip_quotes(out);

    if (wcspbrk(out, L"\\/:")) return;

    wchar_t name[MAX_PATH];
    mrun_copy_w(name, MAX_PATH, out);

    DWORD found = SearchPathW(NULL, name, L".exe", cap, out, NULL);
    if (found > 0 && found < cap) return;
    if (!app_paths_lookup(name, out, cap)) mrun_copy_w(out, cap, name);
}

static bool is_picture(const wchar_t *path) {
    static const wchar_t *const kinds[] = {
        L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".ico",
    };

    const wchar_t *ext = wcsrchr(path, L'.');
    if (!ext) return false;
    for (size_t i = 0; i < ARRAYSIZE(kinds); i++)
        if (_wcsicmp(ext, kinds[i]) == 0) return true;
    return false;
}

static HBITMAP dib_new(int width, int height, UINT32 **bits) {
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = width;
    bi.bmiHeader.biHeight      = -height;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)bits, NULL, 0);
}

static UINT32 channel(UINT32 pixel, int shift) {
    return (pixel >> shift) & 0xFF;
}

static void premultiply(UINT32 *pixels, int count) {
    bool translucent = false;
    bool straight    = false;

    for (int i = 0; i < count; i++) {
        UINT32 alpha = channel(pixels[i], 24);
        if (alpha) translucent = true;
        if (channel(pixels[i], 16) > alpha || channel(pixels[i], 8) > alpha ||
            channel(pixels[i], 0) > alpha)
            straight = true;
    }

    if (!translucent) {
        for (int i = 0; i < count; i++) pixels[i] |= 0xFF000000u;
        return;
    }
    if (!straight) return;

    for (int i = 0; i < count; i++) {
        UINT32 alpha = channel(pixels[i], 24);
        UINT32 r = (channel(pixels[i], 16) * alpha + 127) / 255;
        UINT32 g = (channel(pixels[i], 8)  * alpha + 127) / 255;
        UINT32 b = (channel(pixels[i], 0)  * alpha + 127) / 255;
        pixels[i] = (alpha << 24) | (r << 16) | (g << 8) | b;
    }
}

static HBITMAP fit_bitmap(HBITMAP source, int size) {
    BITMAP info;
    if (!GetObjectW(source, sizeof info, &info) || info.bmWidth <= 0 ||
        info.bmHeight == 0)
        return NULL;

    int width  = info.bmWidth;
    int height = abs(info.bmHeight);

    UINT32 *pixels = (UINT32 *)malloc((size_t)width * height * sizeof(UINT32));
    if (!pixels) return NULL;

    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = width;
    bi.bmiHeader.biHeight      = -height;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC dc   = CreateCompatibleDC(NULL);
    int rows = dc ? GetDIBits(dc, source, 0, (UINT)height, pixels, &bi,
                              DIB_RGB_COLORS)
                  : 0;
    if (dc) DeleteDC(dc);
    if (rows != height) { free(pixels); return NULL; }

    premultiply(pixels, width * height);

    UINT32 *bits   = NULL;
    HBITMAP fitted = dib_new(size, size, &bits);
    if (!fitted) { free(pixels); return NULL; }
    memset(bits, 0, (size_t)size * size * sizeof(UINT32));

    int copy_w = width  < size ? width  : size;
    int copy_h = height < size ? height : size;
    int dst_x  = (size - copy_w) / 2, dst_y = (size - copy_h) / 2;
    int src_x  = (width - copy_w) / 2, src_y = (height - copy_h) / 2;

    for (int y = 0; y < copy_h; y++)
        memcpy(bits + (dst_y + y) * size + dst_x,
               pixels + (src_y + y) * width + src_x,
               (size_t)copy_w * sizeof(UINT32));

    free(pixels);
    return fitted;
}

static HBITMAP shell_image(const wchar_t *path, int size) {
    IShellItemImageFactory *factory = NULL;
    if (FAILED(SHCreateItemFromParsingName(path, NULL,
                                           &IID_IShellItemImageFactory,
                                           (void **)&factory)))
        return NULL;

    SIZE    want   = { size, size };
    int     flags  = is_picture(path) ? SIIGBF_RESIZETOFIT : SIIGBF_ICONONLY;
    HBITMAP bitmap = NULL;
    if (FAILED(IShellItemImageFactory_GetImage(factory, want, flags, &bitmap)))
        bitmap = NULL;

    IShellItemImageFactory_Release(factory);
    return bitmap;
}

static HBITMAP icon_load(const wchar_t *source, int size) {
    wchar_t path[MAX_PATH];
    resolve_source(source, path, MAX_PATH);

    HBITMAP raw = shell_image(path, size);
    if (!raw) {
        log_w(L"mrun/icons: the shell has no icon for '%ls'", path);
        return NULL;
    }

    HBITMAP fitted = fit_bitmap(raw, size);
    DeleteObject(raw);
    return fitted;
}

static bool jobs_pop(IconJob *job) {
    if (s_job_count == 0) return false;
    s_job_count--;
    *job = s_jobs[(s_job_base + s_job_count) % ICON_MAX_JOBS];
    return true;
}

static void jobs_push(const IconJob *job) {
    if (s_job_count == ICON_MAX_JOBS) {
        s_entries[s_jobs[s_job_base].slot].state = ICON_IDLE;
        s_job_base = (s_job_base + 1) % ICON_MAX_JOBS;
        s_job_count--;
    }
    s_jobs[(s_job_base + s_job_count) % ICON_MAX_JOBS] = *job;
    s_job_count++;
}

static IconTake take_job(IconJob *job) {
    EnterCriticalSection(&s_lock);
    IconTake take = s_stop ? TAKE_STOP : jobs_pop(job) ? TAKE_JOB : TAKE_NONE;
    LeaveCriticalSection(&s_lock);
    return take;
}

static DWORD WINAPI icon_worker(LPVOID unused) {
    (void)unused;
    HRESULT  com  = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED |
                                         COINIT_DISABLE_OLE1DDE);
    IconTake take = TAKE_NONE;
    IconJob  job;

    while (take != TAKE_STOP) {
        WaitForSingleObject(s_wake, INFINITE);

        while ((take = take_job(&job)) == TAKE_JOB) {
            HBITMAP bitmap = icon_load(job.path, job.size);
            WPARAM  tag    = MAKEWPARAM(job.slot, job.generation);
            if (!PostMessageW(s_notify, WM_MRUN_ICON, tag, (LPARAM)bitmap) &&
                bitmap)
                DeleteObject(bitmap);
        }
    }

    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}

static bool worker_start(void) {
    if (s_thread)                    return true;
    if (s_unavailable || !mr.window) return false;

    InitializeCriticalSection(&s_lock);
    s_notify = mr.window;
    s_stop   = false;
    s_wake   = CreateEventW(NULL, FALSE, FALSE, NULL);
    s_thread = s_wake ? CreateThread(NULL, 0, icon_worker, NULL, 0, NULL)
                      : NULL;

    if (!s_thread) {
        log_err(L"mrun/icons: could not start the icon loader (%lu); "
                L"rows are drawn without icons", GetLastError());
        if (s_wake) CloseHandle(s_wake);
        s_wake = NULL;
        DeleteCriticalSection(&s_lock);
        s_unavailable = true;
        return false;
    }

    SetThreadPriority(s_thread, THREAD_PRIORITY_BELOW_NORMAL);
    return true;
}

static int entry_find(const wchar_t *path, unsigned hash) {
    for (int i = 0; i < s_entry_count; i++)
        if (s_entries[i].hash == hash && _wcsicmp(s_entries[i].path, path) == 0)
            return i;
    return -1;
}

static int entry_add(const wchar_t *path, unsigned hash) {
    if (s_entry_count >= ICON_MAX_ENTRIES) mrun_icons_flush();

    if (s_entry_count == s_entry_cap) {
        int        next  = s_entry_cap ? s_entry_cap * 2 : 64;
        IconEntry *grown = (IconEntry *)realloc(s_entries,
                                                (size_t)next * sizeof(IconEntry));
        if (!grown) return -1;
        s_entries   = grown;
        s_entry_cap = next;
    }

    wchar_t *copy = _wcsdup(path);
    if (!copy) return -1;

    IconEntry *e = &s_entries[s_entry_count];
    e->path   = copy;
    e->hash   = hash;
    e->state  = ICON_IDLE;
    e->bitmap = NULL;
    return s_entry_count++;
}

static void entry_request(int slot) {
    if (!worker_start()) return;

    IconJob job = {
        .slot       = slot,
        .size       = s_size,
        .generation = s_generation,
    };
    mrun_copy_w(job.path, MAX_PATH, s_entries[slot].path);

    EnterCriticalSection(&s_lock);
    jobs_push(&job);
    s_entries[slot].state = ICON_QUEUED;
    LeaveCriticalSection(&s_lock);

    SetEvent(s_wake);
}

HBITMAP mrun_icons_get(const wchar_t *source, int size) {
    if (!source || !source[0] || size <= 0) return NULL;

    if (size != s_size) {
        mrun_icons_flush();
        s_size = size;
    }

    unsigned hash = path_hash(source);
    int      slot = entry_find(source, hash);
    if (slot < 0) slot = entry_add(source, hash);
    if (slot < 0) return NULL;

    if (s_entries[slot].state == ICON_IDLE) entry_request(slot);
    return s_entries[slot].state == ICON_READY ? s_entries[slot].bitmap : NULL;
}

bool mrun_icons_deliver(WPARAM tag, LPARAM value) {
    HBITMAP bitmap = (HBITMAP)value;
    int     slot   = LOWORD(tag);

    if (HIWORD(tag) != s_generation || slot >= s_entry_count ||
        s_entries[slot].state != ICON_QUEUED) {
        if (bitmap) DeleteObject(bitmap);
        return false;
    }

    s_entries[slot].bitmap = bitmap;
    s_entries[slot].state  = bitmap ? ICON_READY : ICON_MISSING;
    return bitmap != NULL;
}

void mrun_icons_flush(void) {
    if (s_thread) {
        EnterCriticalSection(&s_lock);
        s_job_count = 0;
        LeaveCriticalSection(&s_lock);
    }

    s_generation++;
    for (int i = 0; i < s_entry_count; i++) {
        if (s_entries[i].bitmap) DeleteObject(s_entries[i].bitmap);
        free(s_entries[i].path);
    }
    s_entry_count = 0;
}

void mrun_icons_shutdown(void) {
    if (s_thread) {
        EnterCriticalSection(&s_lock);
        s_stop      = true;
        s_job_count = 0;
        LeaveCriticalSection(&s_lock);
        SetEvent(s_wake);

        if (WaitForSingleObject(s_thread, ICON_JOIN_MS) != WAIT_OBJECT_0) {
            log_err(L"mrun/icons: the icon loader did not stop within %d ms",
                    ICON_JOIN_MS);
            return;
        }

        CloseHandle(s_thread);
        CloseHandle(s_wake);
        DeleteCriticalSection(&s_lock);
        s_thread = NULL;
        s_wake   = NULL;
    }

    mrun_icons_flush();
    free(s_entries);
    s_entries   = NULL;
    s_entry_cap = 0;
    s_size      = 0;
}
