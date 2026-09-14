#include "mrun.h"

static COLORREF to_colorref(lua_Integer c) {
    return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static bool field_int(lua_State *L, int tbl, const char *key, int *out) {
    lua_getfield(L, tbl, key);
    bool ok = lua_isnumber(L, -1);
    if (ok) *out = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return ok;
}

static bool field_bool(lua_State *L, int tbl, const char *key, bool *out) {
    lua_getfield(L, tbl, key);
    bool ok = lua_isboolean(L, -1);
    if (ok) *out = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return ok;
}

static bool field_color(lua_State *L, int tbl, const char *key, COLORREF *out) {
    lua_getfield(L, tbl, key);
    bool ok = lua_isnumber(L, -1);
    if (ok) *out = to_colorref(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return ok;
}

static bool field_wstr(lua_State *L, int tbl, const char *key,
                       wchar_t *out, int cap) {
    lua_getfield(L, tbl, key);
    const char *s = lua_tostring(L, -1);
    bool ok = s != NULL;
    if (ok) mrun_utf8_to_w(s, out, cap);
    lua_pop(L, 1);
    return ok;
}

static int l_set_appearance(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    MrunAppearance *a = &mr.look;

    field_int(L, 1, "width",         &a->width);
    field_int(L, 1, "rows",          &a->rows);
    field_int(L, 1, "row_height",    &a->row_height);
    field_int(L, 1, "input_height",  &a->input_height);
    field_int(L, 1, "padding",       &a->padding);
    field_int(L, 1, "font_size",     &a->font_size);
    field_int(L, 1, "sub_font_size", &a->sub_font_size);
    field_int(L, 1, "icon_size",     &a->icon_size);
    field_int(L, 1, "border_width",  &a->border_width);
    field_int(L, 1, "opacity",       &a->opacity);
    field_int(L, 1, "corner_radius", &a->corner_radius);
    field_int(L, 1, "top_offset",    &a->top_offset);

    field_color(L, 1, "bg",        &a->bg);
    field_color(L, 1, "fg",        &a->fg);
    field_color(L, 1, "dim",       &a->dim);
    field_color(L, 1, "sel_bg",    &a->sel_bg);
    field_color(L, 1, "sel_fg",    &a->sel_fg);
    field_color(L, 1, "border",    &a->border);
    field_color(L, 1, "prompt_fg", &a->prompt_fg);
    field_color(L, 1, "accent",    &a->accent);

    field_bool(L, 1, "rounded",        &a->rounded);
    field_bool(L, 1, "show_subtitles", &a->show_subtitles);
    field_bool(L, 1, "show_icons",     &a->show_icons);
    field_bool(L, 1, "show_module",    &a->show_module);
    field_bool(L, 1, "show_scrollbar", &a->show_scrollbar);

    field_wstr(L, 1, "font",        a->font,        LF_FACESIZE);
    field_wstr(L, 1, "prompt",      a->prompt,      32);
    field_wstr(L, 1, "placeholder", a->placeholder, 96);
    field_wstr(L, 1, "empty_text",  a->empty_text,  96);

    wchar_t pos[16];
    if (field_wstr(L, 1, "position", pos, 16))
        a->position = (_wcsicmp(pos, L"center") == 0) ? MRUN_POS_CENTER
                                                      : MRUN_POS_TOP;

    if (a->opacity < 16)  a->opacity = 16;
    if (a->opacity > 255) a->opacity = 255;
    if (a->rows < 1)      a->rows    = 1;
    if (a->width < 160)   a->width   = 160;
    if (a->icon_size < 8) a->icon_size = 8;

    return 0;
}

static int l_set_behaviour(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    field_bool(L, 1, "hide_on_blur",  &mr.hide_on_blur);
    field_bool(L, 1, "clear_on_hide", &mr.clear_on_hide);

    lua_getfield(L, 1, "log_level");
    const char *level = lua_tostring(L, -1);
    LogLevel    parsed;
    if (level && log_level_from_name(level, &parsed)) log_set_level(parsed);
    lua_pop(L, 1);

    return 0;
}

static int l_set_modules(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    int n = (int)lua_rawlen(L, 1);
    if (n > MRUN_MAX_MODULES) n = MRUN_MAX_MODULES;

    wchar_t        storage[MRUN_MAX_MODULES][MRUN_NAME_CAP];
    const wchar_t *names[MRUN_MAX_MODULES];
    int            count = 0;

    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i);
        const char *s = lua_tostring(L, -1);
        if (s && s[0]) {
            mrun_utf8_to_w(s, storage[count], MRUN_NAME_CAP);
            names[count] = storage[count];
            count++;
        }
        lua_pop(L, 1);
    }

    mrun_module_set_order(names, count);
    return 0;
}

