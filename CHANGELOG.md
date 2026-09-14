# Changelog

All notable changes to mrun are documented here. This project adheres to
[Semantic Versioning](https://semver.org/) (pre-1.0: minor = features + fixes).
Sections after 0.1.0 are generated from commit messages; see *Releases* in the
README.

## 0.2.0

### Added

- Install and upgrade with one line of PowerShell (9c968fa)

  `irm https://raw.githubusercontent.com/blendonl/mrun/main/install.ps1 | iex`
  downloads the latest release into %LOCALAPPDATA%\Programs\mrun and adds
  that folder to the user PATH, so `mrun` works in any new terminal with no
  manual steps.

  - Running it again upgrades in place. A copy running from the install
    folder is quit first, so its exe is not locked, and started again with
    --daemon afterwards. Copies running from anywhere else are left alone.
  - The PATH entry is only added once. The value stays REG_EXPAND_SZ with its
    %VARIABLE% entries unexpanded, and the change is broadcast so Explorer
    and newly opened terminals see it.
  - -Version pins a release and -InstallDir picks another folder.

### Changed

- Stop carrying mshell's name (a211dfe)

  mrun is a standalone launcher and works with or without mshell.

  - Log to %LOCALAPPDATA%\mrun\mrun.log instead of mshell's directory.
  - The version resource, manifest identity and usage line name mrun, not mshell.
  - The shipped config drops its "Reload mshell" entry, which failed anywhere mshell is not installed.
  - README and MANUAL-TESTS no longer describe mshell's launcher handoff, which mshell has removed; under mshell, mrun is bound with mshell.exec like any other program. The link to mshell stays.

### Other

- **ci:** Publish a release when main carries an untagged version (66c28f8)

  After a green build on main, compare the Makefile's VERSION against the
  tags on origin. If v$VERSION does not exist yet, tag the commit and publish
  a GitHub release with the zip the build job just assembled, using that
  version's CHANGELOG section as the notes. A version with no changelog
  section fails the job instead of releasing with empty notes.

- **build:** Work out the next version and changelog section from commits (5e754b5)

  `make bump` reads the Conventional Commits since the latest vX.Y.Z tag,
  picks the next version and writes it to VERSION in the Makefile, with a
  matching CHANGELOG.md section grouped into Breaking, Added, Fixed, Changed
  and Other. Commit bodies are kept under each entry; trailers are dropped.

  Before 1.0, feat, fix and breaking changes bump the minor version; after,
  breaking bumps the major, feat the minor and fix the patch. Anything else
  bumps the patch.

  Running it again replaces the pending section rather than stacking a new
  one, and chore(release) commits are left out, so it can run on every push
  to a branch.

- **ci:** Commit the version bump to pull requests into main (975cafc)

  When a pull request into main is opened or updated, run `make bump` on its
  branch and push the result as `chore(release): vX.Y.Z`. Merging it then
  leaves main on an untagged version, which the release job tags and
  publishes. Nothing is ever committed to main by CI.

  Pull requests from forks are skipped, since the workflow token cannot push
  to them.

- **docs:** Describe how releases are cut (0e9aee2)

  Point Install at the Releases page, and explain in a new Releases section
  how the version is chosen, what the bot commit on a pull request is, and
  how to avoid it with `make bump`.

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
