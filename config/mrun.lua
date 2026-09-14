--[[
    mrun — the launcher's configuration.

    Location:  %APPDATA%\mrun\init.lua
               ...or config\mrun.lua beside mrun.exe (portable installs)
    Check it:  mrun.exe --check
    Reload:    mrun.exe --reload
    Log:       %LOCALAPPDATA%\mrun\mrun.log

    A config error is atomic: the whole file is rejected and mrun runs with
    built-in defaults rather than half of what you wrote.
--]]

----------------------------------------------------------------------
-- Appearance
----------------------------------------------------------------------
mrun.set_appearance({
    width         = 620,      -- design px at 96 DPI; scaled per monitor
    rows          = 8,        -- result rows on screen at once
    row_height    = 34,
    input_height  = 44,
    padding       = 12,
    font          = "Segoe UI",
    font_size     = 16,
    sub_font_size = 12,
    icon_size     = 24,

    bg        = 0x1e1e2e,     -- 0xRRGGBB
    fg        = 0xcdd6f4,
    dim       = 0x7f849c,     -- subtitles, module badges, placeholder
    sel_bg    = 0x313244,
    sel_fg    = 0xffffff,
    border    = 0x45475a,
    prompt_fg = 0x89b4fa,
    accent    = 0x89b4fa,     -- the marker down the selected row

    border_width   = 1,
    opacity        = 246,     -- 0..255
    rounded        = true,
    corner_radius  = 10,
    position       = "top",   -- "top" or "center"
    top_offset     = 18,      -- percent down the work area, "top" only

    show_subtitles = true,
    show_icons     = true,
    show_module    = true,    -- name of the module that produced each row
    show_scrollbar = true,

    prompt      = "\u{203a}",
    placeholder = "Search\u{2026}",
    empty_text  = "No results",
})

mrun.set_behaviour({
    hide_on_blur  = true,     -- close when another window takes focus
    clear_on_hide = true,     -- next open starts with an empty query
    log_level     = "info",   -- error|warn|info|debug|trace
})

----------------------------------------------------------------------
-- Modules
--
-- Every result comes from a module. `set_modules` lists the ones that are
-- enabled and the order they are searched in; leave it out and every
-- registered module is enabled.
--
-- "apps" is the only built-in module today. Clipboard history and emoji are
-- the obvious next ones, and they will register under their own names — at
-- which point they go in this list too.
----------------------------------------------------------------------
mrun.set_modules({ "apps", "calc" })

-- The app launcher. With no `paths` it indexes both Start menus, which is
-- what you want on a normal install.
mrun.configure("apps", {
    -- paths = {
    --     "%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs",
    --     "%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs",
    --     "D:\\Portable",
    -- },
    extensions  = { ".lnk", ".url" },   -- add ".exe" to index raw binaries
    depth       = 4,                    -- how deep to recurse
    show_path   = false,                -- put the containing folder in the subtitle
    packaged    = true,
    max_results = 50,

    -- Anything the Start menu does not have a shortcut for.
    extra = {
        { name = "Terminal",       exec = "cmd.exe" },
        { name = "Registry Editor", exec = "regedit.exe" },
    },
})

----------------------------------------------------------------------
-- A module written entirely in Lua
--
-- `search` is called on every keystroke and returns a list of items:
--
--     { title = "...",        -- required; what is shown and matched
--       subtitle = "...",     -- optional second line
--       exec = "...",         -- optional; launched when there is no activate
--       args = "...", cwd = "...",
--       icon = "...",         -- optional; defaults to exec, "" for none
--       score = 123 }         -- optional; skip it and mrun ranks by title
--
-- `activate(item, query)` runs on Enter. Return false to keep mrun open;
-- anything else closes it.
--
-- `prefix` routes to this module alone: type "=" and only calc answers.
----------------------------------------------------------------------
mrun.module({
    name        = "calc",
    prefix      = "=",
    description = "Evaluate a Lua expression",

    search = function(query)
        if query == "" then return {} end

        local chunk = load("return " .. query, "=calc", "t", {
            math = math, string = string, tostring = tostring,
        })
        if not chunk then return {} end

        local ok, value = pcall(chunk)
        if not ok or value == nil then return {} end

        return {
            { title    = tostring(value),
              subtitle = "Enter to copy to the clipboard",
              score    = 1000 },
        }
    end,

    activate = function(item)
        mrun.set_clipboard(item.title)
    end,
})

----------------------------------------------------------------------
-- Keys (fixed, for now)
--
--   type              filter
--   Up / Down         move, wrapping
--   Ctrl+N / Ctrl+P   same
--   Ctrl+J / Ctrl+K   same
--   PgUp / PgDn       move a page
--   Home / End        first / last
--   Tab               complete the query to the selected row
--   Enter             activate; with no results, run what you typed
--   Ctrl+W            delete the last word
--   Ctrl+U            clear the query
--   Ctrl+R            re-index (rescan the Start menus)
--   Esc               close
----------------------------------------------------------------------
