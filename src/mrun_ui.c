#include "mrun.h"

typedef struct {
    HFONT   font;
    UINT    dpi;
    int     px;
    int     weight;
    wchar_t face[LF_FACESIZE];
} FontCache;

static FontCache s_title;
static FontCache s_sub;
static FontCache s_input;

static HFONT font_get(FontCache *fc, UINT dpi, int px, int weight,
                      const wchar_t *face) {
    if (px < 1) px = 1;
    if (!face || !face[0]) face = L"Segoe UI";

    if (fc->font && fc->dpi == dpi && fc->px == px && fc->weight == weight &&
        wcscmp(fc->face, face) == 0)
        return fc->font;

    if (fc->font) DeleteObject(fc->font);

    fc->font = CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, face);
    fc->dpi    = dpi;
    fc->px     = px;
    fc->weight = weight;
    mrun_copy_w(fc->face, LF_FACESIZE, face);
    return fc->font;
}

static void font_free(FontCache *fc) {
    if (fc->font) DeleteObject(fc->font);
    memset(fc, 0, sizeof(*fc));
}

static void fill(HDC dc, const RECT *rc, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    FillRect(dc, rc, br);
    DeleteObject(br);
}

static void draw_text(HDC dc, RECT rc, const wchar_t *text, COLORREF color,
                      UINT flags) {
    if (!text || !text[0]) return;
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rc,
              flags | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX |
              DT_END_ELLIPSIS);
}

static void draw_icon(HDC dc, HBITMAP icon, int x, int y, int size) {
    HDC src = CreateCompatibleDC(dc);
    if (!src) return;

    HGDIOBJ       old   = SelectObject(src, icon);
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    GdiAlphaBlend(dc, x, y, size, size, src, 0, 0, size, size, blend);
    SelectObject(src, old);
    DeleteDC(src);
}

static int visible_rows(void) {
    int rows = mr.look.rows;
    if (mr.results.count < rows) rows = mr.results.count;
    return rows;
}

static int list_rows(void) {
    int rows = visible_rows();
    if (rows == 0 && mr.query_len > 0) rows = 1;
    return rows;
}

static void clamp_scroll(void) {
    int rows = mr.look.rows;
    if (mr.sel < 0) mr.sel = 0;
    if (mr.sel >= mr.results.count) mr.sel = mr.results.count - 1;
    if (mr.sel < 0) mr.sel = 0;

    if (mr.sel < mr.scroll)             mr.scroll = mr.sel;
    if (mr.sel >= mr.scroll + rows)     mr.scroll = mr.sel - rows + 1;
    if (mr.scroll < 0)                  mr.scroll = 0;
}

static void geometry(UINT dpi, int *out_w, int *out_h) {
    const MrunAppearance *a = &mr.look;

    int pad   = mrun_scale(a->padding,      dpi);
    int input = mrun_scale(a->input_height, dpi);
    int row   = mrun_scale(a->row_height,   dpi);

    int rows = list_rows();

    *out_w = mrun_scale(a->width, dpi);
    *out_h = input + rows * row;
    if (rows > 0) *out_h += pad;
}

static void relayout(void) {
    if (!mr.window) return;

    POINT cursor;
    GetCursorPos(&cursor);

    HMONITOR      hmon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO   mi   = { .cbSize = sizeof(mi) };
    RECT          work = { 0, 0, 1920, 1080 };
    if (GetMonitorInfoW(hmon, &mi)) work = mi.rcWork;

    UINT dpi = mrun_window_dpi(mr.window);

    int w, h;
    geometry(dpi, &w, &h);

    int span_w = work.right - work.left;
    int span_h = work.bottom - work.top;
    if (w > span_w) w = span_w;
    if (h > span_h) h = span_h;

    int x = work.left + (span_w - w) / 2;
    int y = (mr.look.position == MRUN_POS_CENTER)
            ? work.top + (span_h - h) / 2
            : work.top + (span_h * mr.look.top_offset) / 100;
    if (y + h > work.bottom) y = work.bottom - h;
    if (y < work.top)        y = work.top;

    SetWindowPos(mr.window, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);

    HRGN region = NULL;
    if (mr.look.rounded) {
        int radius = mrun_scale(mr.look.corner_radius, dpi);
        if (radius > 0)
            region = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius * 2,
                                        radius * 2);
    }
    SetWindowRgn(mr.window, region, TRUE);

    InvalidateRect(mr.window, NULL, TRUE);
}

