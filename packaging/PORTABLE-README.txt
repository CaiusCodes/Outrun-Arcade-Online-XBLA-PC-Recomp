OutRun Online Arcade PC - 0.9.0

A native Windows PC port of OutRun Online Arcade (Xbox 360 / XBLA), built with
ReXGlue static recompilation. You must provide your own copy of the game: this
ZIP contains no original game files.

FIRST START
1. Extract the whole ZIP (keep the "OutRun Online Arcade" folder together).
2. Open "OutRun Online Arcade.exe".
3. Choose your OutRun Online Arcade Xbox 360 package (title ID 58410968,
   revision 0.0.1.5) or drop it onto the window. The package is never changed.
4. Optional: start in fullscreen, create a desktop shortcut, choose another
   folder for saves and user data (default: resources\userdata).
5. Click Play. Later starts go straight into the game. To reopen setup, use
   Help & Options > Setup in the game, or hold Shift while opening the launcher.

CONTROLS
An Xbox controller is recommended. Keyboard: W accelerate, S brake, A/D steer,
Space = A, Backspace = B, Enter = Start, Q = X, E = Y, Z = LB, C = RB.
In menus Enter selects and Esc goes back; Esc pauses a race. The mouse works
in the menus (hover, click, right click = back, wheel scrolls).

SETTINGS
Help & Options > Graphics: Display Mode, Window Resolution, Render Scale,
Image Filter (Sharpen / FSR), Texture Filtering, Anti-aliasing, Aspect Ratio,
Show FPS and Graphics API (Direct3D 12 / Vulkan). Help & Options > Settings:
the game's own options plus HUD (Original / Sharp / HD / Off), Vivid Colors,
Contrast and Black Level. Rows marked "(restart)" apply after a restart, which
the game offers. The original Xenos renderer can be used instead of the native
one with gpu_native = false in resources\outrun.toml.

MULTIPLAYER
LAN and virtual LAN (for example Radmin VPN, ZeroTier, Tailscale, Hamachi).
Everyone joins the same network, then: host with Multiplayer > Host LAN Match,
join with Multiplayer > LAN Match > Quick Match. If no lobby appears, enter the
host's IPv4 address under Multiplayer > Multiplayer Options > Host Address.
Allow the game through Windows Firewall when asked. Xbox LIVE is not used.

UPDATES
Help & Options > Check for Updates asks this project's GitHub releases for a
newer version (nothing is sent unless you choose it). Updating replaces only
the program files; your imported game files, saves and settings are kept. You
can also extract a newer release ZIP over this folder.

REPORTING A PROBLEM
Close the game and attach the latest log from resources\logs (the newest
outrun_NNN.log) to your report, with what happened and roughly where:
https://github.com/CaiusCodes/Outrun-Online-Arcade-XBLA-PC-Recomp/issues

FOLDERS
OutRun Online Arcade.exe   launcher, importer and updater
resources\                 the game program and runtime
resources\assets\          your imported game files
resources\userdata\        saves (unless you chose another folder)
resources\outrun.toml, pc-settings.toml, multiplayer-settings.toml   settings
resources\cache\, logs\    shader cache and logs (safe to delete)
licenses\                  third-party licences

No administrator rights, registry entries or system-wide install are used.
OutRun is a trademark of SEGA. Xbox and Xbox 360 are trademarks of Microsoft.
This is an independent fan project, not affiliated with or endorsed by them.
