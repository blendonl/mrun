#include "mrun.h"

#define APPS_MAX_ENTRIES 4096
#define APPS_MAX_ROOTS   16
#define APPS_MAX_EXTS    8
#define APPS_DEPTH       4

typedef struct {
    wchar_t name[MRUN_TITLE_CAP];
    wchar_t exec[MAX_PATH];
    wchar_t args[MRUN_ARGS_CAP];
    wchar_t cwd[MAX_PATH];
    wchar_t subtitle[MRUN_SUB_CAP];
} AppEntry;

typedef struct {
    AppEntry *entries;
    int       count;
    int       cap;

    AppEntry *extra;
    int       extra_count;
    int       extra_cap;

    wchar_t   roots[APPS_MAX_ROOTS][MAX_PATH];
    int       root_count;
    bool      roots_set;

    wchar_t   exts[APPS_MAX_EXTS][16];
    int       ext_count;

    int       depth;
    bool      show_path;
} AppsState;

static AppsState s_apps;

static bool apps_grow(AppEntry **arr, int *cap, int need) {
    if (need <= *cap) return true;
    int next = *cap ? *cap * 2 : 128;
    while (next < need) next *= 2;
    if (next > APPS_MAX_ENTRIES) next = APPS_MAX_ENTRIES;
    if (need > next) return false;

    AppEntry *grown = (AppEntry *)realloc(*arr, (size_t)next * sizeof(AppEntry));
    if (!grown) return false;
    *arr = grown;
    *cap = next;
    return true;
}

static bool apps_known_name(const wchar_t *name) {
    for (int i = 0; i < s_apps.count; i++)
        if (_wcsicmp(s_apps.entries[i].name, name) == 0) return true;
    return false;
}

static AppEntry *apps_push(void) {
    if (s_apps.count >= APPS_MAX_ENTRIES) return NULL;
    if (!apps_grow(&s_apps.entries, &s_apps.cap, s_apps.count + 1)) return NULL;

    AppEntry *e = &s_apps.entries[s_apps.count++];
    memset(e, 0, sizeof(*e));
    return e;
}

static bool apps_wanted_ext(const wchar_t *file) {
    const wchar_t *ext = wcsrchr(file, L'.');
    if (!ext) return false;
    for (int i = 0; i < s_apps.ext_count; i++)
        if (_wcsicmp(ext, s_apps.exts[i]) == 0) return true;
    return false;
}

static void apps_scan(const wchar_t *dir, int depth) {
    if (depth > s_apps.depth || s_apps.count >= APPS_MAX_ENTRIES) return;

    wchar_t pattern[MAX_PATH];
    if (_snwprintf(pattern, MAX_PATH, L"%ls\\*", dir) <= 0) return;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == L'.') continue;

        wchar_t full[MAX_PATH];
        if (_snwprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName) <= 0)
            continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            apps_scan(full, depth + 1);
            continue;
        }

        if (!apps_wanted_ext(fd.cFileName)) continue;

        wchar_t name[MRUN_TITLE_CAP];
        mrun_copy_w(name, MRUN_TITLE_CAP, fd.cFileName);
        wchar_t *dot = wcsrchr(name, L'.');
        if (dot) *dot = L'\0';
        if (!name[0] || apps_known_name(name)) continue;

        AppEntry *e = apps_push();
        if (!e) break;
        mrun_copy_w(e->name, MRUN_TITLE_CAP, name);
        mrun_copy_w(e->exec, MAX_PATH, full);
        if (s_apps.show_path) mrun_copy_w(e->subtitle, MRUN_SUB_CAP, dir);
    } while (FindNextFileW(h, &fd) && s_apps.count < APPS_MAX_ENTRIES);

    FindClose(h);
}

static void apps_scan_known(REFKNOWNFOLDERID id) {
    PWSTR path = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, NULL, &path))) {
        apps_scan(path, 0);
        CoTaskMemFree(path);
    }
}

static void apps_add_root(AppsState *st, const wchar_t *raw) {
    if (st->root_count >= APPS_MAX_ROOTS || !raw || !raw[0]) return;

    wchar_t expanded[MAX_PATH];
    DWORD n = ExpandEnvironmentStringsW(raw, expanded, MAX_PATH);
    const wchar_t *use = (n > 0 && n <= MAX_PATH) ? expanded : raw;

    mrun_copy_w(st->roots[st->root_count++], MAX_PATH, use);
}