static void paint_row(HDC dc, const MrunItem *item, RECT row, bool selected,
                      UINT dpi) {
    const MrunAppearance *a = &mr.look;

    if (selected) {
        RECT hl = row;
        fill(dc, &hl, a->sel_bg);

        RECT marker = { row.left, row.top,
                        row.left + mrun_scale(3, dpi), row.bottom };
        fill(dc, &marker, a->accent);
    }

    int pad  = mrun_scale(a->padding, dpi);
    RECT text = { row.left + pad, row.top, row.right - pad, row.bottom };

    if (a->show_icons) {
        int room = row.bottom - row.top;
        int size = mrun_scale(a->icon_size, dpi);
        if (size > room) size = room;

        HBITMAP icon = mrun_icons_get(item->icon, size);
        if (icon)
            draw_icon(dc, icon, text.left, row.top + (room - size) / 2, size);
        text.left += size + pad;
    }

    if (a->show_module && item->owner && !mr.routed) {
        SelectObject(dc, font_get(&s_sub, dpi,
                                  mrun_scale(a->sub_font_size, dpi),
                                  FW_NORMAL, a->font));
        SIZE badge;
        int  len = (int)wcslen(item->owner->name);
        if (GetTextExtentPoint32W(dc, item->owner->name, len, &badge)) {
            RECT br = { text.right - badge.cx, row.top, text.right,
                        row.bottom };
            draw_text(dc, br, item->owner->name, a->dim, DT_RIGHT);
            text.right -= badge.cx + pad;
        }
    }

    bool has_sub = a->show_subtitles && item->subtitle[0];

    SelectObject(dc, font_get(&s_title, dpi, mrun_scale(a->font_size, dpi),
                              FW_NORMAL, a->font));
    RECT title = text;
    if (has_sub) title.bottom = row.top + (row.bottom - row.top) / 2;
    draw_text(dc, title, item->title, selected ? a->sel_fg : a->fg, DT_LEFT);

    if (has_sub) {
        SelectObject(dc, font_get(&s_sub, dpi,
                                  mrun_scale(a->sub_font_size, dpi),
                                  FW_NORMAL, a->font));
        RECT sub = text;
        sub.top = row.top + (row.bottom - row.top) / 2;
        draw_text(dc, sub, item->subtitle, a->dim, DT_LEFT);
    }
}

