# SWIV behaviour-script verb library — C pseudocode specification

Companion to `OBJECT.md` (field names below are `o->f<offset>` of the current object `o` = A5;
`g<off>` = global at A6+off). Listing addresses are from `amprog.asm`; add the shift table in OBJECT.md
to get runtime/trace addresses.

Conventions
* **Yield** = `LAB_0499` = kernel `-1438(A6)`: give control to the next task; the caller resumes on the
  next game tick (one run of list -698, every `-76(A6)` VBLs = 2 VBLs in play). While 165(A6) (pause) is
  set it keeps yielding.
* **Signal** = `-1414(A6)` / `LAB_053B`: `o->time12--`. After that `-1418(A6)` (`D0 = time12 - *clock8`) is
  non-zero for that object = "killed / interrupted". Every wait verb returns those flags:
  **EQ (Z=1) = completed normally, NE = the object was signalled** (hit, off-screen, orphaned, generation
  change...). Scripts follow waits with `BNE die` (`BRA.W LAB_0725`).
* Return-flag idiom for non-wait verbs: `CMPA.L A6,A7` → Z clear = **OK/true**; `CMP.W D0,D0` → Z set =
  **failed/false**.
* "tick" = one game update; "VBL" = one 50 Hz frame. Physics integrates once per VBL elapsed.
* Angles: 256 per turn, 0 = +x (right), 64 = +y (down), 128 = left, 192 = up. (verified, see §Aim)
* Positions 16.16; speed 8.8 px/VBL. `px(v) = v >> 16`.

--------------------------------------------------------------------------------------------------
## 1. Per-tick core

### LAB_04E9 ($211FF2) `int step(void)` — ONE game tick of physics + render + events. Returns NE if signalled.
```c
int step(void) {
    if (clock_mismatch()) return NE;                 // -1418: already signalled → do nothing
    if (o->flags367 & ATTACHED(8) && o->parent308) { o->pos = parent->pos (x,y,z longs); n = 1; }
    else n = g_76;                                   // VBLs elapsed (2)
    for (i = 0; i < n; i++) {                        // integrate (longs, 16.16)
        vx += ax; vy += ay; vz += az;  x += vx; y += vy; z += vz;
    }
    if (z < 0) { z = 0; vz = 0; az = 0; }            // landed
    if (o->timer486) o->timer486 = max(0, o->timer486 - g_76);
    offscreen_check();                               // LAB_04FB, may call o->cb538 (default: signal self)
    if (anim_step(&o->animA380)) box_size_from_frame();   // LAB_052C / LAB_0539: 500/502 = hw/hh of 398
    anim_step(&o->animB422);                          // LAB_052B (result ignored; 440 = frame)
    fill_blit_slots();                                // slot A (x,y,gfx398,flags397|flash), B (if 438), C shadow (if z && !NO_SHADOW)
    insert slots into display list (LAB_0383)
    o->box496 = x.w; o->box498 = y.w;                 // LAB_053C (collision position for this tick)
    o->scroll372 = g3542;
    yield();                                          // <<< the collision/bullet task and the map task run here
    if (o->flags367 & SCREEN_LOCKED(16)) y.w += g3542 - o->scroll372;   // keep screen position
    o->flags367 &= ~HIT_FLASH(2);
    if (o->cb542 >= 0 && o->parent308 == 0) o->cb542();      // orphan/think callback
    if (o->cb534 >= 0 && g169 /*smart bomb*/) o->cb534();
    dispatch_events();                                // LAB_04FE
    return clock_mismatch();                          // NE = signalled during this tick
}
```
Off-screen check (LAB_04FB, $2121A0): with `m = o->margin364` (default -64), `sy = y.w - g3542`, `sx = x.w`:
`if (o->cb538 >= 0 && (sy <= m || sx <= m || sy-256 >= -m || sx-320 >= -m)) o->cb538();`
Default cb538 = LAB_053B = signal (object dies when 64 px outside the 320×256 screen; margin 0 = at the edge).

