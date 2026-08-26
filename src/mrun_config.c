#include "mrun.h"

void mrun_appearance_defaults(MrunAppearance *a) {
    memset(a, 0, sizeof(*a));

    a->width          = 620;
    a->rows           = 8;
    a->row_height     = 34;
    a->input_height   = 44;
    a->padding        = 12;
    a->font_size      = 16;
    a->sub_font_size  = 12;
    a->border_width   = 1;
    a->opacity        = 246;
    a->corner_radius  = 10;
    a->top_offset     = 18;

    a->bg        = RGB(0x1e, 0x1e, 0x2e);
    a->fg        = RGB(0xcd, 0xd6, 0xf4);
    a->dim       = RGB(0x7f, 0x84, 0x9c);
    a->sel_bg    = RGB(0x31, 0x32, 0x44);
    a->sel_fg    = RGB(0xff, 0xff, 0xff);
    a->border    = RGB(0x45, 0x47, 0x5a);
    a->prompt_fg = RGB(0x89, 0xb4, 0xfa);
    a->accent    = RGB(0x89, 0xb4, 0xfa);

    a->rounded        = true;
    a->show_subtitles = true;
    a->show_module    = true;
    a->show_scrollbar = true;
    a->position       = MRUN_POS_TOP;

    wcscpy(a->font, L"Segoe UI");
    wcscpy(a->prompt, L"›");
    wcscpy(a->placeholder, L"Search…");
    wcscpy(a->empty_text, L"No results");
}

static bool file_exists(const wchar_t *path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool appdata_config(wchar_t *out, size_t cap) {
    PWSTR roaming = NULL;
    bool  ok      = false;

    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_RoamingAppData,
                                       KF_FLAG_CREATE, NULL, &roaming))) {
        int n = _snwprintf(out, cap, L"%ls\\mrun\\init.lua", roaming);
        ok = n > 0 && (size_t)n < cap;
        CoTaskMemFree(roaming);
    }

    if (!ok) {
        const wchar_t *env = _wgetenv(L"APPDATA");
        if (env && env[0]) {
            int n = _snwprintf(out, cap, L"%ls\\mrun\\init.lua", env);
            ok = n > 0 && (size_t)n < cap;
        }
    }
    if (!ok) out[0] = L'\0';
    return ok;
}

static bool portable_config(wchar_t *out, size_t cap) {
    wchar_t dir[MAX_PATH];
    DWORD   n = GetModuleFileNameW(NULL, dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    wchar_t *slash = wcsrchr(dir, L'\\');
    if (!slash) return false;
    *slash = L'\0';

    int w = _snwprintf(out, cap, L"%ls\\config\\mrun.lua", dir);
    return w > 0 && (size_t)w < cap;
}

void mrun_config_resolve_path(wchar_t *out, size_t cap,
                              const wchar_t *override) {
    if (override && override[0]) {
        mrun_copy_w(out, cap, override);
        return;
    }

    wchar_t appdata[MAX_PATH];
    bool    have = appdata_config(appdata, MAX_PATH);
    if (have && file_exists(appdata)) { mrun_copy_w(out, cap, appdata); return; }

    wchar_t portable[MAX_PATH];
    if (portable_config(portable, MAX_PATH) && file_exists(portable)) {
        mrun_copy_w(out, cap, portable);
        return;
    }

    if (have) mrun_copy_w(out, cap, appdata);
    else      mrun_copy_w(out, cap, L"config\\mrun.lua");
}

static void set_package_path(lua_State *L, const wchar_t *config_path) {
    wchar_t dir[MAX_PATH];
    mrun_copy_w(dir, MAX_PATH, config_path);

    wchar_t *slash = wcsrchr(dir, L'\\');
    if (!slash) return;
    *slash = L'\0';

    char u8[MAX_PATH * 3];
    if (WideCharToMultiByte(CP_UTF8, 0, dir, -1, u8, (int)sizeof u8,
                            NULL, NULL) <= 0)
        return;

    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    lua_getfield(L, -1, "path");
    const char *existing = lua_tostring(L, -1);

    lua_pushfstring(L, "%s\\?.lua;%s\\?\\init.lua;%s",
                    u8, u8, existing ? existing : "");
    lua_setfield(L, -3, "path");

    lua_pop(L, 2);
}

static int load_file(lua_State *L, const wchar_t *path) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return LUA_ERRFILE;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return LUA_ERRFILE; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return LUA_ERRFILE; }
    rewind(f);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return LUA_ERRMEM; }

    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';

    const char *src = buf;
    size_t      len = read;
    if (len >= 3 && (unsigned char)buf[0] == 0xEF &&
                    (unsigned char)buf[1] == 0xBB &&
                    (unsigned char)buf[2] == 0xBF) {
        src += 3;
        len -= 3;
    }

    int status = luaL_loadbuffer(L, src, len, "@mrun.lua");
    free(buf);
    return status;
}

void mrun_config_shutdown(void) {
    mrun_results_reset();
    mrun_modules_shutdown();
    mr.module_count = 0;
    mr.order_count  = 0;

    if (mr.L) {
        lua_close(mr.L);
        mr.L = NULL;
    }
}

bool mrun_config_load(void) {
    mrun_results_reset();
    mrun_modules_reset();

    if (mr.L) {
        lua_close(mr.L);
        mr.L = NULL;
    }

    mrun_appearance_defaults(&mr.look);
    mr.hide_on_blur     = true;
    mr.clear_on_hide    = true;
    mr.config_error[0]  = '\0';
    mr.config_ok        = true;

    lua_State *L = luaL_newstate();
    if (!L) {
        snprintf(mr.config_error, sizeof mr.config_error,
                 "out of memory creating the Lua state");
        mr.config_ok = false;
        mrun_modules_init();
        return false;
    }

    luaL_openlibs(L);
    set_package_path(L, mr.config_path);
    mrun_lua_register(L);
    mr.L = L;

    if (!file_exists(mr.config_path)) {
        log_msg(LOG_INFO, L"mrun: no config at %ls — using defaults",
                mr.config_path);
        mrun_modules_init();
        return true;
    }

    int status = load_file(L, mr.config_path);
    if (status == LUA_OK) status = lua_pcall(L, 0, 0, 0);

    if (status != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        snprintf(mr.config_error, sizeof mr.config_error, "%s",
                 err ? err : "unknown error");
        lua_pop(L, 1);

        log_err(L"mrun: config FAILED: %hs", mr.config_error);
        log_err(L"mrun: the entire file was rejected — running with built-in "
                L"defaults until %ls loads cleanly", mr.config_path);

        mrun_appearance_defaults(&mr.look);
        mrun_modules_reset();
        mr.config_ok = false;
        mrun_modules_init();
        return false;
    }

    log_msg(LOG_INFO, L"mrun: config loaded from %ls", mr.config_path);
    mrun_modules_init();
    return true;
}