static void apply_common_fields(lua_State *L, int tbl, MrunModule *m) {
    field_wstr(L, tbl, "prefix",      m->prefix,      MRUN_PREFIX_CAP);
    field_wstr(L, tbl, "description", m->description, MRUN_DESC_CAP);
    field_int (L, tbl, "max_results", &m->max_results);
    field_bool(L, tbl, "enabled",     &m->enabled);
}

static int l_configure(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    wchar_t wname[MRUN_NAME_CAP];
    mrun_utf8_to_w(name, wname, MRUN_NAME_CAP);

    MrunModule *m = mrun_module_find(wname);
    if (!m) return luaL_error(L, "mrun.configure: no module named '%s'", name);

    apply_common_fields(L, 2, m);
    if (m->configure) m->configure(m, L, 2);
    return 0;
}

static int l_module(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    char name8[MRUN_NAME_CAP * 4];
    lua_getfield(L, 1, "name");
    const char *raw = lua_tostring(L, -1);
    snprintf(name8, sizeof name8, "%s", raw ? raw : "");
    lua_pop(L, 1);

    if (!name8[0])
        return luaL_error(L, "mrun.module: a 'name' string is required");

    wchar_t name[MRUN_NAME_CAP];
    mrun_utf8_to_w(name8, name, MRUN_NAME_CAP);

    MrunModule *m = mrun_module_new(name);
    if (!m) return luaL_error(L, "mrun.module: too many modules (max %d)",
                              MRUN_MAX_MODULES);
    if (!m->is_lua && m->search)
        return luaL_error(L, "mrun.module: '%s' is a built-in module; "
                             "use mrun.configure to change it", name8);

    lua_getfield(L, 1, "search");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return luaL_error(L, "mrun.module: '%s' needs a 'search' function",
                          name8);
    }
    if (m->lua_search != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, m->lua_search);
    m->lua_search = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_getfield(L, 1, "activate");
    if (lua_isfunction(L, -1)) {
        if (m->lua_activate != LUA_NOREF)
            luaL_unref(L, LUA_REGISTRYINDEX, m->lua_activate);
        m->lua_activate = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }

    m->is_lua      = true;
    m->search      = mrun_lua_module_search;
    m->activate    = mrun_lua_module_activate;
    m->max_results = 20;
    apply_common_fields(L, 1, m);
    return 0;
}

static int l_spawn(lua_State *L) {
    wchar_t cmd[MAX_PATH], args[MRUN_ARGS_CAP], cwd[MAX_PATH];
    mrun_utf8_to_w(luaL_checkstring(L, 1), cmd,  MAX_PATH);
    mrun_utf8_to_w(lua_tostring(L, 2),     args, MRUN_ARGS_CAP);
    mrun_utf8_to_w(lua_tostring(L, 3),     cwd,  MAX_PATH);

    lua_pushboolean(L, mrun_spawn(cmd, args, cwd));
    return 1;
}

static int l_set_clipboard(lua_State *L) {
    wchar_t *text = mrun_utf8_to_w_dup(luaL_checkstring(L, 1));
    bool     ok   = text && mrun_set_clipboard(text);
    free(text);

    lua_pushboolean(L, ok);
    return 1;
}

static int l_get_clipboard(lua_State *L) {
    if (!OpenClipboard(NULL)) { lua_pushnil(L); return 1; }

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); lua_pushnil(L); return 1; }

    const wchar_t *text = (const wchar_t *)GlobalLock(h);
    if (text) mrun_push_w(L, text);
    else      lua_pushnil(L);

    if (text) GlobalUnlock(h);
    CloseClipboard();
    return 1;
}

static int l_hide(lua_State *L) {
    (void)L;
    if (mr.window) PostMessageW(mr.window, WM_MRUN_HIDE, 0, 0);
    return 0;
}

static int l_set_query(lua_State *L) {
    wchar_t text[MRUN_QUERY_CAP];
    mrun_utf8_to_w(luaL_optstring(L, 1, ""), text, MRUN_QUERY_CAP);
    mrun_ui_set_query(text);
    return 0;
}

