#include "mrun_update.h"

static const wchar_t POWERSHELL_QUOTES[] = L"'\u2018\u2019\u201A\u201B";

typedef struct {
    wchar_t *buf;
    size_t   cap;
    size_t   len;
    bool     overflow;
} Builder;

static void put(Builder *b, wchar_t c) {
    if (b->len + 1 >= b->cap) { b->overflow = true; return; }
    b->buf[b->len++] = c;
}

static void put_text(Builder *b, const wchar_t *text) {
    for (; *text; text++) put(b, *text);
}

static void put_single_quoted(Builder *b, const wchar_t *text) {
    put(b, L'\'');
    for (; *text; text++) {
        if (wcschr(POWERSHELL_QUOTES, *text)) put(b, *text);
        put(b, *text);
    }
    put(b, L'\'');
}

static bool usable(const wchar_t *s) {
    return s && s[0] && !wcschr(s, L'"');
}

bool mrun_update_params(const wchar_t *script_url, const wchar_t *install_dir,
                        wchar_t *out, size_t cap) {
    if (!out || cap == 0) return false;
    out[0] = L'\0';
    if (!usable(script_url) || !usable(install_dir)) return false;

    Builder b = { out, cap, 0, false };

    put_text(&b, L"-NoProfile -Command \"$ErrorActionPreference = 'Stop'; "
                 L"try { & ([scriptblock]::Create((Invoke-RestMethod ");
    put_single_quoted(&b, script_url);
    put_text(&b, L"))) -InstallDir ");
    put_single_quoted(&b, install_dir);
    put_text(&b, L" -SkipPath; Start-Sleep -Seconds 3 } "
                 L"catch { Write-Host $_ -ForegroundColor Red; "
                 L"Read-Host 'Press Enter to close' }\"");

    out[b.overflow ? 0 : b.len] = L'\0';
    return !b.overflow;
}
