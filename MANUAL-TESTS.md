# Manual test checklist

`make test` covers the logic with no Windows in it — the fuzzy matcher, which
decides what a query matches and in what order, and the PowerShell command an
update runs, down to how a folder name is quoted. Everything below needs a real
Windows machine, because it involves a window, the keyboard or the shell.

None of this needs a window manager. Run it from `cmd.exe` alongside Explorer;
the last section is the only part that wants one.

`%LOCALAPPDATA%\mrun\mrun.log` is written on every run and is the first place
to look.

## Resident instance and the CLI

| # | Test | Expected |
|---|------|----------|
| 1 | `mrun.exe` with nothing running. | The launcher appears **and takes focus** — you can type into it straight away, without clicking. It stays resident after `Esc`. |
| 2 | `mrun.exe` again. | It toggles: opens if hidden, hides if shown. **Never** a second window, and Task Manager shows one `mrun.exe`. |
| 3 | `mrun.exe --daemon` on a clean machine. | Nothing appears. A later `mrun.exe` opens instantly (indexing already happened). |
| 4 | `mrun.exe --quit`, then `mrun.exe`. | It starts fresh. |
| 5 | `mrun.exe --check`. | Prints `ok:` and the config path, or `FAILED:` and the Lua error, to the console you typed it into. Exit code 0 / 1 to match. |
| 6 | `mrun.exe --version`. | Prints the version, and nothing else. |
| 7 | `mrun.exe --query fire`. | Opens with `fire` already typed and Firefox selected. |
| 8 | `mrun.exe --mode calc` (with the shipped example config). | Opens with `=` already typed; only the calc module answers. |
| 9 | `mrun.exe --nonsense`. | Prints the usage text rather than doing something surprising. |
| 10 | `mrun.exe --quit` when nothing is running. | Exits quietly. No window flashes up. |

## Searching and launching

- Type `fire` → **Firefox** is selected. Type `vsc` → **Visual Studio Code** is
  found (subsequence matching). Type `xyzzy` → "No results", and the window
  shrinks to fit.
- `Up`/`Down` wrap at both ends. `Ctrl+N`/`Ctrl+P` and `Ctrl+J`/`Ctrl+K` do the
  same thing.
- `PgUp`/`PgDn` move a page and **clamp** rather than wrap.
- `Home`/`End` go to the first and last result.
- With more results than `rows`, the list scrolls to keep the selection visible
  and the scrollbar tracks it.
- `Tab` completes the query to the selected row. Inside a prefix-routed module,
  the prefix is preserved.
- `Ctrl+W` deletes the last word; `Ctrl+U` clears the query.
- `Enter` on a result launches it; the launcher hides.
- `Enter` with **no** results runs what you typed: `cmd`, a full path, and a URL
  each work.
- Launching something that does not exist: the launcher **stays open** and the
  log says why.
- Click a row to select it; double-click to launch it. The scroll wheel moves
  the selection.
- Click another window while it is open → it hides (`hide_on_blur`).
- Install a program, press `Ctrl+R`, and it is findable without restarting mrun.
- Packaged apps with no Start menu shortcut are found and launch: `settings` →
  **Settings**, `calc` → **Calculator**, and any Store/MSIX app such as
  **Claude**. With `packaged = false` and `--reload`, they are gone.
- Type fast (hold a key on autorepeat) with several hundred results matching:
  no visible lag, no flicker.
- Leave it open for a few minutes typing and deleting: memory in Task Manager
  is flat. (Guards against the Lua registry leaking a ref per result per
  keystroke.)

## Config

- No config at all (`%APPDATA%\mrun\init.lua` deleted, none beside the exe): it
  still opens, on defaults, and the log says no config was found.
- Break the config (a stray `end`), then `mrun.exe --reload`: the launcher still
  opens, on **built-in defaults**, and the log names the error. Nothing is
  half-applied.
- Fix it and `--reload` again: your settings are back, with no restart.
- Change `bg` / `width` / `rows` and `--reload`: visible on the next open.
- Change `opacity` and `--reload`: the window's transparency actually changes
  (it is re-applied, not just stored).
- `position = "center"` vs `"top"` with `top_offset`: both land where they say.
- Declare `mrun.set_modules{}` **before** `mrun.module{}` in the file: the
  module is still enabled. (Names are resolved after the whole config runs, so
  declaration order must not matter.)
- Name a module in `set_modules` that does not exist: it is skipped with a log
  line, and the others still work.
- Set `prefix = "="` on a module: typing `=` shows only its results, and its
  name appears in the input row.
- A Lua module whose `search` raises: the error is logged and the launcher keeps
  working — the other modules still answer.
- A Lua module returning a row with no `title`: skipped, not crashed.
- `mrun.configure("apps", { paths = { "%APPDATA%\\..." } })`: environment
  variables in paths are expanded.