static void apps_read_extra(AppsState *st, lua_State *L, int tbl) {
    lua_getfield(L, tbl, "extra");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    st->extra_count = 0;
    int entries = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= entries; i++) {
        lua_rawgeti(L, -1, i);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

        if (!apps_grow(&st->extra, &st->extra_cap, st->extra_count + 1)) {
            lua_pop(L, 1);
            break;
        }
        AppEntry *e = &st->extra[st->extra_count];
        memset(e, 0, sizeof(*e));

        lua_getfield(L, -1, "name");
        mrun_utf8_to_w(lua_tostring(L, -1), e->name, MRUN_TITLE_CAP);
        lua_pop(L, 1);

        lua_getfield(L, -1, "exec");
        if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_getfield(L, -1, "command"); }
        mrun_utf8_to_w(lua_tostring(L, -1), e->exec, MAX_PATH);
        lua_pop(L, 1);

        lua_getfield(L, -1, "args");
        mrun_utf8_to_w(lua_tostring(L, -1), e->args, MRUN_ARGS_CAP);
        lua_pop(L, 1);

        lua_getfield(L, -1, "cwd");
        mrun_utf8_to_w(lua_tostring(L, -1), e->cwd, MAX_PATH);
        lua_pop(L, 1);

        lua_getfield(L, -1, "subtitle");
        mrun_utf8_to_w(lua_tostring(L, -1), e->subtitle, MRUN_SUB_CAP);
        lua_pop(L, 1);

        lua_pop(L, 1);

        if (e->name[0] && e->exec[0]) st->extra_count++;
    }
    lua_pop(L, 1);
}

static void apps_configure(MrunModule *m, lua_State *L, int tbl) {
    AppsState *st = (AppsState *)m->state;

    lua_getfield(L, tbl, "paths");
    if (lua_istable(L, -1)) {
        st->roots_set = true;
        st->root_count = 0;
        int n = (int)lua_rawlen(L, -1);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            const char *s = lua_tostring(L, -1);
            if (s) {
                wchar_t path[MAX_PATH];
                mrun_utf8_to_w(s, path, MAX_PATH);
                apps_add_root(st, path);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, tbl, "extensions");
    if (lua_istable(L, -1)) {
        st->ext_count = 0;
        int n = (int)lua_rawlen(L, -1);
        for (int i = 1; i <= n && st->ext_count < APPS_MAX_EXTS; i++) {
            lua_rawgeti(L, -1, i);
            const char *s = lua_tostring(L, -1);
            if (s && s[0]) {
                wchar_t ext[16];
                mrun_utf8_to_w(s, ext, 16);
                if (ext[0] != L'.') {
                    wchar_t dotted[16];
                    _snwprintf(dotted, 16, L".%ls", ext);
                    dotted[15] = L'\0';
                    mrun_copy_w(st->exts[st->ext_count++], 16, dotted);
                } else {
                    mrun_copy_w(st->exts[st->ext_count++], 16, ext);
                }
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, tbl, "depth");
    if (lua_isnumber(L, -1)) st->depth = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, tbl, "show_path");
    if (lua_isboolean(L, -1)) st->show_path = lua_toboolean(L, -1);
    lua_pop(L, 1);

    apps_read_extra(st, L, tbl);
}

static bool apps_init(MrunModule *m) {
    AppsState *st = (AppsState *)m->state;
    st->count = 0;

    for (int i = 0; i < st->extra_count; i++) {
        AppEntry *e = apps_push();
        if (!e) break;
        *e = st->extra[i];
    }

    if (st->roots_set) {
        for (int i = 0; i < st->root_count; i++) apps_scan(st->roots[i], 0);
    } else {
        apps_scan_known(&FOLDERID_CommonPrograms);
        apps_scan_known(&FOLDERID_Programs);
    }

    log_msg(LOG_INFO, L"mrun/apps: indexed %d entries", st->count);
    return true;
}

static void apps_free_all(AppsState *st) {
    free(st->entries);
    free(st->extra);
    memset(st, 0, sizeof(*st));
}

static void apps_shutdown(MrunModule *m) {
    apps_free_all((AppsState *)m->state);
}

static void apps_search(MrunModule *m, const wchar_t *query, MrunResults *out) {
    AppsState *st = (AppsState *)m->state;

    for (int i = 0; i < st->count; i++) {
        const AppEntry *e = &st->entries[i];

        int score = mrun_score(query, e->name);
        if (score == MRUN_NO_MATCH) continue;

        MrunItem item;
        memset(&item, 0, sizeof(item));
        item.lua_ref = LUA_NOREF;
        mrun_copy_w(item.title,    MRUN_TITLE_CAP, e->name);
        mrun_copy_w(item.subtitle, MRUN_SUB_CAP,   e->subtitle);
        mrun_copy_w(item.exec,     MAX_PATH,       e->exec);
        mrun_copy_w(item.args,     MRUN_ARGS_CAP,  e->args);
        mrun_copy_w(item.cwd,      MAX_PATH,       e->cwd);

        mrun_results_add(out, m, &item, score);
    }
}

void mod_apps_register(void) {
    MrunModule *m = mrun_module_new(L"apps");
    if (!m) return;

    apps_free_all(&s_apps);
    s_apps.depth     = APPS_DEPTH;
    s_apps.show_path = false;
    s_apps.ext_count = 0;
    mrun_copy_w(s_apps.exts[s_apps.ext_count++], 16, L".lnk");
    mrun_copy_w(s_apps.exts[s_apps.ext_count++], 16, L".url");

    mrun_copy_w(m->description, MRUN_DESC_CAP, L"Installed applications");
    m->state       = &s_apps;
    m->max_results = 50;
    m->configure   = apps_configure;
    m->init        = apps_init;
    m->shutdown    = apps_shutdown;
    m->search      = apps_search;
}
