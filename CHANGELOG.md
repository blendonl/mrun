# Changelog

All notable changes to mrun are documented here. This project adheres to
[Semantic Versioning](https://semver.org/) (pre-1.0: minor = features + fixes).
Sections after 0.1.0 are generated from commit messages; see *Releases* in the
README.

## Unreleased

### Changed

- **mrun no longer carries mshell's name.** The log moves from
  `%LOCALAPPDATA%\mshell\mrun.log` to `%LOCALAPPDATA%\mrun\mrun.log`, and the
  exe's version resource and manifest identify it as mrun rather than as part
  of mshell.
- The shipped `config/mrun.lua` drops its "Reload mshell" entry, which failed on
  any machine without mshell.
- mshell no longer looks for `mrun.exe` on its own. Under mshell, bind it with
  `mshell.exec("mrun.exe")`.

## 0.1.0

First release. Extracted from [mshell](https://github.com/blendonl/mshell), whose
built-in launcher this replaces.

### Added

- **A launcher that is its own program.** mshell's built-in one indexed the
  Start menu and did nothing else: no config, no way to add a source, no way to
  add a mode. Extending it in place would have meant growing the window manager
  every time the launcher learned something new.

  Being a separate process also fixes the hardest part. An overlay painted by a
  window manager is typically `WS_EX_NOACTIVATE` and cannot take the foreground,
  so it can only be typed into by intercepting the keyboard globally and
  forwarding keystrokes to itself — a capture mode whose failure case is a dead
  keyboard. mrun is an ordinary process with an ordinary focused window and
  reads keys the way every other application does.

- **A module system.** Every result comes from a module, behind one interface
  (`init` / `configure` / `search` / `activate`). `apps` is the first, indexing
  both Start menus; clipboard history and emoji are the obvious next ones and
  register the same way.

  A module is also writable **entirely in Lua**, against the same interface:

  ```lua
  mrun.set_modules({ "apps", "calc" })     -- enabled, in search order

  mrun.module({
      name     = "calc",
      prefix   = "=",                       -- typing "=" searches only this
      search   = function(query) ... end,   -- runs on every keystroke
      activate = function(item) ... end,    -- runs on Enter
  })
  ```

  Module names are resolved after the whole config has run, so the order things
  are declared in the file does not matter.

- **Fuzzy matching** — subsequence, with prefix, word-boundary and camelCase
  hits ranked above scattered ones, consecutive runs rewarded and long gaps
  penalised. `vsc` finds *Visual Studio Code*. A row may supply its own `score`
  to override the ranking. Unit-tested (`make test`), being the one part with no
  Windows in it.

- **A resident instance with a client CLI.** The first invocation goes resident;
  every later one is a client that tells it what to do and exits (`--toggle`,
  `--show`, `--hide`, `--query`, `--mode`, `--reload`, `--quit`). Binding
  `mrun.exe` to a hotkey therefore gives a toggle rather than a second copy, and
  `--daemon` at login makes the first open instant.

- **Lua configuration**, at `%APPDATA%\mrun\init.lua` or `config\mrun.lua`
  beside the exe. `mrun.set_appearance{}` covers size, colours, fonts, opacity,
  rounding and position; `mrun.set_behaviour{}` covers hide-on-blur and whether
  the query survives a close. `--check` validates a config from the command
  line.

  A config error is **atomic**: the whole file is rejected and mrun runs on
  built-in defaults rather than half of what you wrote.

- **Per-monitor DPI awareness**, declared in the manifest so it applies before
  the first window exists and costs no load-time dependency on a `user32` export
  that older Windows lacks.

### Notes

- The portable config file is `config\mrun.lua` rather than `config\init.lua`
  deliberately: mrun.exe is often dropped beside `mshell.exe`, whose own config
  is `config\init.lua`, and two programs reading the same filename out of the
  same folder is a trap.
- The log goes to `%LOCALAPPDATA%\mshell\mrun.log`, which `log.c` inherited from
  mshell along with the file itself.
