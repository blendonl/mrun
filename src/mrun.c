#define COBJMACROS
#include "mrun.h"

Mrun mr;

#define MRUN_COPY_QUERY 1
#define MRUN_COPY_MODE  2

void mrun_copy_w(wchar_t *out, size_t cap, const wchar_t *src) {
    if (!out || cap == 0) return;
    if (!src) { out[0] = L'\0'; return; }

    size_t len = wcslen(src);
    if (len >= cap) len = cap - 1;
    memcpy(out, src, len * sizeof(wchar_t));
    out[len] = L'\0';
}

void mrun_utf8_to_w(const char *s, wchar_t *out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = L'\0';
    if (!s) return;

    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
    if (n <= 0) out[0] = L'\0';
    else        out[cap - 1] = L'\0';
}

wchar_t *mrun_utf8_to_w_dup(const char *s) {
    if (!s) return NULL;

    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;

    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;

    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

void mrun_push_w(lua_State *L, const wchar_t *w) {
    if (!w || !w[0]) { lua_pushstring(L, ""); return; }

    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) { lua_pushstring(L, ""); return; }

    char  stack[512];
    char *buf = (n <= (int)sizeof stack) ? stack : (char *)malloc((size_t)n);
    if (!buf) { lua_pushstring(L, ""); return; }

    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, n, NULL, NULL);
    lua_pushstring(L, buf);
    if (buf != stack) free(buf);
}

bool mrun_set_clipboard(const wchar_t *text) {
    if (!text) return false;
    if (!OpenClipboard(NULL)) return false;

    EmptyClipboard();

    size_t  bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL mem   = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) { CloseClipboard(); return false; }

    void *dst = GlobalLock(mem);
    if (!dst) { GlobalFree(mem); CloseClipboard(); return false; }

    memcpy(dst, text, bytes);
    GlobalUnlock(mem);

    bool ok = SetClipboardData(CF_UNICODETEXT, mem) != NULL;
    if (!ok) GlobalFree(mem);

    CloseClipboard();
    return ok;
}

static const wchar_t MRUN_APPS_FOLDER[] = L"shell:AppsFolder\\";

static const PROPERTYKEY PKEY_APP_HOST_ENVIRONMENT = {
    { 0x9f4c2855, 0x9f79, 0x4b39,
      { 0xa8, 0xd0, 0xe1, 0xd4, 0x2d, 0xe1, 0xd5, 0xf3 } },
    14
};

enum { APP_HOST_IMMERSIVE = 1 };

static const wchar_t *packaged_app_id(const wchar_t *cmd) {
    size_t prefix = ARRAYSIZE(MRUN_APPS_FOLDER) - 1;
    if (_wcsnicmp(cmd, MRUN_APPS_FOLDER, prefix) != 0) return NULL;
    return cmd[prefix] ? cmd + prefix : NULL;
}

static bool packaged_app_is_desktop(const wchar_t *parsing_name) {
    IShellItem2 *item = NULL;
    if (FAILED(SHCreateItemFromParsingName(parsing_name, NULL, &IID_IShellItem2,
                                           (void **)&item)))
        return false;

    ULONG   host = APP_HOST_IMMERSIVE;
    HRESULT hr   = IShellItem2_GetUInt32(item, &PKEY_APP_HOST_ENVIRONMENT,
                                         &host);
    IShellItem2_Release(item);
    return SUCCEEDED(hr) && host != APP_HOST_IMMERSIVE;
}

static HRESULT activate_packaged_app(const wchar_t *aumid,
                                     const wchar_t *params) {
    IApplicationActivationManager *manager = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_ApplicationActivationManager, NULL,
                                  CLSCTX_SERVER,
                                  &IID_IApplicationActivationManager,
                                  (void **)&manager);
    if (FAILED(hr)) return hr;

    CoAllowSetForegroundWindow((IUnknown *)manager, NULL);

    DWORD pid = 0;
    hr = IApplicationActivationManager_ActivateApplication(manager, aumid,
                                                           params, AO_NONE,
                                                           &pid);
    IApplicationActivationManager_Release(manager);
    return hr;
}

