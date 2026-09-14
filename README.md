# mrun

A **modular keyboard launcher for Windows**, configured in Lua. Type a few
letters, get what you meant. Everything it can find comes from a *module*, and
a module is thirty lines of Lua if you want it to be.

```
┌──────────────────────────────────────────────────────────┐
│ › vsc                                                    │
├──────────────────────────────────────────────────────────┤
│ ▌ Visual Studio Code                               apps  │
│   VSCodium                                         apps  │
└──────────────────────────────────────────────────────────┘
```

It runs under plain Explorer or any window manager and needs neither. It is a
natural fit for [mshell](https://github.com/blendonl/mshell) — a tiling WM that
replaces `explorer.exe`, where there is no Start menu and no Run box.

## Why it is its own program

A launcher that lives inside a window manager grows the window manager every
time it learns something new. Keeping it separate means:

- **Its config is its own.** Its own Lua state, its own file. A syntax error in
  your launcher config cannot take your window manager down with it.
- **Its input is boring.** It is an ordinary process with an ordinary focused
  window, so it reads the keyboard the way every other app does. A launcher
  drawn *by* a WM typically cannot take focus, and has to intercept the
  keyboard globally to be typed into at all.
- **It is useful on its own.** No window manager required.

## Build

Cross-compiled from Linux with **mingw-w64**. Lua 5.4 is vendored, so there is
nothing to fetch:

```sh
sudo apt install gcc-mingw-w64-x86-64
make            # -> mrun.exe
make test       # host-side unit tests (the fuzzy matcher)
make dist       # -> dist/mrun-<version>-win64.zip
```

## Releases

Every pull request merged into `main` is a release. While the PR is open, CI
works out the next version from its
[Conventional Commits](https://www.conventionalcommits.org/) and pushes a
`chore(release): vX.Y.Z` commit to the branch that sets `VERSION` in the
Makefile and writes the matching section of `CHANGELOG.md`. When the PR merges,
CI tags that version and publishes the zip with that section as its notes.

Before 1.0, `feat`, `fix` and breaking changes (`feat!:`, or a
`BREAKING CHANGE:` footer) bump the minor version. From 1.0, breaking changes
bump the major, `feat` the minor and `fix` the patch. Anything else bumps the
patch.

Pull after the bot commits before pushing to the branch again, or run
`make bump` and commit the result yourself so there is nothing for it to add.
Commits pushed straight to `main` are not released on their own; they go out
with the next merged PR.

## Install

In PowerShell:

```powershell
irm https://raw.githubusercontent.com/blendonl/mrun/main/install.ps1 | iex
```

That downloads the latest release into `%LOCALAPPDATA%\Programs\mrun` and adds
the folder to your user `PATH`. Run it again to upgrade; a copy running from
that folder is stopped for the upgrade and started again. To pin a version, or
install somewhere else:

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/blendonl/mrun/main/install.ps1))) -Version 0.1.0 -InstallDir C:\Tools\mrun
```

Nothing about it needs the script, though: `mrun.exe` is a single
self-contained binary. Download `mrun-<version>-win64.zip` from
[Releases](https://github.com/blendonl/mrun/releases), or build it, and put it
anywhere on your `PATH` (or anywhere at all).

Copy `config/mrun.lua` to `%APPDATA%\mrun\init.lua` when you want to configure
it. Until then it runs on built-in defaults. Keep your changes there rather than
in the copy beside the exe, which an upgrade overwrites.

## Using it

```
mrun                     toggle the launcher (starts it if it is not running)
mrun --show|--hide       show or hide it
mrun --toggle            toggle it
mrun --daemon            start resident and hidden, so the first open is instant
mrun --mode <module>     open with that module's prefix already typed
mrun --query <text>      open with <text> already typed
mrun --reload            reload the config of the running instance
mrun --check             validate the config and exit
mrun --config <path>     use this config file
mrun --log-level <lvl>   error|warn|info|debug|trace
mrun --quit              stop the resident instance
mrun --version           print the version and exit
```

The first invocation becomes the **resident instance**; every later one is a
client that just tells it what to do and exits. So binding `mrun.exe` to a hotkey
gives you a toggle, not a pile of processes. `--daemon` at login makes the first
open instant, since indexing has already happened.

| Key | |
|-----|--|
| type | filter |
| `Up` / `Down` | move, wrapping |
| `Ctrl+N` / `Ctrl+P` | same |
| `Ctrl+J` / `Ctrl+K` | same |
| `PgUp` / `PgDn` | move a page |
| `Home` / `End` | first / last |
| `Tab` | complete the query to the selected row |
| `Enter` | activate — **with no results, runs what you typed** |
| `Ctrl+W` / `Ctrl+U` | delete the last word / clear |
| `Ctrl+R` | re-index |
| `Esc` | close |

That `Enter` behaviour is what makes it a Run box as well as a menu: `cmd`, a
path and a URL all work, because they are handed to `ShellExecuteW`.

## Modules

Every result comes from a module. `apps` is the only built-in one today — it
indexes both Start menus, plus the packaged (Microsoft Store / MSIX) apps that
have no shortcut file there, such as Settings, Calculator or Claude; set
`packaged = false` to leave those out. Clipboard history and emoji are the
obvious next ones and will register under their own names.

`set_modules` lists the enabled ones and the order they are searched in. Order
of declaration in the file does not matter; names are resolved after the whole
config has run.

```lua
mrun.set_modules({ "apps", "calc" })