static void paint(HWND hwnd) {
    const MrunAppearance *a = &mr.look;

    PAINTSTRUCT ps;
    HDC         hdc = BeginPaint(hwnd, &ps);

    RECT client;
    GetClientRect(hwnd, &client);
    int w = client.right, h = client.bottom;
    if (w <= 0 || h <= 0) { EndPaint(hwnd, &ps); return; }

    HDC     mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    if (!mem || !bmp) {
        if (bmp) DeleteObject(bmp);
        if (mem) DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return;
    }
    HBITMAP old_bmp = (HBITMAP)SelectObject(mem, bmp);

    UINT dpi   = mrun_window_dpi(hwnd);
    int  pad   = mrun_scale(a->padding,      dpi);
    int  input = mrun_scale(a->input_height, dpi);
    int  row_h = mrun_scale(a->row_height,   dpi);

    RECT all = { 0, 0, w, h };
    fill(mem, &all, a->bg);
    SetBkMode(mem, TRANSPARENT);

    HFONT input_font = font_get(&s_input, dpi,
                                mrun_scale(a->font_size + 3, dpi),
                                FW_NORMAL, a->font);
    HFONT old_font = (HFONT)SelectObject(mem, input_font);

    int prompt_w = 0;
    if (a->prompt[0]) {
        SIZE size;
        int  len = (int)wcslen(a->prompt);
        if (GetTextExtentPoint32W(mem, a->prompt, len, &size)) {
            RECT pr = { pad, 0, pad + size.cx, input };
            draw_text(mem, pr, a->prompt, a->prompt_fg, DT_LEFT);
            prompt_w = size.cx + pad / 2;
        }
    }

    RECT query = { pad + prompt_w, 0, w - pad, input };
    if (mr.query_len > 0) {
        draw_text(mem, query, mr.query, a->fg, DT_LEFT);
    } else {
        draw_text(mem, query, a->placeholder, a->dim, DT_LEFT);
    }

    if (mr.routed) {
        SelectObject(mem, font_get(&s_sub, dpi,
                                   mrun_scale(a->sub_font_size, dpi),
                                   FW_NORMAL, a->font));
        RECT tag = { w / 2, 0, w - pad, input };
        draw_text(mem, tag, mr.routed->name, a->accent, DT_RIGHT);
        SelectObject(mem, input_font);
    }

    RECT rule = { 0, input - mrun_scale(1, dpi), w, input };
    fill(mem, &rule, a->border);

    int rows = visible_rows();
    for (int i = 0; i < rows; i++) {
        int index = mr.scroll + i;
        if (index >= mr.results.count) break;

        RECT row = { 0, input + pad / 2 + i * row_h,
                     w, input + pad / 2 + (i + 1) * row_h };
        paint_row(mem, &mr.results.items[index], row, index == mr.sel, dpi);
    }

    if (mr.results.count == 0 && mr.query_len > 0) {
        SelectObject(mem, font_get(&s_title, dpi,
                                   mrun_scale(a->font_size, dpi),
                                   FW_NORMAL, a->font));
        RECT empty = { pad, input + pad / 2, w - pad, input + pad / 2 + row_h };
        draw_text(mem, empty, a->empty_text, a->dim, DT_LEFT);
    }

    if (a->show_scrollbar && mr.results.count > a->rows) {
        int track_top = input + pad / 2;
        int track_h   = rows * row_h;
        int bar_h     = track_h * a->rows / mr.results.count;
        if (bar_h < mrun_scale(16, dpi)) bar_h = mrun_scale(16, dpi);

        int span   = mr.results.count - a->rows;
        int offset = span > 0 ? (track_h - bar_h) * mr.scroll / span : 0;
        int width  = mrun_scale(3, dpi);

        RECT bar = { w - width - mrun_scale(2, dpi), track_top + offset,
                     w - mrun_scale(2, dpi), track_top + offset + bar_h };
        fill(mem, &bar, a->border);
    }

    if (a->border_width > 0) {
        int bw = mrun_scale(a->border_width, dpi);
        RECT edges[] = {
            { 0, 0, w, bw }, { 0, h - bw, w, h },
            { 0, 0, bw, h }, { w - bw, 0, w, h },
        };
        for (int i = 0; i < 4; i++) fill(mem, &edges[i], a->border);
    }

    SelectObject(mem, old_font);
    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void mrun_ui_refresh(void) {
    mrun_search(mr.query);
    clamp_scroll();
    relayout();
}

void mrun_ui_set_query(const wchar_t *text) {
    mrun_copy_w(mr.query, MRUN_QUERY_CAP, text ? text : L"");
    mr.query_len = (int)wcslen(mr.query);
    mrun_ui_refresh();
}

static void force_foreground(HWND hwnd) {
    DWORD self  = GetCurrentThreadId();
    HWND  front = GetForegroundWindow();
    DWORD owner = front ? GetWindowThreadProcessId(front, NULL) : 0;
    bool  attached = owner && owner != self &&
                     AttachThreadInput(self, owner, TRUE);

    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    if (attached) AttachThreadInput(self, owner, FALSE);
}

void mrun_ui_show(void) {
    if (!mr.window) return;

    if (mr.clear_on_hide) {
        mr.query[0]  = L'\0';
        mr.query_len = 0;
    }

    mrun_search(mr.query);
    clamp_scroll();
    relayout();

    mr.visible = true;
    ShowWindow(mr.window, SW_SHOW);
    SetWindowPos(mr.window, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    force_foreground(mr.window);
}

void mrun_ui_hide(void) {
    if (!mr.window || !mr.visible) return;

    mr.visible = false;
    ShowWindow(mr.window, SW_HIDE);

    if (mr.clear_on_hide) {
        mr.query[0]  = L'\0';
        mr.query_len = 0;
        mrun_search(mr.query);
    }
}

void mrun_ui_toggle(void) {
    if (mr.visible) mrun_ui_hide();
    else            mrun_ui_show();
}

static void query_backspace_word(void) {
    while (mr.query_len > 0 && mr.query[mr.query_len - 1] == L' ')
        mr.query[--mr.query_len] = L'\0';
    while (mr.query_len > 0 && mr.query[mr.query_len - 1] != L' ')
        mr.query[--mr.query_len] = L'\0';
}

static void activate_selection(void) {
    bool close;

    if (mr.results.count > 0) {
        close = mrun_activate(mr.sel);
    } else if (mr.query_len > 0) {
        close = mrun_spawn(mr.query, NULL, NULL);
    } else {
        return;
    }

    if (close) mrun_ui_hide();
    else       mrun_ui_refresh();
}

static void complete_selection(void) {
    if (mr.sel < 0 || mr.sel >= mr.results.count) return;

    const MrunItem *item = &mr.results.items[mr.sel];
    if (mr.routed && mr.routed->prefix[0]) {
        wchar_t joined[MRUN_QUERY_CAP];
        _snwprintf(joined, MRUN_QUERY_CAP, L"%ls%ls", mr.routed->prefix,
                   item->title);
        joined[MRUN_QUERY_CAP - 1] = L'\0';
        mrun_ui_set_query(joined);
    } else {
        mrun_ui_set_query(item->title);
    }
}

static void move_selection(int delta, bool wrap) {
    if (mr.results.count == 0) return;

    mr.sel += delta;
    if (wrap) {
        if (mr.sel < 0)                 mr.sel = mr.results.count - 1;
        if (mr.sel >= mr.results.count) mr.sel = 0;
    }

    clamp_scroll();
    InvalidateRect(mr.window, NULL, TRUE);
}

static bool handle_key(WPARAM vk) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (ctrl) {
        switch (vk) {
        case 'N': case 'J': move_selection(1, true);  return true;
        case 'P': case 'K': move_selection(-1, true); return true;
        case 'U':
            mr.query[0]  = L'\0';
            mr.query_len = 0;
            mrun_ui_refresh();
            return true;
        case 'W':
            query_backspace_word();
            mrun_ui_refresh();
            return true;
        case 'R':
            mrun_modules_reload_data();
            mrun_icons_flush();
            mrun_ui_refresh();
            return true;
        }
        return false;
    }

    switch (vk) {
    case VK_ESCAPE: mrun_ui_hide();        return true;
    case VK_RETURN: activate_selection();  return true;
    case VK_TAB:    complete_selection();  return true;
    case VK_UP:     move_selection(-1, true);  return true;
    case VK_DOWN:   move_selection(1, true);   return true;
    case VK_PRIOR:  move_selection(-mr.look.rows, false); return true;
    case VK_NEXT:   move_selection(mr.look.rows, false);  return true;
    case VK_HOME:   mr.sel = 0; clamp_scroll();
                    InvalidateRect(mr.window, NULL, TRUE); return true;
    case VK_END:    mr.sel = mr.results.count - 1; clamp_scroll();
                    InvalidateRect(mr.window, NULL, TRUE); return true;
    }
    return false;
}

static void handle_char(wchar_t ch) {
    if (ch == VK_BACK) {
        if (mr.query_len > 0) mr.query[--mr.query_len] = L'\0';
        mrun_ui_refresh();
        return;
    }
    if (ch < L' ') return;
    if (mr.query_len >= MRUN_QUERY_CAP - 1) return;

    mr.query[mr.query_len++] = ch;
    mr.query[mr.query_len]   = L'\0';
    mrun_ui_refresh();
}

static void handle_click(int y, bool double_click) {
    UINT dpi   = mrun_window_dpi(mr.window);
    int  pad   = mrun_scale(mr.look.padding,      dpi);
    int  input = mrun_scale(mr.look.input_height, dpi);
    int  row_h = mrun_scale(mr.look.row_height,   dpi);

    if (y < input + pad / 2 || row_h <= 0) return;

    int index = mr.scroll + (y - input - pad / 2) / row_h;
    if (index < 0 || index >= mr.results.count) return;

    mr.sel = index;
    clamp_scroll();
    InvalidateRect(mr.window, NULL, TRUE);

    if (double_click) activate_selection();
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MRUN_SHOW:   mrun_ui_show();   return 0;
    case WM_MRUN_HIDE:   mrun_ui_hide();   return 0;
    case WM_MRUN_TOGGLE: mrun_ui_toggle(); return 0;

    case WM_MRUN_RELOAD:
        mrun_config_load();
        SetLayeredWindowAttributes(hwnd, 0, (BYTE)mr.look.opacity, LWA_ALPHA);
        mrun_ui_refresh();
        return 0;

    case WM_MRUN_QUIT:
        mr.quitting = true;
        DestroyWindow(hwnd);
        return 0;

    case WM_MRUN_ICON:
        if (mrun_icons_deliver(wp, lp) && mr.visible)
            InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_COPYDATA: {
        const COPYDATASTRUCT *cd = (const COPYDATASTRUCT *)lp;
        if (cd && cd->lpData && cd->cbData >= sizeof(wchar_t)) {
            wchar_t text[MRUN_QUERY_CAP];
            mrun_copy_w(text, MRUN_QUERY_CAP, (const wchar_t *)cd->lpData);
            mrun_apply_copydata(cd->dwData, text);
        }
        return TRUE;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint(hwnd);
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (handle_key(wp)) return 0;
        break;

    case WM_CHAR:
        handle_char((wchar_t)wp);
        return 0;

    case WM_LBUTTONDOWN:
        handle_click(GET_Y_LPARAM(lp), false);
        return 0;

    case WM_LBUTTONDBLCLK:
        handle_click(GET_Y_LPARAM(lp), true);
        return 0;

    case WM_MOUSEWHEEL:
        move_selection(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1, false);
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && mr.hide_on_blur && mr.visible)
            mrun_ui_hide();
        return 0;

    case WM_DPICHANGED:
        relayout();
        return 0;

    case WM_DESTROY:
        mr.window = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool mrun_ui_init(void) {
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = mr.hinst;
    wc.lpszClassName = MRUN_CLASS;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);

    if (!RegisterClassExW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log_err(L"mrun: RegisterClassEx failed: %lu", GetLastError());
        return false;
    }

    mr.window = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        MRUN_CLASS, L"mrun", WS_POPUP,
        0, 0, 0, 0, NULL, NULL, mr.hinst, NULL);

    if (!mr.window) {
        log_err(L"mrun: CreateWindowEx failed: %lu", GetLastError());
        return false;
    }

    SetLayeredWindowAttributes(mr.window, 0, (BYTE)mr.look.opacity, LWA_ALPHA);
    return true;
}

void mrun_ui_shutdown(void) {
    mrun_icons_shutdown();

    if (mr.window) {
        DestroyWindow(mr.window);
        mr.window = NULL;
    }
    UnregisterClassW(MRUN_CLASS, mr.hinst);

    font_free(&s_title);
    font_free(&s_sub);
    font_free(&s_input);
}
