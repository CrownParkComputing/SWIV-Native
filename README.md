# SWIV Amiga — native (Retro Recompilation)

A native, emulator-free recreation of **S.W.I.V.** (Storm / The Sales Curve, 1991, Amiga) in C11 + raylib,
built by reading the game's code and data out of the original and re-implementing them:

- all 73 enemy behaviour scripts, player helicopter & jeep, bullets, collisions, map-driven spawning,
  per-scanline palette, hidden collision plane, power-ups — ported from the 68000 behaviour scripts
- the front end: title, publisher text, blueprint screens, hi-score tables, controls screen, credits,
  GET READY, stats, name entry, ending — in the game's own font
- the sound driver's 29 effects synthesised from their Paula parameters (verified against the host audio),
  MOD music via libxmp
- extras: options (difficulty easy/normal/hard, volumes, fullscreen), SFX tuning screen, map / sprite
  viewers, Hall of Light media screen (fetch with `tools/fetch_hol_extras.py`)

## Requirements
- raylib (static, expected under `~/.local`), libxmp, a C11 compiler
- the game disk image `SWIVFIX.ADF` (not included): the default path is `/home/jon/swiv-amiga-re/SWIVFIX.ADF`,
  override with `--adf PATH`

## Build / run
    make                     # build/swivview (the game), build/simrun (headless engine), build/dumpmap
    ./build/swivview         # boots into the attract sequence; port 1 = jeep (WASD+Shift / pad 1), port 2 = heli (arrows+Space / pad 0)
    make test                # native map renderer identical to the reference renderer, 7 levels

`re/` holds the reverse-engineering notes (object model, verb library, porting guide, handler table).
The headless `build/simrun` + `tools/parity.py` diff the native engine against the original's per-frame
object log for regression testing.
