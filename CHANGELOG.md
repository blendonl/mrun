# Changelog

All notable changes to mrun are documented here. This project adheres to
[Semantic Versioning](https://semver.org/) (pre-1.0: minor = features + fixes).
Sections after 0.1.0 are generated from commit messages; see *Releases* in the
README.

## 0.5.1

### Other

- **docs:** Stop suggesting mrun share a folder with mshell (c2c3288)

  The Updating section and a manual test described leaving generic files alone
  as protecting a folder mrun shares with mshell.exe. mrun belongs in a folder of
  its own, such as the install script's, so the docs now say that instead, and
  the manual test no longer names mshell.

## 0.5.0

### Added

- **ui:** Show each result's icon (3945b3d)

  Rows were text only, so entries with similar names could only be told
  apart by reading them.

  - Each row draws the shell's icon for what it launches: Start menu
    shortcuts, packaged apps, folders, URLs, and bare names found on PATH
    or under App Paths. An image file is drawn as itself.
  - Icons are loaded on a background thread and cached per path, so
    opening the launcher and typing never wait on the shell; an icon
    appears as soon as it is ready.
  - Icons are extracted at the row's real pixel size, so they stay sharp
    at any scaling, and are loaded again when the DPI or icon_size
    changes. Ctrl+R reloads them along with the index.
  - Lua rows and the apps module's extra entries take an optional `icon`
    path, defaulting to `exec`; `""` draws none.
  - New appearance options `show_icons` (on by default) and `icon_size`
    (24).

## 0.4.0

### Added

- **install:** Leave an install that is already up to date alone (ebd5cd3)

  Running install.ps1 against a folder whose mrun.exe already is the release
  being installed now says so and stops: nothing is downloaded, and a copy
  running from that folder is not restarted.

  - -SkipPath installs or upgrades without adding the folder to the user PATH.

- Update mrun from the launcher (25cc84f)

  Type "update mrun" and press Enter. After a Yes/No box naming the installed
  version and the folder, mrun runs install.ps1 over the folder it is running
  from: the latest release is downloaded, mrun is stopped while its files are
  replaced and started again, and PATH is left alone. `mrun --update` does the
  same without asking.

  - The row comes from a new built-in module, `settings`, the home for mrun's
    own actions. It stays enabled when `set_modules` leaves it out, so a config
    written before it existed can still update mrun, and
    `mrun.configure("settings", { enabled = false })` turns it off. With a
    prefix configured, typing the prefix alone lists every action in it.
  - Progress shows in a PowerShell window that closes by itself on success and
    stays open with the error on failure.
  - The PowerShell command is built by mrun_update.c, which has no Windows in
    it and is unit-tested, down to how apostrophes in a folder name are quoted.

- **install:** Leave a shared folder's own files alone when updating (2f3ac1a)

  mrun.exe is often dropped beside mshell.exe, and Update mrun installs over
  whatever folder the running copy is in. It now replaces mrun.exe and
  config\mrun.lua there without copying in README.md, CHANGELOG.md,
  MANUAL-TESTS.md or LICENSE, so another program's files of the same name are
  not overwritten and the folder is not filled with mrun's documents.

  - -SkipDocs gives install.ps1 the same behaviour. Without it, the one-line
    installer still copies the whole release, as before.

## 0.3.0

### Fixed

- **apps:** Find packaged apps that have no Start menu shortcut (d7a6288)

  The apps module only indexed .lnk and .url files under the two Start menus.
  Microsoft Store and MSIX apps such as Claude, Settings and Calculator have no
  file there, so they were never found.

  - Enumerate the Apps folder and add every entry whose ID parses as a package
    application ID, launched through shell:AppsFolder\<AUMID>. Start menu
    shortcuts and extra entries still win on a name clash.
  - New `packaged` option on the apps module, on by default, to leave them out.
  - COM is initialized once on the main thread, which the Apps folder and the
    shell:AppsFolder launch both need.

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