bool mrun_spawn(const wchar_t *cmd, const wchar_t *args, const wchar_t *cwd) {
    if (!cmd || !cmd[0]) return true;

    const wchar_t *params = (args && args[0]) ? args : NULL;
    const wchar_t *dir    = (cwd  && cwd[0])  ? cwd  : NULL;

    const wchar_t *aumid = packaged_app_id(cmd);
    if (aumid && packaged_app_is_desktop(cmd)) {
        HRESULT hr = activate_packaged_app(aumid, params);
        if (SUCCEEDED(hr)) {
            log_w(L"mrun: activated '%ls'%ls%ls", aumid, params ? L" " : L"",
                  params ? params : L"");
            return true;
        }
        log_msg(LOG_WARN, L"mrun: could not activate '%ls' (0x%08lX), "
                          L"trying ShellExecute", aumid, (unsigned long)hr);
    }

    INT_PTR code = (INT_PTR)ShellExecuteW(NULL, L"open", cmd, params, dir,
                                          SW_SHOWNORMAL);
    if (code <= 32) {
        log_err(L"mrun: failed to launch '%ls'%ls%ls (code %lld)", cmd,
                params ? L" " : L"", params ? params : L"", (long long)code);
        return false;
    }

    log_w(L"mrun: launched '%ls'%ls%ls", cmd, params ? L" " : L"",
          params ? params : L"");
    return true;
}

typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);

UINT mrun_window_dpi(HWND hwnd) {
    static GetDpiForWindowFn fn;
    static bool              probed;

    if (!probed) {
        probed = true;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32)
            fn = (GetDpiForWindowFn)(void *)
                     GetProcAddress(user32, "GetDpiForWindow");
    }

    if (fn && hwnd) {
        UINT dpi = fn(hwnd);
        if (dpi) return dpi;
    }

    HDC screen = GetDC(NULL);
    UINT dpi = screen ? (UINT)GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen) ReleaseDC(NULL, screen);
    return dpi ? dpi : 96;
}

int mrun_scale(int px, UINT dpi) {
    if (dpi == 0) dpi = 96;
    return MulDiv(px, (int)dpi, 96);
}

static void console_print(const char *s) {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(out, s, (DWORD)strlen(s), &written, NULL);
        WriteFile(out, "\r\n", 2, &written, NULL);
    }
    FreeConsole();
}

typedef struct {
    UINT     message;
    wchar_t  config[MAX_PATH];
    wchar_t  text[MRUN_QUERY_CAP];
    ULONG    copy_kind;
    bool     daemon;
    bool     check;
    bool     version;
    bool     update;
    bool     usage;
    LogLevel level;
    bool     level_set;
} Options;

static bool match(const wchar_t *arg, const wchar_t *name) {
    return _wcsicmp(arg, name) == 0;
}

static void parse_args(Options *opt) {
    memset(opt, 0, sizeof(*opt));
    opt->message = WM_MRUN_TOGGLE;
    opt->level   = LOG_INFO;

    int      argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;

    for (int i = 1; i < argc; i++) {
        const wchar_t *a = argv[i];
        const wchar_t *next = (i + 1 < argc) ? argv[i + 1] : NULL;

        if      (match(a, L"--show"))    opt->message = WM_MRUN_SHOW;
        else if (match(a, L"--hide"))    opt->message = WM_MRUN_HIDE;
        else if (match(a, L"--toggle"))  opt->message = WM_MRUN_TOGGLE;
        else if (match(a, L"--reload"))  opt->message = WM_MRUN_RELOAD;
        else if (match(a, L"--quit"))    opt->message = WM_MRUN_QUIT;
        else if (match(a, L"--daemon"))  opt->daemon  = true;
        else if (match(a, L"--check"))   opt->check   = true;
        else if (match(a, L"--version")) opt->version = true;
        else if (match(a, L"--update"))  opt->update  = true;
        else if (match(a, L"--help") || match(a, L"-h")) opt->usage = true;
        else if (match(a, L"--config") && next) {
            mrun_copy_w(opt->config, MAX_PATH, next);
            i++;
        } else if ((match(a, L"--mode") || match(a, L"-m")) && next) {
            mrun_copy_w(opt->text, MRUN_QUERY_CAP, next);
            opt->copy_kind = MRUN_COPY_MODE;
            i++;
        } else if (match(a, L"--query") && next) {
            mrun_copy_w(opt->text, MRUN_QUERY_CAP, next);
            opt->copy_kind = MRUN_COPY_QUERY;
            i++;
        } else if (match(a, L"--log-level") && next) {
            char name[32];
            WideCharToMultiByte(CP_UTF8, 0, next, -1, name, (int)sizeof name,
                                NULL, NULL);
            if (log_level_from_name(name, &opt->level)) opt->level_set = true;
            i++;
        } else {
            opt->usage = true;
        }
    }

    LocalFree(argv);
}

