#ifndef MRUN_H
#define MRUN_H

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "log.h"
#include "mrun_score.h"

#ifndef MRUN_VERSION
#define MRUN_VERSION "0.0.0"
#endif

#define MRUN_CLASS       L"mrun_Window"
#define MRUN_MUTEX       L"Local\\mrun_singleton"

#define MRUN_MAX_MODULES 32
#define MRUN_MAX_ITEMS   512
#define MRUN_QUERY_CAP   256
#define MRUN_TITLE_CAP   192
#define MRUN_SUB_CAP     192
#define MRUN_ARGS_CAP    512
#define MRUN_NAME_CAP    64
#define MRUN_PREFIX_CAP  8
#define MRUN_DESC_CAP    128

#define WM_MRUN_SHOW   (WM_APP + 1)
#define WM_MRUN_HIDE   (WM_APP + 2)
#define WM_MRUN_TOGGLE (WM_APP + 3)
#define WM_MRUN_RELOAD (WM_APP + 4)
#define WM_MRUN_QUIT   (WM_APP + 5)
#define WM_MRUN_ICON   (WM_APP + 6)

typedef struct MrunModule MrunModule;

typedef struct {
    wchar_t     title[MRUN_TITLE_CAP];
    wchar_t     subtitle[MRUN_SUB_CAP];
    wchar_t     exec[MAX_PATH];
    wchar_t     args[MRUN_ARGS_CAP];
    wchar_t     cwd[MAX_PATH];
    wchar_t     icon[MAX_PATH];
    int         lua_ref;
    int         score;
    int         seq;
    MrunModule *owner;
} MrunItem;

typedef struct {
    MrunItem items[MRUN_MAX_ITEMS];
    int      count;
} MrunResults;

struct MrunModule {
    wchar_t name[MRUN_NAME_CAP];
    wchar_t prefix[MRUN_PREFIX_CAP];
    wchar_t description[MRUN_DESC_CAP];
    bool    enabled;
    bool    is_lua;
    int     max_results;

    bool (*init)(MrunModule *m);
    void (*shutdown)(MrunModule *m);
    void (*configure)(MrunModule *m, lua_State *L, int tbl);
    void (*search)(MrunModule *m, const wchar_t *query, MrunResults *out);
    bool (*activate)(MrunModule *m, const MrunItem *item);

    int   lua_search;
    int   lua_activate;
    int   lua_reload;
    void *state;
};

typedef enum {
    MRUN_POS_TOP = 0,
    MRUN_POS_CENTER,
} MrunPosition;

typedef struct {
    int          width;
    int          rows;
    int          row_height;
    int          input_height;
    int          padding;
    int          font_size;
    int          sub_font_size;
    int          icon_size;
    wchar_t      font[LF_FACESIZE];
    COLORREF     bg;
    COLORREF     fg;
    COLORREF     dim;
    COLORREF     sel_bg;
    COLORREF     sel_fg;
    COLORREF     border;
    COLORREF     prompt_fg;
    COLORREF     accent;
    int          border_width;
    int          opacity;
    int          corner_radius;
    bool         rounded;
    bool         show_subtitles;
    bool         show_icons;
    bool         show_module;
    bool         show_scrollbar;
    MrunPosition position;
    int          top_offset;
    wchar_t      prompt[32];
    wchar_t      placeholder[96];
    wchar_t      empty_text[96];
} MrunAppearance;

typedef struct {
    HINSTANCE      hinst;
    HWND           window;
    lua_State     *L;
    wchar_t        config_path[MAX_PATH];
    char           config_error[1024];
    bool           config_ok;

    MrunModule     modules[MRUN_MAX_MODULES];
    int            module_count;
    int            order[MRUN_MAX_MODULES];
    int            order_count;
    wchar_t        order_names[MRUN_MAX_MODULES][MRUN_NAME_CAP];
    int            order_names_count;
    bool           order_explicit;

    MrunAppearance look;

    wchar_t        query[MRUN_QUERY_CAP];
    int            query_len;
    MrunResults    results;
    int            sel;
    int            scroll;
    MrunModule    *routed;

    bool           visible;
    bool           quitting;
    bool           hide_on_blur;
    bool           clear_on_hide;
} Mrun;

extern Mrun mr;

void     mrun_utf8_to_w(const char *s, wchar_t *out, int cap);
wchar_t *mrun_utf8_to_w_dup(const char *s);
void     mrun_push_w(lua_State *L, const wchar_t *w);
void     mrun_copy_w(wchar_t *out, size_t cap, const wchar_t *src);
bool     mrun_set_clipboard(const wchar_t *text);
bool     mrun_spawn(const wchar_t *cmd, const wchar_t *args, const wchar_t *cwd);
UINT     mrun_window_dpi(HWND hwnd);
int      mrun_scale(int px, UINT dpi);

MrunModule *mrun_module_new(const wchar_t *name);
MrunModule *mrun_module_find(const wchar_t *name);
void        mrun_modules_init(void);
void        mrun_modules_shutdown(void);
void        mrun_modules_reset(void);
void        mrun_modules_reload_data(void);
void        mrun_module_set_order(const wchar_t **names, int count);
void        mrun_results_add(MrunResults *out, MrunModule *owner,
                             const MrunItem *item, int score);
void        mrun_search(const wchar_t *query);
void        mrun_results_reset(void);
bool        mrun_activate(int index);

void mod_apps_register(void);

void mrun_appearance_defaults(MrunAppearance *look);
bool mrun_config_load(void);
void mrun_config_resolve_path(wchar_t *out, size_t cap, const wchar_t *override);
void mrun_config_shutdown(void);
void mrun_lua_register(lua_State *L);
void mrun_lua_module_search(MrunModule *m, const wchar_t *query,
                            MrunResults *out);
bool mrun_lua_module_activate(MrunModule *m, const MrunItem *item);
void mrun_lua_module_free(MrunModule *m);

void mrun_apply_copydata(ULONG kind, const wchar_t *text);

bool mrun_ui_init(void);
void mrun_ui_shutdown(void);
void mrun_ui_show(void);
void mrun_ui_hide(void);
void mrun_ui_toggle(void);
void mrun_ui_refresh(void);
void mrun_ui_set_query(const wchar_t *text);

HBITMAP mrun_icons_get(const wchar_t *source, int size);
bool    mrun_icons_deliver(WPARAM tag, LPARAM bitmap);
void    mrun_icons_flush(void);
void    mrun_icons_shutdown(void);

#endif
