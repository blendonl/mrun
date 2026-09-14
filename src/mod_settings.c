#include "mrun.h"

#define UPDATE_PARAMS_CAP  (1024 + 2 * MAX_PATH)
#define UPDATE_CONFIRM_CAP (512 + MAX_PATH)

typedef struct {
    const wchar_t *title;
    const wchar_t *subtitle;
    void         (*run)(void);
} SettingsEntry;

static bool exe_folder(wchar_t *out, DWORD cap) {
    DWORD n = GetModuleFileNameW(NULL, out, cap);
    if (n == 0 || n >= cap) return false;

    wchar_t *slash = wcsrchr(out, L'\\');
    if (!slash) return false;
    slash[1] = L'\0';
    return true;
}

static bool launch_updater(const wchar_t *folder) {
    wchar_t params[UPDATE_PARAMS_CAP];
    if (!mrun_update_params(MRUN_UPDATE_SCRIPT, folder, params,
                            UPDATE_PARAMS_CAP)) {
        log_err(L"mrun/settings: cannot build an update command for %ls",
                folder);
        return false;
    }

    log_msg(LOG_INFO, L"mrun/settings: installing the latest release over %ls",
            folder);
    return mrun_spawn(MRUN_UPDATE_SHELL, params, NULL);
}

bool mrun_update_start(void) {
    wchar_t folder[MAX_PATH];
    if (!exe_folder(folder, MAX_PATH)) {
        log_err(L"mrun/settings: cannot tell which folder mrun.exe is in");
        return false;
    }
    return launch_updater(folder);
}

static bool update_confirmed(const wchar_t *folder) {
    wchar_t text[UPDATE_CONFIRM_CAP];
    _snwprintf(text, UPDATE_CONFIRM_CAP,
               L"Replace mrun %hs in\n%ls\nwith the latest release from "
               L"GitHub?\n\nmrun closes while its files are replaced and "
               L"starts again afterwards. If you already have the latest "
               L"release, nothing changes.",
               MRUN_VERSION, folder);
    text[UPDATE_CONFIRM_CAP - 1] = L'\0';

    return MessageBoxW(NULL, text, L"Update mrun",
                       MB_YESNO | MB_ICONQUESTION | MB_TOPMOST |
                       MB_SETFOREGROUND) == IDYES;
}

static void update_mrun(void) {
    wchar_t folder[MAX_PATH];
    if (!exe_folder(folder, MAX_PATH)) {
        log_err(L"mrun/settings: cannot tell which folder mrun.exe is in");
        return;
    }
    if (update_confirmed(folder)) launch_updater(folder);
}

static const SettingsEntry ENTRIES[] = {
    { L"Update mrun",
      L"Enter to install the latest release (you have " MRUN_VERSION L")",
      update_mrun },
};

#define ENTRY_COUNT ((int)(sizeof ENTRIES / sizeof ENTRIES[0]))

static const SettingsEntry *entry_titled(const wchar_t *title) {
    for (int i = 0; i < ENTRY_COUNT; i++)
        if (wcscmp(ENTRIES[i].title, title) == 0) return &ENTRIES[i];
    return NULL;
}

static void settings_search(MrunModule *m, const wchar_t *query,
                            MrunResults *out) {
    if ((!query || !query[0]) && mr.routed != m) return;

    for (int i = 0; i < ENTRY_COUNT; i++) {
        int score = mrun_score(query, ENTRIES[i].title);
        if (score == MRUN_NO_MATCH) continue;

        MrunItem item;
        memset(&item, 0, sizeof(item));
        item.lua_ref = LUA_NOREF;
        mrun_copy_w(item.title,    MRUN_TITLE_CAP, ENTRIES[i].title);
        mrun_copy_w(item.subtitle, MRUN_SUB_CAP,   ENTRIES[i].subtitle);

        mrun_results_add(out, m, &item, score);
    }
}

static bool settings_activate(MrunModule *m, const MrunItem *item) {
    const SettingsEntry *entry = entry_titled(item->title);
    if (!entry) return true;

    mrun_ui_hide();
    entry->run();
    return true;
}

void mod_settings_register(void) {
    MrunModule *m = mrun_module_new(L"settings");
    if (!m) return;

    mrun_copy_w(m->description, MRUN_DESC_CAP,
                L"mrun's own settings and maintenance, such as updating it");
    m->enabled_when_unlisted = true;
    m->search                = settings_search;
    m->activate              = settings_activate;
}
