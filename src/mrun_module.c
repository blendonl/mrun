#include "mrun.h"

MrunModule *mrun_module_find(const wchar_t *name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < mr.module_count; i++)
        if (_wcsicmp(mr.modules[i].name, name) == 0) return &mr.modules[i];
    return NULL;
}

MrunModule *mrun_module_new(const wchar_t *name) {
    MrunModule *existing = mrun_module_find(name);
    if (existing) return existing;

    if (mr.module_count >= MRUN_MAX_MODULES) {
        log_err(L"mrun: module limit (%d) reached, '%ls' ignored",
                MRUN_MAX_MODULES, name);
        return NULL;
    }

    MrunModule *m = &mr.modules[mr.module_count++];
    memset(m, 0, sizeof(*m));
    mrun_copy_w(m->name, MRUN_NAME_CAP, name);
    m->enabled      = true;
    m->max_results  = 0;
    m->lua_search   = LUA_NOREF;
    m->lua_activate = LUA_NOREF;
    m->lua_reload   = LUA_NOREF;
    return m;
}

static void rebuild_default_order(void) {
    mr.order_count = 0;
    for (int i = 0; i < mr.module_count; i++)
        if (mr.modules[i].enabled) mr.order[mr.order_count++] = i;
}

void mrun_module_set_order(const wchar_t **names, int count) {
    if (count > MRUN_MAX_MODULES) count = MRUN_MAX_MODULES;

    for (int i = 0; i < count; i++)
        mrun_copy_w(mr.order_names[i], MRUN_NAME_CAP, names[i]);

    mr.order_names_count = count;
    mr.order_explicit    = true;
}

static void resolve_explicit_order(void) {
    for (int i = 0; i < mr.module_count; i++) mr.modules[i].enabled = false;

    mr.order_count = 0;
    for (int i = 0; i < mr.order_names_count; i++) {
        MrunModule *m = mrun_module_find(mr.order_names[i]);
        if (!m) {
            log_err(L"mrun: unknown module '%ls' in the module list",
                    mr.order_names[i]);
            continue;
        }
        if (m->enabled) continue;
        m->enabled = true;
        mr.order[mr.order_count++] = (int)(m - mr.modules);
    }
}

void mrun_modules_reset(void) {
    mrun_modules_shutdown();
    mr.module_count       = 0;
    mr.order_count        = 0;
    mr.order_names_count  = 0;
    mr.order_explicit     = false;
    mod_apps_register();
}

void mrun_modules_init(void) {
    if (mr.order_explicit) resolve_explicit_order();
    else                   rebuild_default_order();

    for (int i = 0; i < mr.order_count; i++) {
        MrunModule *m = &mr.modules[mr.order[i]];
        if (m->init && !m->init(m)) {
            log_err(L"mrun: module '%ls' failed to initialise, disabling it",
                    m->name);
            m->enabled = false;
        }
    }

    int keep = 0;
    for (int i = 0; i < mr.order_count; i++)
        if (mr.modules[mr.order[i]].enabled) mr.order[keep++] = mr.order[i];
    mr.order_count = keep;

    log_msg(LOG_INFO, L"mrun: %d module(s) active of %d registered",
            mr.order_count, mr.module_count);
}

void mrun_modules_reload_data(void) {
    for (int i = 0; i < mr.order_count; i++) {
        MrunModule *m = &mr.modules[mr.order[i]];
        if (m->init && !m->init(m)) m->enabled = false;
    }
}

void mrun_modules_shutdown(void) {
    for (int i = 0; i < mr.module_count; i++) {
        MrunModule *m = &mr.modules[i];
        if (m->shutdown) m->shutdown(m);
        if (m->is_lua)   mrun_lua_module_free(m);
    }
}

static void results_drop(MrunResults *r, int from) {
    for (int i = from; i < r->count; i++)
        if (r->items[i].lua_ref != LUA_NOREF && mr.L)
            luaL_unref(mr.L, LUA_REGISTRYINDEX, r->items[i].lua_ref);
    if (from < r->count) r->count = from;
}

void mrun_results_reset(void) {
    results_drop(&mr.results, 0);
    mr.sel    = 0;
    mr.scroll = 0;
    mr.routed = NULL;
}

void mrun_results_add(MrunResults *out, MrunModule *owner,
                      const MrunItem *item, int score) {
    if (!out || out->count >= MRUN_MAX_ITEMS) return;

    MrunItem *dst = &out->items[out->count];
    *dst        = *item;
    dst->owner  = owner;
    dst->score  = score;
    dst->seq    = out->count;
    out->count++;
}

static bool ranks_above(const MrunItem *a, const MrunItem *b) {
    if (a->score != b->score) return a->score > b->score;
    return a->seq < b->seq;
}

static void sort_range(MrunResults *r, int lo, int hi) {
    static int  order[MRUN_MAX_ITEMS];
    static bool placed[MRUN_MAX_ITEMS];

    int n = hi - lo;
    if (n < 2) return;

    for (int i = 0; i < n; i++) order[i] = lo + i;

    for (int i = 1; i < n; i++) {
        int key = order[i];
        int j   = i - 1;
        while (j >= 0 && ranks_above(&r->items[key], &r->items[order[j]])) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    for (int i = 0; i < n; i++) placed[i] = false;

    for (int i = 0; i < n; i++) {
        if (placed[i] || order[i] == lo + i) { placed[i] = true; continue; }

        MrunItem carried = r->items[lo + i];
        int      slot    = i;

        while (!placed[slot]) {
            placed[slot] = true;

            int from = order[slot];
            if (from == lo + i) {
                r->items[lo + slot] = carried;
                break;
            }
            r->items[lo + slot] = r->items[from];
            slot = from - lo;
        }
    }
}

static const wchar_t *skip_spaces(const wchar_t *s) {
    while (*s == L' ' || *s == L'\t') s++;
    return s;
}

static MrunModule *route(const wchar_t *query, const wchar_t **rest) {
    *rest = query;
    if (!query || !query[0]) return NULL;

    for (int i = 0; i < mr.order_count; i++) {
        MrunModule *m = &mr.modules[mr.order[i]];
        if (!m->prefix[0]) continue;

        size_t n = wcslen(m->prefix);
        if (wcsncmp(query, m->prefix, n) != 0) continue;

        *rest = skip_spaces(query + n);
        return m;
    }
    return NULL;
}

static void run_module(MrunModule *m, const wchar_t *query, MrunResults *out) {
    if (!m->search) return;

    int before = out->count;
    m->search(m, query, out);
    sort_range(out, before, out->count);

    if (m->max_results > 0 && out->count - before > m->max_results)
        results_drop(out, before + m->max_results);
}

void mrun_search(const wchar_t *query) {
    results_drop(&mr.results, 0);
    mr.sel    = 0;
    mr.scroll = 0;

    const wchar_t *effective = query;
    mr.routed = route(query, &effective);

    if (mr.routed) {
        run_module(mr.routed, effective, &mr.results);
        return;
    }

    for (int i = 0; i < mr.order_count; i++)
        run_module(&mr.modules[mr.order[i]], query, &mr.results);

    sort_range(&mr.results, 0, mr.results.count);
}

bool mrun_activate(int index) {
    if (index < 0 || index >= mr.results.count) return false;

    MrunItem   *item = &mr.results.items[index];
    MrunModule *m    = item->owner;

    if (m && m->activate) return m->activate(m, item);
    if (item->exec[0])    return mrun_spawn(item->exec, item->args, item->cwd);
    return true;
}
