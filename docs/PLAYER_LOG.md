# Player log (bug reports)

Added 8 November 2026 (branch `player-logging`, src/player_log.cpp). Goal: a
friend's `resources\logs\outrun_00N.log` should say which build, PC and
settings they used and where in the game they were when something went
wrong, without the old noise. Nothing here changes guest state or gameplay.

**For testers:** the F8 mark and F9 frame trace are off in releases. Add
`outrun_mark_key = true` to `resources\outrun.toml`, then press F8 the moment
you see something wrong (keep driving; one press per problem). Afterwards zip
and send the whole `resources\logs` folder.

## What the log now contains

1. **Startup header**, right after the runtime's own start lines (once the
   PC menu has loaded its settings):
   ```
   ==== OutRun Online Arcade PC v0.9.0 build 69 (hash, date) ====
     OS:        Windows 11 25H2 (build 26200.xxxx)
     Renderer:  Plume (native), Direct3D 12 (gpu = auto)
     GPU:       <adapter> (vendor, device, VRAM), driver <umd> (NVIDIA nnn.nn)
     Display:   windowed/fullscreen, window client WxH, monitor WxH @ Hz
     Settings:  every PC menu row by name (Display Mode=..., Render Scale=..., ...)
     Present:   vsync, frame_limit, fsr_quality, letterbox, tearing, log_level
     Audio:     mute, steering loops, initial volume
     Controls:  mnk_mode, controller names (DirectInput) and XInput slots in use
     Network:   private_match, local_sessions
   ```
   The adapter is the one the runtime picks (DXGI high-performance). The
   driver number is the user-mode driver version; for NVIDIA the familiar
   "566.36"-style number is added.
2. **Settings changes**: Accept in the PC menu logs one line with only the
   rows that changed (`Settings changed: Render Scale 1x -> 2x, ...`). The old
   per-read/preview/stage lines (`PC menu: read N = N`) are debug level.
3. **Game breadcrumbs** (logged on change only, `Game:` prefix):
   - `screen <name> (was <name>)`: title, main menu, help & options, car
     select, transmission select, race options, music select, leaderboards,
     multiplayer screens, pause, race, ranking. The screen is the frontend
     script that reads menu input (frontend.sbn's 27 scripts, by name hash);
     dialogs and the card manager keep the screen under them. A screen must
     hold for 0.25 s (race: 1 s), so the menus' frames without input reads do
     not flap.
   - `race N start: mode <single player | Time Attack | LAN/online>, car <model>`
   - `stage N '<name>' (<code>) route <forks so far>, race m:ss.ss` when the
     car reaches the stage (its checkpoint gate loads). When a race goes on
     past the goal from stage 1 (Continuous Mode), the stage lines and the
     mark say ` lap N` and the route starts again for that lap.
   - `goal reached after stage 5 ...`, then `race N end (ranking|back to
     menus|back to title): reached stage N ..., goal C, race m:ss.ss`.
   - The race clock is the port's own: seconds of Swaps from the start line,
     paused time left out. It is close to, not equal to, the game's HUD time.
4. **F8 = mark a bug**: with the game window in front, F8 writes
   `=== PLAYER MARK #n: stage 3 'Canyon' (GRAN) route RR, race 1:23.45, 0:31.20 into the stage, fps 60, Plume D3D12, scale 1x, AA 4x ===`
   (or `screen <name>` outside a race), flushes the log, saves
   `logs\outrun_mark_NN_<date>_<time>.png` (the window's client area, taken
   with PrintWindow on a worker thread) and shows "Bug mark #n saved to the
   log" for 2.5 s in the top-left corner (ImGui overlay; drawn after the
   capture, so not in the picture). With Plume it also traces the next frame
   draw by draw into `logs\outrun_mark_NN_<date>_<time>_frame.txt` (not into
   the log; the log gets one `Player mark frame trace: <file> (frame, draws,
   lines, KB)` line): per draw the vs/ps shader hashes, z enable/write/func,
   RB_DEPTHCONTROL, cull, blend, stencil, write mask, alpha test, textures
   (guest address, format, fetch constant), viewport, guest constants (vs
   c17 = eye, i.e. the camera position), vertex layout and first vertices;
   plus each render target set and each resolve (target and depth surface).
   A race frame is ~860 draws, ~13,000 lines, ~2.7 MB (capped at 8 MB;
   text, so it zips to a fraction). Tracing makes that one frame slow (one
   hitch). The car's own track position is not read (no known read-only
   address); the time into the stage and the trace's eye constant place it.
   F9 is the renderer's two-frame trace into the log, also off unless
   `gpu_native_f9_trace = true`.
5. **Session summary** when the window closes:
   `Session summary: played m:ss, N races, N stage entries (N different: codes), fps avg x, lowest driving second y, N player marks; last: <where>`.
   The runtime hard-exits right after the window closes, so this is written
   from OnWindowCloseRequested.
6. **Crash line**: an unhandled-exception filter writes
   `CRASH: unhandled exception <code> at <address>; last known: <where>`,
   flushes, and hands on to the previous filter.

