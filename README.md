# SWIV-Native

Native (no 68000, no chipset emulation) decoder + viewer for the Amiga
**S.W.I.V.** game disk.  Everything is plain C11; the viewer uses raylib.

Formats are a C port of the measured documentation in
`~/swiv-amiga-re/docs` (catalogue, stream-C depacker, `.LIN` sprites,
`.PAM` maps, level table at AMPROG `0x384C`).

    make                      # build/dumpmap + build/swivview
    make test                 # native render == python reference, all 7 levels, pixel-identical
    ./build/swivview          # raylib viewer (TAB = map / sprites)
    ./build/dumpmap 0 town.ppm [--objects] [--bake]

Viewer: all controls are on-screen buttons (mouse or touch) — level 1-7, pause,
objects, restart, speed presets, scrub + progress bar; sprites: file/frame, animate, zoom,
palette source (7 level palettes, fully faded-in, or the 11 AMPROG screen palettes).
Headless: `--shot out.png [--scroll N] [--sprites --file X.LIN --pal N]`.

The map scrolls at the game's ¼ px/frame with one palette per frame taken
from the map's colour commands (the sunset etc. is data in the map).
Validation chain:

1. `make test` — native renderer is bit-identical to `map.py` (14 images).
2. `~/amiga-decomp/tools/swiv_scroll_gate.py` — native TOWN render vs the
   Musashi oracle frames: scroll monotonic, ≥75 % pixel-exact incl. sprites
   (≈97 % on sprite-free terrain).

Disk image not included (`SWIVFIX.ADF`, see swiv-amiga-re).

## Play mode (prototype engine)

`PLAY` in the viewer (or `--play`).  `src/game.[ch]` is a native shoot-'em-up
core driven by the real map records: an object record is spawned when it comes
within 256 px of the screen edge (as the original's `0x365e`), classified per
sprite file (ground static / ground mover / air / mine / pickup / scenery —
table `CLASS_TAB`, hand-measured from which sprites appear as what), with
player heli, bullets, aimed enemy fire, collisions, EXPL1/EXPL2/PLOP explosions,
score/lives/power.  Controls: arrows + space, or touch — left half drag = stick,
right half = fire.  Behaviours are approximations until they are read out of
AMPROG.OBJ; map/terrain/palette are exact.