static int l_log(lua_State *L) {
    LogLevel    level = LOG_INFO;
    const char *first = luaL_checkstring(L, 1);
    const char *body  = lua_tostring(L, 2);

    if (body && log_level_from_name(first, &level)) {
        wchar_t msg[512];
        mrun_utf8_to_w(body, msg, 512);
        log_msg(level, L"config: %ls", msg);
        return 0;
    }

    wchar_t msg[512];
    mrun_utf8_to_w(first, msg, 512);
    log_msg(LOG_INFO, L"config: %ls", msg);
    return 0;
}

static int l_version(lua_State *L) {
    lua_pushstring(L, MRUN_VERSION);
    return 1;
}

void mrun_lua_module_free(MrunModule *m) {
    if (!mr.L) return;
    if (m->lua_search   != LUA_NOREF)
        luaL_unref(mr.L, LUA_REGISTRYINDEX, m->lua_search);
    if (m->lua_activate != LUA_NOREF)
        luaL_unref(mr.L, LUA_REGISTRYINDEX, m->lua_activate);
    m->lua_search   = LUA_NOREF;
    m->lua_activate = LUA_NOREF;
}

static void read_lua_item(lua_State *L, int idx, MrunItem *item) {
    memset(item, 0, sizeof(*item));
    item->lua_ref = LUA_NOREF;

    field_wstr(L, idx, "title",    item->title,    MRUN_TITLE_CAP);
    field_wstr(L, idx, "subtitle", item->subtitle, MRUN_SUB_CAP);
    field_wstr(L, idx, "exec",     item->exec,     MAX_PATH);
    field_wstr(L, idx, "args",     item->args,     MRUN_ARGS_CAP);
    field_wstr(L, idx, "cwd",      item->cwd,      MAX_PATH);
    if (!field_wstr(L, idx, "icon", item->icon, MAX_PATH))
        mrun_copy_w(item->icon, MAX_PATH, item->exec);
}

void mrun_lua_module_search(MrunModule *m, const wchar_t *query,
                            MrunResults *out) {
    lua_State *L = mr.L;
    if (!L || m->lua_search == LUA_NOREF) return;

    int base = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, m->lua_search);
    mrun_push_w(L, query);

    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        log_err(L"mrun: module '%ls' search failed: %hs", m->name,
                lua_tostring(L, -1));
        lua_settop(L, base);
        return;
    }
    if (!lua_istable(L, -1)) { lua_settop(L, base); return; }

    int n = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= n && out->count < MRUN_MAX_ITEMS; i++) {
        lua_rawgeti(L, -1, i);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

        MrunItem item;
        read_lua_item(L, lua_gettop(L), &item);
        if (!item.title[0]) { lua_pop(L, 1); continue; }

        int score;
        lua_getfield(L, -1, "score");
        if (lua_isnumber(L, -1)) {
            score = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
        } else {
            lua_pop(L, 1);
            score = mrun_score(query, item.title);
            if (score == MRUN_NO_MATCH) { lua_pop(L, 1); continue; }
        }

        lua_pushvalue(L, -1);
        item.lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);

        mrun_results_add(out, m, &item, score);
        lua_pop(L, 1);
    }

    lua_settop(L, base);
}

bool mrun_lua_module_activate(MrunModule *m, const MrunItem *item) {
    lua_State *L = mr.L;
    if (!L) return true;

    if (m->lua_activate == LUA_NOREF) {
        if (item->exec[0]) return mrun_spawn(item->exec, item->args, item->cwd);
        return true;
    }

    int base = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, m->lua_activate);

    if (item->lua_ref != LUA_NOREF)
        lua_rawgeti(L, LUA_REGISTRYINDEX, item->lua_ref);
    else
        lua_newtable(L);

    mrun_push_w(L, mr.query);

    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        log_err(L"mrun: module '%ls' activate failed: %hs", m->name,
                lua_tostring(L, -1));
        lua_settop(L, base);
        return true;
    }

    bool keep_open = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    lua_settop(L, base);
    return !keep_open;
}

void mrun_lua_register(lua_State *L) {
    static const luaL_Reg api[] = {
        {"set_appearance", l_set_appearance},
        {"set_behaviour",  l_set_behaviour},
        {"set_behavior",   l_set_behaviour},
        {"set_modules",    l_set_modules},
        {"configure",      l_configure},
        {"module",         l_module},
        {"spawn",          l_spawn},
        {"set_clipboard",  l_set_clipboard},
        {"get_clipboard",  l_get_clipboard},
        {"set_query",      l_set_query},
        {"hide",           l_hide},
        {"log",            l_log},
        {"version",        l_version},
        {NULL, NULL}
    };

    luaL_newlib(L, api);
    lua_setglobal(L, "mrun");
}
