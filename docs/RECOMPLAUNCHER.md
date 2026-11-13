# RecompLauncher support

[RecompLauncher](https://recomplauncher.com) installs recomps from their GitHub releases. The release
ZIP carries `recomplauncher.json` (source: `packaging/recomplauncher.json`, copied next to
`OutRun Online Arcade.exe` by `tools/Make-Release.ps1`), which lets the launcher set the game up and
start it without the setup window. Guide and schema: https://recomplauncher.com/sdk.

## What the file does

| Part | What happens |
| --- | --- |
| `game` | The player picks their XBLA package file (`58410968\000D0000\...`), a folder that holds it, or an extracted folder with `default.xex`. No `check` or `dumps`: the launcher's checks are for ISO 9660/XDVDFS discs, and there is no Redump/No-Intro entry for the XBLA package. `--prepare` does the real check (the package's `default.xex` SHA-256, revision 0.0.1.5). |
| `prepare` | `OutRun Online Arcade.exe --prepare --game {game}` imports the game files into `resources\assets` exactly as the setup window does, without a window, printing `progress <0..1> <text>` lines. Exit 0 = ready. If the files are already imported it only checks a picked package and exits 0. |
| `start` | `OutRun Online Arcade.exe --play`: starts `resources\outrun.exe`, waits until the game closes (following Restart Now), returns the game's exit code. 75 = game files not imported, 76 = `resources\outrun.exe` missing. |
| `config` / `settings` | Edits `resources\pc-settings.toml`, the same file Help & Options > Graphics saves. Display mode, window size, render scale, aspect ratio, anti-aliasing, texture filtering, image filter, HUD, Show FPS, and (advanced) Vivid Colors, Contrast, Black Level. `new` writes the first-install defaults from `packaging/outrun.toml` when the file doesn't exist yet. Graphics API is left out: the first-install value is `auto`, which pc-settings.toml cannot hold. |
| `saves`, `folders`, `actions` | `resources\userdata` (default save folder), the three settings files, `resources\logs`, and a button that clears `resources\cache` (shader cache). |
| `looks` | Off: the game draws from `resources\outrun.exe`, not the root launcher, and has its own picture options. |

`pc-settings.toml` is strict (`src/pc_settings_store.cpp`): an unknown key or value makes the game
ignore the whole file (logged as "PC settings were not loaded"). Every key and value in
`recomplauncher.json` is one that file accepts; keep them in step when settings change.

Without `--prepare` or `--play` the launcher behaves exactly as before (setup window, Shift/--manage,
--update, a dropped package).

## Checking it

- Schema: validate `packaging/recomplauncher.json` against https://recomplauncher.com/schema/recomplauncher.json.
- In RecompLauncher: Settings > Developers > Developer tools, "Add a build", pick an extracted release
  folder. Its Developer tab lists problems.