### LAB_04FE ($2121D6) `dispatch_events()` — collision-event dispatch (order: bit0, 3, 4, 1, 2, 5)
```c
w = o->hits506 & o->enable508;
if (w & 1)  { o->h510(); w = o->hits506 & o->enable508; }   // hit by player bullet
if (w & 8)  { o->h514(); ... }                               // touched by JEEP
if (w & 16) { o->h522(); ... }                               // touched by HELI
if (w & 2)  { o->h518(); ... }                               // touched a jeep-killer
if (w & 4)  { o->h526(); ... }                               // touched a heli-killer
if (w & 32) { o->h530(); }
o->hits506 = 0;
```
Handlers run on the object's own stack inside step(); they usually just `signal()` or do LAB_0728.
Event registration verbs (A0 = handler): LAB_0505 → 510 (bit0); LAB_0506 → 518 (bit1); LAB_0507 → 526
(bit2); LAB_0509 → 514 (bit3); LAB_050A → 522 (bit4); LAB_0508 → 514 AND 522 (bits 3+4, "touched by
either player"); $21229E → 530 (bit5, unused). Disable: LAB_050B bit0, LAB_050C bit1, LAB_050D bit2,
$2122B2 bits1+2, LAB_050F bit4, LAB_050E bits 3+4, $2122D6 bit5. Default handler in every slot = LAB_04DC
(free object immediately). `LAB_0634`'s mask: `LAB_04D1` leaves 508 = 0, so nothing fires unless enabled.

### What sets the bits of 506 — collision classes (see §6)
Every collision box carries an identity/mask word (504). When two boxes overlap, each gets the OTHER's
mask ORed into its hits word (506). Classes:
| bit | meaning | who has it |
|---|---|---|
| 0 | player bullet | player bullets ($8041 heli, $8081 jeep) |
| 1 | kills the jeep (ground hazard) | ground enemies (D1=34=$22), bullets (38=$26), hazards (6) |
| 2 | kills the heli (air hazard) | air enemies (36=$24), bullets, hazards |
| 3 | is the JEEP | jeep player box $0048/$0088 |
| 4 | is the HELI | heli player box $0050/$0090 |
| 5 | solid: absorbs player bullets (bullet explodes) | all shootable enemies (34/36/38/32) |
| 6 | owner = heli | heli's bullets ($8041) and heli box ($0050) |
| 7 | owner = jeep | jeep's bullets ($8081) and jeep box ($0088) |
| 15 | passive: this box never scans, only receives | player bullets, flashes ($8000 enemies) |
Therefore for an enemy: bit0 = shot, bit3 = rammed by jeep, bit4 = rammed by heli; bit6/7 of 506 tell
which player's bullet hit (used for scoring). Enemy–enemy overlaps also set bits 1/2/5 but those are not
enabled for enemies. LAB_0720 enables bit0 (if hp≠0), bit3 (if D1 bit1), bit4 (if D1 bit2), all → LAB_0728.

--------------------------------------------------------------------------------------------------
## 2. Waiting verbs (all step physics each tick unless noted)

| verb | addr | semantics | returns |
|---|---|---|---|
| LAB_04E9 | $211FF2 | one tick (above) | NE = signalled |
| LAB_04E8 | $211FEC | `while (step()==EQ);` — run until signalled (hit/offscreen/orphan/bomb) | always NE |
| LAB_04E4 (D0=n) | $211FD8 | `for(i<n) if(step()) break;` n ticks | flags of -1418 |
| LAB_04DE (D0=n) | $211FBC | `t=g_66+n; while((short)(g_66-t)<0) if(step()) break;` n ticks (absolute tick counter) | flags |
| LAB_04DD | $211FBA | = LAB_04DE(1) = one tick | flags |
| LAB_06D5 (D0=m) | $21581A | `while ((u16)(y.w - m) < g3530) if(step()) break;` = step until the object is on/near screen: `y >= scroll + m` (m = -16: 16 px above the top edge; m = 100: 100 px down into the screen). Unlabelled entry $215818 = m=-32. | flags |
| LAB_06D4 (D0=m) | $215808 | LAB_06D5 with events disabled (508 saved/cleared/restored) | flags |
| LAB_06D0 (D0=m) | $2157E8 | same condition but **plain yields (no physics, no render)** — used by LAB_0720 so the enemy is invisible/inert until it is about to scroll on (`y >= 3530 + m`). | flags |
| LAB_0499 | $211C2A | yield once (stays yielding while paused 165(A6)) | — |
| LAB_049D (D0=n) / LAB_049C | $211C42 / $211C40 | n plain yields (tick counter -68); 049C = 1 | — |
| LAB_049F (D0=n) | $211C58 | n plain yields | — |
| -1418(A6) | | `D0 = time12 - *clock8`; NE = signalled. Scripts call it to poll. | flags |
| -1422(A6) (A0) | | use `*A0` as this object's clock (8(A5)=A0, 12(A5)=*A0) – e.g. 12534(A6) boss-group clock | |

--------------------------------------------------------------------------------------------------
## 3. Spawning / lifetime

### LAB_04D2 ($211E9A) `obj *spawn_prio(int prio_D0, handler A0)`; LAB_04D1 ($211E98) = prio 100.
```c
obj *c = kalloc(0x222); if (!c) return NULL (Z set);
ktask_start(c, pc=A0, prio);                    // -1450: link into list -698, initial SR $2200
c->timer486 = 0; c->parent308 = c->child312 = c->sib316 = 0;
c->pos/vel/acc (320..355) = o's;  c->speed356+angle358 (long) = o's;
c->w276..w291 (8 words) = o's;   c->clock8 = o->clock8; c->time12 = o->time12;
c->margin364 = -64; c->cb538 = LAB_053B; c->cb542.w = -1; c->cb534.w = -1 (negative = none);
c->w374 = 0; c->threat370 = 0; c->death376 = LAB_0634;
c->h510..h530 = LAB_04DC; c->flags367 = 0; c->enable508 = 0;
animA/animB inactive (396=0,397=0,438=0,439=0); blit slots reset (key $64, 18=$100);
c->flags397 |= (o->flags397 & 1);
return c (Z clear);                              // A0 = child; NOTE A5 (current) unchanged
```
The new task starts executing `handler` with A5 = child on the NEXT scheduler pass (after the parent
yields). Caller typically patches child fields through A0 right after spawning (e.g. `ADD.W D0,320(A0)`).
* LAB_04CD ($211E80) `spawn(handler)`: prio 100, **retries (yielding) until memory is available**.
* LAB_04C9 ($211E64) `spawn_attached(handler)`: LAB_04CD + `child->parent308 = o`, linked into o's child
  list (LAB_04BA), `child->cb542 = LAB_053B` (child dies when orphaned). Child still needs `flags367 |= 8`
  to follow the parent's position. LAB_04CA ($211E6A): same but try-once (Z set on failure).
* LAB_04C6 ($211E18): spawn with pos/vel/acc zeroed and clock = 202(A6) (player-side).
* LAB_071D ($215FC2) `formation(dx_D0, dy_D1, count_D2, dparam_D3)`: spawns count-1 clones running the
  caller's own continuation (return address), each placed at the current pos; after each spawn the caller
  moves `x += dx, y += dy, w276 += dparam`. Result: count objects in a line, all executing the code after
  the BSR. (Trace: YELLOW 6 planes 8 px apart.)
* LAB_04DC ($211FA8) `free()`: detach all children (LAB_04C3: their 308 = 0), unlink from parent
  (LAB_04BD), kfree(0x222) and never return (task ends). Default event handler.
* LAB_0724 ($216056) `enemy_cleanup()`: `g156 -= threat370; gfx_release(gfx368); box_unlink()` (LAB_0541).
* **LAB_0725 ($21606C) `die()` = LAB_0724 + LAB_04DC.** Every enemy script ends with `BRA.W LAB_0725`.
  Note it does NOT spawn an explosion; LAB_0729 does that before signalling.

### LAB_0720 ($215FE6) `enemy_init(gfx_D0, mask_D1, onscreen_margin_D2, hp_D3, score_D4, threat_D5)`
```c
o->threat370 = threat; o->score362 = score; o->gfx368 = gfx;
gfx_acquire(gfx);                 // LAB_0493: refcount++ in table 11566(A6)[gfx&$1FF]; YIELD until loaded
LAB_06D0(margin);                 // plain-yield until y >= g3530 + margin (object inert & invisible)
box_register(mask);               // LAB_053E: box at 488, 496/498=0, hw=hh=8, mask504=mask, hits506=0, link in 11078 list
set_frame(gfx);                   // LAB_0538: 398 = gfx; 500/502 = frame hw/hh
o->hp360 = hp;
if (hp) { on(bit0, LAB_0728);  if (mask & 4) on(bit4, LAB_0728);  if (mask & 2) on(bit3, LAB_0728); }
o->cb534 = LAB_0729;              // smart bomb kills it
g156 += threat;
```
Typical masks: 34=$22 ground enemy (jeep-killer, solid), 36=$24 air enemy (heli-killer, solid),
38=$26 both (bullets, bosses), 32 shootable but harmless, 6 hazard both (not shootable, bullets pass),
4 heli-only hazard, 0 none, $8000 passive (flashes/decor). margin: -48/-16 typical, positive = wait until
well inside the screen (e.g. 100, 192, 288 for things that appear from the bottom/side).

### LAB_0728 ($216082) `on_bullet_hit()` (event bit0/3/4 handler)
```c
if (--o->hp360 > 0) { sfx_hit(x); o->flags367 |= HIT_FLASH; return; }   // LAB_0726: LAB_03EB + bit1
LAB_0729();
```
### LAB_0729 ($21608A) `kill()` — award score, spawn death effect, signal self
```c
g12498++;
if (o->hits506 & 0x40) heli_rec(11176)->score76 += o->score362;       // bit6 = heli's bullet
else if (o->hits506 & 0x80) jeep_rec(11356)->score76 += o->score362;  // bit7 = jeep's
spawn_prio100(o->death376);                        // default LAB_0634 explosion at o's pos (copies pos/vel/w276..)
if (o->w374) { c = spawn(LAB_0636); if (c) c->w276 = o->w374; }       // bonus/score popup gfx
signal();                                          // LAB_053B → the script's wait returns NE → BRA LAB_0725
```
(Verified in trace: two LAB_0729 calls with 506=$8041 → heli record scored 12 and 50.)

### Death / explosion handlers (spawned via 376 or directly)
* LAB_0634 ($21466A): sfx LAB_03C5(x) then LAB_0635.
* LAB_0635: explosion: gfx set 5, anim rate 4, frames $0E05..$1A05, then $8800 (signal) → LAB_04E8 → release
  gfx → free. flags367 |= NO_SHADOW, z += 1, no motion (LAB_053A).
* LAB_062D ($214596): burning wreck: gfx set 6 anim (rate 6, frames $0006..$0C06, signal), then every 15
  ticks spawn an LAB_0635 at a random ±31 offset (uses LAB_0629) until signalled (off-screen); sfx LAB_03C3.
* LAB_062F/LAB_0630 ($21460C/$21461C): ring of 8 (speed $600) / 16 (speed $300) LAB_0635s flying outward
  (angle += 100 each, speed grows), used for big deaths / player death.
* LAB_0636 ($2146AC): popup: flags397 |= $41, no motion, z=0, 418=$140, shows gfx w276 for 1 tick, then
  moves 320 px up, 1 tick, free. (w276 = o->w374 of the victim, e.g. $1011 "bonus" icon.)
* LAB_07D0 ($2176D4) `explode_at(dx_D0, dy_D1)`: spawn LAB_062D at pos+(dx,dy) then wait 20 ticks (LAB_049D).
* LAB_07E3 ($21791A): boss damage smoke: `w282 = 0; if (hp360 <= 50) { r=rng(); w282 = (r&3)-2; if ((r&15)==0) { spawn(LAB_05E9); c=spawn(LAB_0634); if (c) c->x += (rng()&31)-15; ...}}`

--------------------------------------------------------------------------------------------------
## 4. Motion / aiming verbs

| verb | addr | pseudocode |
|---|---|---|
| LAB_053A | $212AB6 | `stop()`: vx=vy=vz=ax=ay=az=0; speed356=0 |
| LAB_0515 | $212312 | `set_velocity_from_angle()`: `a=angle358&=255; vx = speed356*cos(a); vy = speed356*sin(a)` (longs; sin table below, result is 16.16 because sin is ×256 and speed is ×256). vz untouched. |
| LAB_051F (D0=a) | $21278A | `D1 = SIN[a&255]; D0 = SIN[(a+64)&255] (=cos)`; table LAB_0520 at $2127A2: 256 words, `round(256*sin(2πi/256))` (0,6,13,19,25,31,38,44,…,256,…). |
| LAB_0516 (D0=tx,D1=ty,D2=sx,D3=sy) | $212330 | `angle_to()`: `dx=tx-sx, dy=ty-sy` (words); `a = ATAN[min-scaled |dy|][|dx|]` (32×32 byte table LAB_051D at $21238A, both shifted right together until < 32); `if (dx<0) a = 128 - a; if (dy<0) a = -a;` returns D0 = a & 255. **Verified: 14 trace samples match atan2(dy,dx)·256/2π within ±1.5.** |
| LAB_0510 (D0=tx,D1=ty,D2=maxstep) | $2122DE | `turn_towards(tx,ty,max)`: `a = angle_to(tx,ty,x.w,y.w); if (max==0) angle358 = a; else { d = (int8)(a - angle358); angle358 += clamp(d, -max, +max); }` (shortest way round). Does NOT recompute velocity — call LAB_0515 after. |
| LAB_071B (D0=base) | $215F9C | `set_frame(base + (((angle358+16)&$E0) << 4))` → 8-direction frame: frame index = (angle+16)/32, i.e. frame 0 = right, 2 = down, 4 = left, 6 = up (frame field = dir*2... precisely gfx word = base + dir8*$200). |
| LAB_071C (D0=base) | $215FB0 | 16 directions: `set_frame(base + (((angle358+8)&$F0) << 5))` = base + dir16*$200. |
| LAB_0719 / LAB_071A (A0=table) | $215F72/$215F88 | `set_frame(table[dir8])` / `set_frame(table[dir16])` (word tables). |
| LAB_067A / LAB_0679 | $215048/$215042 | `blocked_ahead()`: temporarily move by 2×(vx,vy), pixel-collision-test the sprite against the background (LAB_030E / LAB_030D = two modes, result 162(A6)), restore; returns NE if overlapping. Jeep terrain test. |
| LAB_068C / LAB_0692 | $215210/$21526C | clamp the player: x to 4..316, y to [scroll+4, scroll+252]; 068C (jeep) also clamps y to 3558(A6) and, if driving into the bottom edge onto terrain, spawns LAB_0630 (death). |
| LAB_0697 (D0=joy) | $2152AA | joystick nibble → angle (table: up=$C0, down=$40, left=$80, right=0, diagonals $A0/$60/$E0/$20); Z set if no direction. |

### Player position lookup (all return D0 = x, D1 = y in object units)
| LAB_058A | $213032 | `nearest_player()`: if both alive pick the one with smaller |dx|+|dy|; else the live one; **Z clear**. If none: D0=160, D1=g3530+192, **Z set**. |
| LAB_0587 | $21300E | `alternate_player()`: both alive → toggle g168 and alternate; else the live one (no flag; stale pos if none). |
| LAB_0581 / LAB_0582 | $212FC6/$212FCA | prefer player#1 (heli, 56==1) / player#0 (jeep); fall back to the other; **Z clear**. None alive: D0 = 96+t, D1 = g3542+128+t (t=|(int8)tick|), **Z set**. |
Player records: HELI 11176(A6), JEEP 11356(A6); 70/72/74 = x,y,z, 54 = alive, 76.l score (see OBJECT.md).

### Aim-at-player recipe used by the handlers
`LAB_058A (or 0587/0581) → LAB_0510(D2=0 or max turn) → LAB_0515 → LAB_071B/071C` — then pass `angle358`
(or the computed D0) as the bullet angle to LAB_0613.

--------------------------------------------------------------------------------------------------
## 5. Firing verbs (how enemies shoot)

### LAB_0613 ($214250) `fire_homing(dx_D0, dy_D1, angle_D2)` — THE standard enemy bullet (13 sites, + LAB_0809)
Spawns two prio-100 children at `pos + (dx,dy)`:
* **LAB_0616 bullet** (gfx $0802 = set 2 frame 4; LAB_0720(gfx,38,-16,hp 1,score 7,threat 5)):
  flags367 |= SCREEN_LOCKED; stop(); `w276 = LAB_0587 player` (alternating); z = 32; NO_SHADOW; margin364 = 0
  (dies at the screen edge); speed356 = $300 (3 px/VBL); angle = D2; LAB_0515; frame = $0002 + dir16;
  wait 20 ticks; then `for (n = 2*(5 + g182); n--; ) { (tx,ty)=player w276 pos; turn_towards(tx,ty,14);
  LAB_0515; frame=$0002+dir16; if (LAB_04DE(8)) break; }` then LAB_04E8 (fly straight until signalled) → LAB_0725.
  Because it was LAB_0720'd with mask 38 and hp 1, touching either player (bits 3/4) or a player bullet
  kills it (LAB_0728→0729: explosion, score 7 to the shooter-of-record!). Touching it gives the player
  bits 1/2 → player death.
* **LAB_0619 flash** (gfx 7, mask $8000 passive, threat 1): z = 33, NO_SHADOW, margin 0, anim rate 1:
  $0401 then $8800 signal → LAB_04E8 → LAB_0725. (~2 VBL muzzle flash.)
The child's returned A0 is used by LAB_0613 to add (dx,dy) and set angle (child has not run yet).

### LAB_0809 ($217D2E) `fire_pattern(idx_D0)` (idx 0..4): `set_frame(T[idx].gfx); angle=T[idx].angle;
fire_homing(T[idx].d0x,d0y,angle); wait 5 ticks; fire_homing(T[idx].d1x,d1y,angle); wait 5 ticks`.
Table LAB_080B ($217D60): {gfx,angle,(dx,dy),(dx,dy)} = {$0232,106,(-15,16),(-22,4)}, {$0432,85,(-6,21),(-18,14)},
{$0632,64,(8,20),(-8,20)}, {$0832,43,(6,21),(18,14)}, {$0A32,22,(15,16),(22,4)}.

### Enemy missiles (LAB_069A/069B/069C, $2152E2/$2152EA/$2152F2) — the "big shell", budgeted by 206(A6)
* LAB_069C: `if (g206 < 0) return; spawn(LAB_069F) [aimed]; spawn(LAB_0619) [flash]`.
* LAB_069B: `spawn(LAB_06A0)` — missile in the parent's current angle358 (inherited), accelerating.
* LAB_069A: `spawn(LAB_069E)` — same but full speed immediately.
* Missile body LAB_06A1: `g206--; sfx LAB_040C(x); box mask = $8006 (passive: hurts both players, cannot be
  shot, does not stop bullets)`; speed $1000 → LAB_0515 → one step of (vx,vy) added to x,y (start ahead of
  the muzzle); events bit3+bit4 (touch either player) → LAB_053B (missile dies); margin 0; NO_SHADOW;
  gfx $3001 + dir16, two frames alternate every tick (280/282); z = y/2 (draw order trick);
  if accelerating (284): speed $80, +$80 every 5 ticks for 20 steps, then forever at that speed; else speed
  $500 forever; on signal: unlink box, g206++, free. LAB_069F aims: `(tx,ty)=alternate_player(); ty -= 8;
  turn_towards(tx,ty,0)`.

### Other projectiles are ordinary child handlers: see handlers spawning gfx $0C1A/$0C0F/$1831/$284C (mask 38),
flames $0601/$0013 (mask 6/4: hurt, not shootable). Player bullets are NOT objects: 30-entry tables
11288/11468(A6) (48 B: 0.w state, 2.l (dx<<16|dy), 6.. collision box (mask $8041/$8081), 14.l (x<<16|y),
26.. blit node, 36.w gfx) moved by LAB_04A6 in the priority -2 task LAB_04A4 that also runs the collision sweep.

--------------------------------------------------------------------------------------------------
## 6. Collision system (task LAB_04A4, runs once per tick after the objects have yielded)
* Box node = 20 bytes at 488(A5): x496, y498, hw500, hh502, mask504, hits506 — `LAB_053E(D0=mask)` /
  `LAB_053D` (mask as inline word after the BSR) registers it (hw=hh=8 until `set_frame`), `LAB_0541` unlinks.
  x,y are the integer pos at the END of the object's tick (LAB_053C); hw/hh come from the frame descriptor
  (`LAB_038D(gfx)` +16/+18) each time `LAB_0538/0539` runs.
* LAB_0545 keeps the list 11058(A6) sorted by x; LAB_0549 sweeps: for every box A with mask bit15 clear
  ("scanner"), every other box B whose CENTRE (x,y) lies inside A's rectangle [x±hw, y±hh]:
  `B.hits |= A.mask; A.hits |= B.mask`. Passive boxes (bit15) never scan but can be found.
* Events are therefore purely collision-derived; enemies' own `LAB_0720` mask bits 1/2 determine whether
  bits 3/4 are enabled on themselves. Observed in trace: $24 vs $8041 (heli bullet hits plane),
  $48 (jeep) vs $06 (hazard) → jeep's bit1 event, $48 vs $8081 (its own bullets, ignored), $22 vs $22.
* Player boxes: heli $0050 (bits 4,6), jeep $0048/$0088 (bits 3,6/7); the heli registers LAB_0507 (bit2 =
  hit a heli-killer → death), the jeep LAB_0506 (bit1).

--------------------------------------------------------------------------------------------------
## 7. Graphics / animation verbs
| LAB_0492 (inline word) / LAB_0493 (D0=gfx) | $211BFA/$211C00 | `gfx_acquire(set)`: refcount++ in 11566(A6)[set&$1FF]; if not yet loaded (ptr +4 == 0) yield until it is. |
| LAB_0497 (inline) / LAB_0498 (D0) | $211C1C/$211C22 | `gfx_release(set)`: refcount--. |
| LAB_0537 (inline word) / LAB_0538 (D0=gfx) | $212A96/$212A9C | `set_frame(gfx)`: 398 = gfx; box hw/hh = frame's +16/+18 (LAB_0539). |
| LAB_0528 (inline script) / LAB_0527 | $2129A8/$2129A2 | `anim_start(slotA / slotB, script)`: slot.loop0 = slot.rd6 = script; count=0; rate=3; countdown=1; user=0; active=-1. Execution resumes after the first `$8000` word of the script. |
| LAB_0680 (inline script) | $215102 | = LAB_0527 for slot B (overlay sprite 440/slot 442; 438 must be set non-zero by the handler to draw). |
| LAB_053E / LAB_053D / LAB_0541 | | collision box register / (inline mask) / unlink (see §6) |

Anim interpreter (LAB_052C/052D, called by step()): `if(!active) return 0; countdown -= g_76; if (countdown > 0) return 0;
countdown = rate; loop: w = *rd++; if (w >= 0) { frame18 = w; slot.rd6 = rd; return 1; } op = (w>>11)&15, arg = w&$7FF:`
| word | op | effect |
|---|---|---|
| $8000 | END | active = 0 (frame stays) — also the terminator LAB_0528 skips to |
| $8800 | END+SIGNAL | signal the object (its wait returns NE), active = 0 |
| $9000 | LOOP | `if (count==0) rd = loop0; /*forever*/ else if (--count != 0) rd = loop0; /*else fall through*/` |
| $9800+n | SETLOOP | `count = n; loop0 = rd` (loop point = the word after this one) |
| $A000+n / $A800+n | user = n / user += n (slot +14; game-specific) |
| $B000+n | RATE | `countdown = rate = n` (VBLs per frame) |
| $B800+n / $C000+n | flags17 |= n / &= ~n (render flags of the sprite = 397 for slot A) |
Frame words are gfx words `(frame<<9)|set`, e.g. `$0404 $0604 $9000 $8000` = 2-frame loop forever.

--------------------------------------------------------------------------------------------------
## 8. Misc verbs
| LAB_0629 | $21455C | **RNG**: `s = g11172; s2 = s<<1 (32-bit); if (!(carry==0 && s2!=0)) s2 ^= $1D872B41; s2 = rotl32(s2,16); g11172 = s2; return D0 = s2` (use the low word / `AND #n`). Verified 215/215 trace samples. CAVEAT: the INT6 sound interrupt (LAB_03AD) does `g11172.hiword += VHPOSR` on every interrupt, so the sequence is NOT reproducible from game logic alone (212 of 214 consecutive calls in the trace saw a perturbed state). A port can use the LFSR as is. Also read raw: `TST.W/AND.W 11172(A6)` by some handlers, `MOVE.W 11172(A6),358(A5)` (random heading). |
| LAB_0625 | $214542 | `threat_ok()`: Z clear if g156 (sum of live threat) <= 160, Z set if over budget (handler then usually dies or waits). |
| LAB_062B | $214572 | smart bomb: spawn LAB_062C (sfx LAB_03C9, g169 = -1, g11166 = $100 flash, 50 yields, g169 = 0). Every enemy's 534 callback (LAB_0729) fires while g169. |
| LAB_0817 | $217EDA | screen shake: g3530 -= 3, yield, += 3, yield. |
| LAB_07B4 / LAB_07B5 | $2173CE/$2173DA | boss enter/leave: g140++ & g166 bit3 set / g140--, when 0: 20 yields then clear bit3. |
| LAB_0836 | $218280 | goose wing flap: frame = $0E57 (or $1E57 if (rng&31)==0 or (z!=0 && toggling w278)) + dir8, then step(). |
| LAB_0670 | $214FC0 | player per-tick (invuln timers 106/108 → flicker, copy x,y,z to record 70/72/74, then step()). |
| LAB_0676 | $215026 | player: if not invulnerable spawn LAB_0630 (death ring) and signal self. |
| LAB_0581/0582/0587/058A | | see §4 |
| LAB_03F3 (D0=x) | $210E58 | SFX: two-tone "pickup"/alarm; LAB_03C1 big explosion chord; LAB_03C3 / LAB_03C5 explosion variants; LAB_03C9 smart bomb; LAB_03EB hit ping; LAB_03FE shot; LAB_040C missile launch. All take D0 = x for panning and are fire-and-forget. |
| LAB_04A2 / LAB_04A3 | | kernel -1446 / -1442 (free memory D0 bytes at A0) |
| LAB_0382 / LAB_0383 (A0=blit node) | $210534/$21053A | insert node into display list 3564(A6) (map tiles) / 208(A6) (sprites), sorted by key +8 (renderer internal). |

--------------------------------------------------------------------------------------------------
## 9. Script skeleton (what every behaviour handler looks like)
```c
void handler(void) {                      // A5 = freshly spawned object: x,y from map, w276 = type nibble
    LAB_0720(gfx, mask, margin, hp, score, threat);   // may block for many ticks until on screen
    o->flags367 |= SCREEN_LOCKED / NO_SHADOW; z = ...; anim_start(script); speed/angle; LAB_0515;
    for (;;) {                            // behaviour loop; every wait returns NE when killed
        if (LAB_04DE(n)) break;           // or LAB_04E4 / LAB_04E8 / LAB_06D5 ...
        aim/fire/turn...
    }
    LAB_0725();                           // cleanup + free (explosion was already spawned by LAB_0729)
}
```
Death path: player bullet box overlaps → 506 bit0 → LAB_0728 → hp-- → LAB_0729 (score, explosion child,
signal) → script's wait returns NE → `BRA LAB_0725`.