## Noise cuts

| Before | Now |
| --- | --- |
| `Keyboard context: ...` every ~0.3 s in menus | logged once the context has held for 1 s |
| ORNATIVE frame / pools / video memory / queue lag / gpu time every 600 frames (10 s) | every 3600 frames (1 min) |
| one `ORNATIVE shader ... translated` line per shader | one `ORNATIVE shaders: N translated (v vs, p ps) over s, ms each` line per batch (a second with none, 10 s at most); per-shader lines are debug |
| `ORNATIVE occlusion result` bursts | debug |
| `Local leaderboard: view N has no scores` every visit | once per view (then debug) |
| `PC menu: read/preview/stage N = N` | debug; Accept logs the named changes |

Debug lines appear with `log_level = "debug"` in outrun.toml.

## outrun.toml options (no menu entries)

| Key | Default | Meaning |
| --- | --- | --- |
| `outrun_log_breadcrumbs` | true | the `Game:` lines |
| `outrun_mark_key` | false | F8 marks (tester tool) |
| `outrun_mark_screenshot` | true | PNG beside the log with each mark |
| `outrun_mark_trace` | true | next frame traced into `_frame.txt` beside the log with each mark (Plume) |
| `outrun_log_files` | false | every file the game opens (troubleshooting; long) |

`outrun_test_script` takes `<seconds>:mark` for background tests (a mark
without focus or a key press).

## How the state is known (read-only)

The game opens its files through NtCreateFile, reached only indirectly
(through the dispatch table, slot 0x824C95E4). Startup swaps that one slot
for a wrapper that calls the original and notes the path; the call is
unchanged. Background runs with `outrun_log_files = true` (routes LRLR and
LLLL, 3x game speed, countdown held) showed:

- `Stage\<CODE>\...` loads at the race start (stage 1) and, for each later
  stage, at the fork about ten seconds before its checkpoint.
- `BK\cs_BK_<CODE>_smt.sz` (the start line and each checkpoint gate) loads as
  the car reaches it: the stage begins there.
- Each gate also loads the next fork's two signs,
  `COMMON\obj_OBJ_<CODE>_BUNKI_TX_smt.sz`, which name the two stages the fork
  leads to. With the routes taken this gives the whole map:

| Stage | Column 0 (left) | 1 | 2 | 3 | 4 |
| --- | --- | --- | --- | --- | --- |
| 1 | BEAC Palm Beach | | | | |
| 2 | SANF Bay Area | YOSE National Park | | | |
| 3 | NIAG Water Falls | SEQU Big Forest | GRAN Canyon | | |
| 4 | MACH Lost City | LASV Casino Town | ALAS (Alaska) | AMAZ Jungle | |
| 5 | EAST Giant Statues | MAYA (Maya) | PRIN Floral Village | FLOR (Florida) | NEWY Skyscrapers |

  (Names in brackets are the game's internal names from Text\English_US.bin;
  the others are the names the port's notes use. GRAN/AMAZ/NEWY/FLOR
  positions follow from the signs and the earlier RRRR sweep's stage names;
  RRLR was run to check GRAN, ALAS and FLOR.) Route letters in the log are
  the forks taken, L or R.
- `obj_end_<code>` is the ending (goal), `spr_ranking_xst` and
  `ani_SPRANI_RANKING_CVT` the name entry/ranking after a race, and
  `ani_SPRANI_NAME5<x>` its goal letter.
- `CARS\obj_plcar_<model>_smt.sz` is the car (the last one loaded before a
  race; car select previews load each car).
- Time Attack loads ghosts (`Ghosts\gc_*`) or `ani_SPRANI_TAEXTRA`.

## Limits

- OutRun and Heart Attack load the same files, so the mode reads "single
  player" for both. The OutRun2 (classic) course set is not named: its
  courses do not use the Stage folders.
- The race clock is an estimate (see above).
- Mark screenshots use PrintWindow; exclusive fullscreen may give a black
  image on some drivers.

## Release additions (11 November)

Quiet lines, on by default, nothing shown to the player:

- `Game: hitch N ms [while loading] at <stage, race time>`: a frame over 100 ms while driving, at most one line every 5 s (the rest counted on the next line and in the session summary).
- `Display: windowed 1280x720 -> fullscreen 3440x1440`: display mode or window size changes, checked once a second.
- `Controller N connected` / `disconnected`.
- `Save: reading|writing (<where>)`, `Save: none yet`, `Save: ... failed, status X`: the save file through the same NtCreateFile wrapper.
- `ORNATIVE GPU device removed (reason X) at <where>` (Direct3D 12) and shader/pipeline failures now name the stage or screen.
- The session summary adds hitches over 100 ms and saves written (failures marked).
- The header shows `log_max_files` (how many logs are kept).

Multiplayer events come from the runtime ("[netplay]" and XSession lines) and the screen breadcrumbs; release builds rewrite the runtime's address-bearing log lines so no IP address is written (tools/Redact-NetplayLog.py), and log the hosting game's per-frame QoS listen once.