## Updating

Build a copy that is older than the latest release, so there is something to
update, and run it from a folder of its own:

```sh
make clean && make VERSION=0.0.1
```

An update runs `install.ps1` from `main`. To try a branch's `install.ps1` before
it is merged, point the build at it:

```sh
make clean && make VERSION=0.0.1 \
  "CFLAGS_EXTRA=-DMRUN_UPDATE_SCRIPT='L\"https://raw.githubusercontent.com/blendonl/mrun/<branch>/install.ps1\"'"
```

- Type `update mrun`: **Update mrun** is the top row, badged `settings`, and
  its subtitle names the version you have. With an empty query it is **not** in
  the list.
- With `set_modules({ "apps", "calc" })`, which does not name `settings`, the
  row is still there. Add `mrun.configure("settings", { enabled = false })`
  and `--reload`: it is gone.
- `Enter` on it: the launcher hides and a Yes/No box names the installed version
  and the folder. **No** does nothing: no PowerShell window, mrun still running.
- **Yes**: a PowerShell window downloads the release, mrun quits, the files are
  replaced, and mrun comes back resident (`mrun` opens it at once). The window
  closes by itself a few seconds later. Open it and search for `update mrun`
  again: the subtitle shows the new version.
- Run it again on the latest release: the window says it is already installed,
  downloads nothing, and mrun is **not** restarted.
- Your `%APPDATA%\mrun\init.lua` is untouched and the user `PATH` is unchanged
  (`[Environment]::GetEnvironmentVariable('Path', 'User')` before and after),
  including when mrun lives in a folder that is not on `PATH`.
- `mrun.exe --update` from a terminal: the same update starts without asking.
- Unplug the network and update: the PowerShell window stays open with the error
  in red until you press `Enter`, and mrun keeps running.
- Run a copy from a folder with an apostrophe in its name (`C:\O'Brien\mrun`)
  and update it: the files land in that folder.
- Put a `README.md` and a `LICENSE` of your own beside the copy, then update:
  `mrun.exe` and `config\mrun.lua` are replaced, and your two files are
  untouched, with no `CHANGELOG.md` or `MANUAL-TESTS.md` added. The one-line
  installer, run over a folder of its own, still copies all of them.

## Display

- On a **mixed-DPI** multi-monitor setup, open it on each monitor in turn: it is
  sized, positioned and rendered for the monitor the cursor is on. Text is not
  blurry on the high-DPI one.
- `rounded = true`: the corners are actually rounded (a region, so it works on
  Windows 10 as well as 11).
- Long entries: the title ellipsizes rather than overflowing, and the module
  badge on the right is never overlapped.
- Set `show_subtitles = false`, `show_module = false`, `show_scrollbar = false`
  in turn: each disappears and the layout stays sane.

## Icons

- Every app row has its icon: Start menu shortcuts (no shortcut arrow),
  packaged apps such as **Settings** and **Calculator**, and `extra` entries
  given as a bare name (`cmd.exe`) or an App Paths name (`chrome`).
- Icons are sharp at 100%, 150% and 200% scaling. Moving between monitors with
  different scaling redraws them at the new size, not stretched.
- First open after starting mrun: the rows appear at once and the icons fill in
  a moment later. Typing never waits on an icon.
- An `extra` entry whose `exec` does not exist, and a Lua row with no `exec`:
  an empty slot, with the title still lined up under the others.
- `icon = ""` on a row draws no icon; `icon = "%WINDIR%\\explorer.exe"` draws
  Explorer's; `icon = "C:\\...\\picture.jpg"` draws the picture.
- `show_icons = false` and `--reload`: rows are drawn exactly as without icons,
  titles flush with the prompt.
- `icon_size = 16` and `--reload`: every icon is redrawn at the new size.
- Hold `Down` through a long result list, then do it again: GDI objects in Task
  Manager rise the first time and stay flat the second.
- `mrun.exe --quit` straight after opening, while icons are still loading: it
  exits promptly.

## With mshell

- Bound with `mshell.exec("mrun.exe")`, the key opens mrun, and pressing it
  again hides it.
- mshell does **not** tile it, put a focus ring on it, or count it as a window
  (it is a tool window). It floats over the tiles.
- Open mrun, then quit mshell from another machine/Task Manager: mrun is
  unaffected — it is a separate process and owns its own keyboard input.
- With mshell as the shell (no Explorer), `claude` → **Claude** and `notepad`
  → **Notepad** launch. With `--log-level debug` the log says `activated`.
- In the same session, `calc` → **Calculator** does not launch, since Windows
  cannot host UWP apps without Explorer, but the launcher stays open straight
  away rather than freezing for most of a minute.
