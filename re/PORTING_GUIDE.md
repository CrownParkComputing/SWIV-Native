# Porting a SWIV behaviour handler to C (route B: semantic rewrite)

You translate 68000 behaviour scripts (AMPROG.OBJ) into C coroutines against the native engine.
Read in this order: `re/VERBS.md` (verb semantics), `re/OBJECT.md` (object fields), `src/engine/engine.h`
(the C API — every verb in VERBS.md has a C function; the names say which LAB it is), `src/engine/effects.c`
(examples of scripts written against the API: explosion, homing bullet, missile), `src/behaviours/table.c`
(the dispatch table; your functions must have exactly the `bh_*` names declared there as weak symbols).

## Rules
* One C function per handler entry: `void bh_name(Obj *o)`. It runs as a coroutine: write it as the
  script is written — sequential code, loops, waits. `o` is A5. When the original does `BRA.W LAB_0725`
  (die), just `return;` — the engine performs LAB_0725 (enemy_cleanup + free) when the function returns.
  If the original frees with LAB_04DC directly (no enemy cleanup), call `eng_free(o); return;`.
* Every wait verb returns nonzero when the object was signalled: translate `BSR wait ; BNE die` as
  `if (wait_xxx(o, ...)) return;`.
* Registers the script keeps across waits (D-registers used as loop counters etc.) → use locals; they are
  preserved across waits (real coroutine stack). Object-field locals 276..291 → `o->w[0..7]` (w[0] = 276 =
  the map record's type nibble, copied to children by spawn), 292..307 → `o->w[8..15]` (not copied).
* Spawning: `spawn(handler)` returns the child (`Obj *c`); the child starts running on the next tick, so
  you may patch `c->x/c->y/c->angle/c->w[..]` right after spawning, exactly like the original writes through
  A0. Children that are NOT in the dispatch table are `static void` functions in your file.
* `LAB_071D formation(dx,dy,count,dparam)`: the clones run the CALLER's continuation. In C, put the code
  after the BSR into a `static void cont_xxx(Obj *o)` and call `formation(o, dx, dy, count, dparam, cont_xxx);
  cont_xxx(o); return;` (the caller itself also continues there). Note the caller's x/y/w[0] are advanced
  count-1 times by formation() before it continues.
* Inline-operand verbs (`BSR LAB_0537 ; DC.W gfx`, `LAB_053D`, `LAB_0492/0497`, `LAB_0528 + anim words`) take
  the words following the BSR as arguments; anim scripts become `static const int16_t ANIM_X[] = {...}` using
  the A_* macros (A_RATE(n), A_LOOP, A_SETLOOP(n), A_END, A_END_SIGNAL ...) with the frame words verbatim.
* Fixed point: positions 16.16 (`o->x += 5 << 16`), speed 8.8, angle uint8. `MOVE.W #n,320(A5)` writes the
  integer part: `o->x = n << 16` (it also zeroes the fraction — keep that).
* Globals: `g.scroll3530`, `g.scroll3542`, `g.heli`/`g.jeep` records (x,y,z 16.16, alive, score), `rng()`,
  `g.threat156`, `threat_ok()`, `g.missile_budget206`, `g.difficulty182`, `g.tick`.
* Sounds: `sfx(SFX_*, x)`. Unknown LAB_03xx sound routines → pick the closest SFX_ and leave a comment.
* If a verb you need is missing from engine.h, implement it in your file as `static` with a `/* LAB_xxxx */`
  comment and describe it in your report so it can be promoted to the engine.
* Keep a `/* LAB_xxxx @ $addr */` comment per handler and per non-obvious block, so the port can be audited
  against the listing. Do NOT invent behaviour: if a piece is unclear, port it literally (even if ugly) and
  flag it in the report.

## Build & check
`make build/simrun && ./build/simrun 0 4000 build/sim.txt --fire` must compile with your file (put it at
`src/behaviours/bh_groupN.c`; the Makefile wildcard picks it up). Then
`python3 tools/parity.py re/trace/objlog_4000.txt build/sim.txt --names re/names.txt [--gfx HEX]` compares
per-graphic live counts/trajectories with the original (TOWN level only reaches some enemies; others can
only be compile-checked). `re/stats/objlog_stats.txt` has measured activation thresholds/lifetimes per
graphic (host). Listing for your group: `re/handlers_groupN.asm` (addresses in `;xxxxxx:` comments match
`re/handlers.txt` / table.c). Runtime-image listing (15 insertions, see OBJECT.md shift table):
`re/pipeline/disasm/listing.asm` — consult it if the disk listing looks inconsistent.
