# Check for Updates

Added 6 November 2026 (version 0.9.0). Adapted from the Perfect Dark and GoldenEye 007 XBLA PC ports'
updaters (pd_update.h, packaging/launcher/update.cpp); OutRun's launcher is the C# one, so the update
window is C# here.

## What the player sees

- **Help & Options > Check for Updates**, the sixth card after How to play (icon: How to play's). Its
  description gives this copy's version. Nothing is checked at start or in the background.
- Choosing it opens a box in the restart prompt's style (src/update_prompt.cpp): "Checking for
  updates..." while a worker thread asks `releases/latest`, then one of:
  - "OutRun is up to date." (OK);
  - "Could not check for updates." with the reason (OK);
  - "Version X is available. You have Y." with Update and Cancel.
- Update starts `OutRun Online Arcade.exe --update` (the launcher, one folder up from
  resources\outrun.exe) and closes the game. If Help & Options was reached from the pause menu, the box
  asks the player to quit to the main menu first. (The pause menu's Help & Options is the game's own
  four-row list, so the card cannot be reached mid-race today; the check is a safety net.)
- Pad buttons with a controller, key caps (Esc, Enter) without one; mouse hover and click work. The menus
  get no input while the box shows.

## The update window (setup/portable/Updater.cs)

1. Waits up to 30 s for this folder's outrun.exe to exit.
2. Asks GitHub again for the latest release, compares versions (installed version =
   resources\outrun.exe's ProductVersion, from CMake's OUTRUN_VERSION).
3. Downloads `<ZipPrefix><version>.zip` into resources\.update.
4. Refuses it unless its SHA-256 equals the asset's `digest` from GitHub's API (or a `<zip>.sha256`
   asset).
5. Unpacks it, refusing absolute names, drive letters, `..` and unsafe Windows names.
6. Checks it is an OutRun release of that version: `OutRun Online Arcade\OutRun Online Arcade.exe` and
   `resources\outrun.exe` with the release's version.
7. Replaces the program files only: the files at the release's top, licenses\ and resources\ less
   player data. Never written or deleted: resources\assets, userdata, cache, logs, outrun.toml,
   pc-settings.toml, multiplayer-settings.toml, import-manifest.json, outrun.toml.*.bak, the import
   journal/lock. Such files in a release ZIP are left out and logged.
8. Swap: every new file is written as `<name>.update-new`; resources\.update-journal.txt lists the
   targets; each target becomes `<name>.update-old` and the new file takes its name. Any failure puts
   every replaced file back. A cut-off update is undone at the next launcher start from the journal
   (or finished, if the journal says every file was swapped). The running launcher's own old exe is
   deleted at the next start.
9. Starts the game again after 1.8 s. On a failure it shows the reason; Play starts the old version.

Log: resources\logs\update.log.

## Configuration

- Repository and ZIP name: CMake `OUTRUN_UPDATE_REPO` = `CaiusCodes/Outrun-Online-Arcade-XBLA-PC-Recomp` (the
  user's repo) and `OUTRUN_UPDATE_ZIP_PREFIX` = `Outrun-Online-Arcade-XBLA-PC-Recomp-v` (the repo name, as the
  GoldenEye and Perfect Dark releases do), and the same two constants in setup/portable/Updater.cs
  (UpdateSource). Keep them equal; an existing build folder needs `-D` for both (cached).
- The repository was renamed on 2026-11-12 from Outrun-Arcade-Online-XBLA-PC-Recomp (GitHub redirects
  the old API URL). Copies of 0.9.0 downloaded before then look for the asset
  `Outrun-Arcade-Online-XBLA-PC-Recomp-v<version>.zip`, so attach each later release ZIP under both
  names (same file, same SHA-256) until those copies have updated.
- `OUTRUN_UPDATE_API` (environment): another releases/latest URL, https or http on 127.0.0.1/localhost.
  The game passes it on to the launcher.
- Test-only: `OUTRUN_UPDATE_TEST_FAIL_AFTER=n` makes the swap fail after n files (rollback test);
  `OUTRUN_UPDATE_BACKGROUND=1` opens the update window top right without taking the focus.

## Packaging

tools\Build-Portable.ps1 must compile portable/Updater.cs with the launcher, and the release ZIP must
be named `<ZipPrefix><version>.zip` with the top folder `OutRun Online Arcade\`. The README section is in
packaging\PORTABLE-README.txt ("Updating").

## Tested (6 Nov, scratch install, fake releases on 127.0.0.1)

In-game background runs: up to date; update available -> Update -> game closed, update window
downloaded/verified/installed 0.9.1 and restarted the game (title showed v0.9.1); Back from the box
returns to the cards. Launcher runs: wrong checksum, unsafe ZIP name, a forced failure after 6 files and
a locked rexruntime.dll all left every program file unchanged; a cut-off update (journal, one swapped
file, one staged file, one new file) was undone at the next start; a committed journal's leftover
launcher exe was deleted. Player data (saves, settings, import manifest; assets and cache sizes/times)
was identical before and after every run. tools/Test-PcMenuPatch.py checks the sixth card and its block.