static const char *USAGE =
    "mrun " MRUN_VERSION " - a modular keyboard launcher for Windows\n"
    "\n"
    "  mrun                     toggle the launcher (starts it if needed)\n"
    "  mrun --show|--hide       show or hide it\n"
    "  mrun --toggle            toggle it\n"
    "  mrun --daemon            start resident and hidden\n"
    "  mrun --mode <module>     open with that module's prefix typed in\n"
    "  mrun --query <text>      open with <text> typed in\n"
    "  mrun --reload            reload the config of the running instance\n"
    "  mrun --quit              stop the running instance\n"
    "  mrun --update            install the latest release over this copy\n"
    "  mrun --check             validate the config and exit\n"
    "  mrun --config <path>     use this config file\n"
    "  mrun --log-level <lvl>   error|warn|info|debug|trace\n"
    "  mrun --version           print the version and exit";

static HWND find_resident(void) {
    for (int attempt = 0; attempt < 40; attempt++) {
        HWND hwnd = FindWindowW(MRUN_CLASS, NULL);
        if (hwnd) return hwnd;
        Sleep(50);
    }
    return NULL;
}

static int run_as_client(const Options *opt) {
    HWND resident = find_resident();
    if (!resident) {
        console_print("error: mrun is starting up or wedged; try again");
        return 1;
    }

    if (opt->copy_kind && opt->message != WM_MRUN_HIDE &&
        opt->message != WM_MRUN_QUIT) {
        COPYDATASTRUCT cd = {
            .dwData = opt->copy_kind,
            .cbData = (DWORD)((wcslen(opt->text) + 1) * sizeof(wchar_t)),
            .lpData = (void *)opt->text,
        };
        SendMessageW(resident, WM_COPYDATA, 0, (LPARAM)&cd);
        return 0;
    }

    PostMessageW(resident, opt->message, 0, 0);
    return 0;
}

void mrun_apply_copydata(ULONG kind, const wchar_t *text) {
    if (kind == MRUN_COPY_MODE) {
        MrunModule *m = mrun_module_find(text);
        mrun_ui_set_query(m && m->prefix[0] ? m->prefix : L"");
    } else {
        mrun_ui_set_query(text);
    }
    mrun_ui_show();
}

static int run_check(const Options *opt) {
    mrun_config_resolve_path(mr.config_path, MAX_PATH, opt->config);

    char path[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, mr.config_path, -1, path, (int)sizeof path,
                        NULL, NULL);

    char msg[2048];
    bool ok = mrun_config_load();

    if (ok) {
        snprintf(msg, sizeof msg, "ok: %s\n  %d module(s) active of %d",
                 path, mr.order_count, mr.module_count);
        console_print(msg);
    } else {
        snprintf(msg, sizeof msg,
                 "FAILED: %s\n  %s\n  Nothing in this file took effect; "
                 "mrun would run with built-in defaults.",
                 path, mr.config_error);
        console_print(msg);
    }

    mrun_config_shutdown();
    return ok ? 0 : 1;
}

static int run_update(void) {
    if (mrun_update_start()) return 0;

    console_print("error: could not start the updater");
    return 1;
}

static int run(HINSTANCE hInstance) {
    mr.hinst = hInstance;
    mrun_appearance_defaults(&mr.look);

    Options opt;
    parse_args(&opt);

    if (opt.usage)   { console_print(USAGE);        return 0; }
    if (opt.version) { console_print(MRUN_VERSION); return 0; }
    if (opt.check)   { return run_check(&opt); }
    if (opt.update)  { return run_update(); }

    HANDLE once = CreateMutexW(NULL, TRUE, MRUN_MUTEX);
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
        int rc = run_as_client(&opt);
        CloseHandle(once);
        return rc;
    }

    log_init(L"mrun", opt.level_set ? opt.level : LOG_INFO);
    log_msg(LOG_INFO, L"mrun %hs starting", MRUN_VERSION);

    mrun_config_resolve_path(mr.config_path, MAX_PATH, opt.config);
    mrun_config_load();

    if (!mrun_ui_init()) {
        log_err(L"mrun: could not create its window — giving up");
        mrun_config_shutdown();
        if (once) CloseHandle(once);
        log_shutdown();
        return 1;
    }

    if (opt.message == WM_MRUN_QUIT) {
        mrun_ui_shutdown();
        mrun_config_shutdown();
        if (once) CloseHandle(once);
        log_shutdown();
        return 0;
    }

    if (!opt.daemon) {
        if (opt.copy_kind) mrun_apply_copydata(opt.copy_kind, opt.text);
        else if (opt.message != WM_MRUN_HIDE) mrun_ui_show();
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    mrun_ui_shutdown();
    mrun_config_shutdown();
    if (once) CloseHandle(once);
    log_msg(LOG_INFO, L"mrun: exiting");
    log_shutdown();
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED |
                                       COINIT_DISABLE_OLE1DDE);
    int rc = run(hInstance);
    if (SUCCEEDED(com)) CoUninitialize();
    return rc;
}