mrun.configure("apps", {
    extensions  = { ".lnk", ".url" },   -- add ".exe" to index raw binaries
    depth       = 4,
    show_path   = false,
    packaged    = true,
    max_results = 50,
    extra = {
        { name = "Terminal", exec = "cmd.exe" },
    },
})
```

### Writing one

A module written in Lua uses the same interface the built-in ones do. `search`
runs on every keystroke and returns rows; `activate` runs on `Enter`. A `prefix`
routes to that module *alone* — type `=` and only calc answers.

```lua
mrun.module({
    name        = "calc",
    prefix      = "=",
    description = "Evaluate a Lua expression",

    search = function(query)
        if query == "" then return {} end
        local chunk = load("return " .. query, "=calc", "t", { math = math })
        if not chunk then return {} end
        local ok, value = pcall(chunk)
        if not ok or value == nil then return {} end
        return {
            { title = tostring(value), subtitle = "Enter to copy", score = 1000 },
        }
    end,

    activate = function(item)
        mrun.set_clipboard(item.title)
    end,
})
```

A row is `{ title, subtitle, exec, args, cwd, score }` — only `title` is
required. Supply `score` to rank it yourself; leave it out and mrun ranks by
`title`. `activate` returning `false` keeps the launcher open; anything else
closes it. A module whose `search` raises is logged and skipped — the others
still answer.

### Matching

Fuzzy and subsequence-based, with **prefix**, **word-boundary** and
**camelCase** hits ranked above scattered ones, consecutive runs rewarded and
long gaps penalised. So `vsc` finds *Visual Studio Code*, `fire` puts *Firefox*
above *Notepad Firewall Helper*, and an exact name always wins. This is the one
piece with no Windows in it, so it is unit-tested: `make test`.

## The `mrun.*` API

| | |
|-|-|
| `mrun.set_appearance{}` | size, colours, fonts, opacity, rounding, position |
| `mrun.set_behaviour{}` | `hide_on_blur`, `clear_on_hide`, `log_level` |
| `mrun.set_modules{}` | which modules are enabled, and their search order |
| `mrun.configure(name, {})` | settings for one module |
| `mrun.module{}` | register a module written in Lua |
| `mrun.spawn(cmd, args, cwd)` | launch something |
| `mrun.set_clipboard(text)` / `mrun.get_clipboard()` | the clipboard |
| `mrun.set_query(text)` | type into the input programmatically |
| `mrun.hide()` | close the launcher |
| `mrun.log([level,] msg)` | write to the log |
| `mrun.version()` | the version string |

Colours are `0xRRGGBB` integers. `config/mrun.lua` is the annotated reference
and documents every field.

## Files

| Where | What |
|-------|------|
| `%APPDATA%\mrun\init.lua` | your config |
| `config\mrun.lua` beside `mrun.exe` | the portable fallback, and the shipped reference |
| `%LOCALAPPDATA%\mrun\mrun.log` | its log |

A config error is **atomic**: the whole file is rejected and mrun runs on
built-in defaults rather than half of what you wrote, with the reason in the
log. `mrun --check` gives you the same verdict on the command line before you
trust it.

## With mshell

mrun and [mshell](https://github.com/blendonl/mshell) know nothing about each
other. Bind `mrun.exe` like any other program:

```lua
mshell.keys.bind({"LWin"}, "Space",
    function() mshell.exec("mrun.exe") end, { desc = "run" })
```

Its window is a tool window, so mshell floats it over the tiles rather than
tiling it.

## Architecture

| File | Responsibility |
|------|----------------|
| `mrun.c` | entry point, single-instance/client split, CLI, the small shared helpers |
| `mrun_ui.c` | the window: layout, painting, keys, mouse, focus |
| `mrun_config.c` | config path resolution, Lua state lifecycle, atomic load |
| `mrun_lua.c` | the `mrun.*` API, and the bridge for Lua-backed modules |
| `mrun_module.c` | the module registry, prefix routing, ranking |
| `mrun_score.c` | the fuzzy matcher — no Windows in it, so it is unit-tested |
| `mod_apps.c` | the app-launching module |
| `log.c` | leveled rotating log |

## License

MIT — see [LICENSE](LICENSE). Vendored Lua 5.4 is under its own MIT license
(`vendor/lua/doc/readme.html`).
