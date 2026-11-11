# Outrun SDK source patches

`ffmpeg-xmaframes-reset.patch` registers the missing raw-XMA codec reset
callback in the pinned FFmpeg submodule. The runtime already requests a
decoder flush, but without this callback its overlap buffer survives reuse.
The corrected private probe reproduces corrupted sound beginnings with the
old DLL in 15 bank/sample cases; all 60 interrupted Clear/Release comparisons
pass after the fix, with clean decodes unchanged. See
`docs/AUDIO-DECODER-RESET-20261019.md`. In-game confirmation remains pending.
`Apply-SdkPatches.py` includes this layer and resolves its original file from
the FFmpeg submodule's HEAD before validating/writing. Private captures and
extracted samples are not part of the patch or a release payload.

`audio-context-consumer-read.patch` is an audio investigation patch applied
after the SDK snapshot below. It prevents the decoder's old snapshot from
rewinding the guest-owned output read position. `git apply --check` succeeds
against OutRun's local SDK snapshot. A direct probe of the shipped runtime
reproduces read position 7 becoming 3; the isolated compatibility-DLL test
preserves 7 while retaining producer/flag/config updates. This is a confirmed
state-merge bug, but the October 14 listening test produced no audible improvement to the
reported steering sound. It is not a demonstrated steering-audio fix.

`tools/Patch-AudioContextRead.py` creates a separate Windows test DLL from the
exact pinned September cohort, guarded by SHA-256 and an instruction signature.
It removes only the 19-byte stale-read assignment, preserving the original
prologue, unwind metadata, import table, function size and the rest of the DLL.
It refuses to overwrite the original or an existing output. This keeps the
working graphics/runtime cohort intact during diagnosis; use the source patch
for a future SDK rebuild after regression testing. No patched DLL is currently
part of the release payload.

The SDK starts at upstream ReXGlue `v0.9.0`, commit
`3eb9b511b4140d2769e27be63eae57d41bfa2afa`.

`sdk-snapshot.patch` captures the SDK source cohort used during Outrun
development. It is the difference between upstream v0.9.0 and the local
`sdk-snapshot-2026-10-08` commit. It includes shared work for other XBLA
ports; this is a reproducibility baseline, not a claim that every change is
needed by Outrun. The installed September runtime predates that snapshot, so
new binaries require independent regression testing before replacing any
existing release or install.

`libmspack-real-sources.patch` selects libmspack's real source directory
instead of its `cabextract/mspack` symlinks, allowing Windows checkouts with
`core.symlinks=false` without copying or replacing stub files.
