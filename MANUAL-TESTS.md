# Manual test checklist

`make test` covers the logic with no Windows in it — the fuzzy matcher, which
decides what a query matches and in what order. Everything below needs a real
Windows machine, because it involves a window, the keyboard or the shell.

None of this needs mshell. Run it from `cmd.exe` alongside Explorer; the last
section is the only part that wants a window manager.

`%LOCALAPPDATA%\mshell\mrun.log` is written on every run and is the first place
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

## With mshell

- mshell does **not** tile it, put a focus ring on it, or count it as a window
  (`mrun_Window` is in mshell's ignore list). It floats over the tiles.
- With `mrun.exe` beside `mshell.exe`, mshell's `launcher` action opens **mrun**.
- With `mrun.exe` only on `PATH`, it still opens mrun.
- With `mrun.exe` neither beside mshell nor on `PATH`, the action falls back to
  mshell's built-in overlay and nothing errors.
- Open mrun, then quit mshell from another machine/Task Manager: mrun is
  unaffected — it is a separate process and owns its own keyboard input.
