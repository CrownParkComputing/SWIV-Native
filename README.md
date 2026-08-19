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

Viewer keys — Map: `1-7` level, `SPACE` pause, `UP/DOWN` scrub, `+/-` speed,
`O` objects, `HOME` restart.  Sprites: `LEFT/RIGHT` file, `UP/DOWN` frame,
`PGUP/PGDN` palette level, `A` animate.  `F2` screenshot, `ESC` quit.
Headless: `--shot out.png [--scroll N] [--sprites]`.

The map scrolls at the game's ¼ px/frame with one palette per frame taken
from the map's colour commands (the sunset etc. is data in the map).
Validation chain:

1. `make test` — native renderer is bit-identical to `map.py` (14 images).
2. `~/amiga-decomp/tools/swiv_scroll_gate.py` — native TOWN render vs the
   Musashi oracle frames: scroll monotonic, ≥75 % pixel-exact incl. sprites
   (≈97 % on sprite-free terrain).

Disk image not included (`SWIVFIX.ADF`, see swiv-amiga-re).
