# SWIV object record (A5) — field map

All offsets are decimal bytes from A5 (the current task/object). Object size = $222 = 546 bytes,
allocated by kernel `-1502(A6)` and initialised by `LAB_04D2` (spawn). `.l` = 32-bit, `.w` = 16-bit,
`.b` = byte. Values marked **(V)** were verified against `trace/fast_2700.bin` / `trace/chip_2700.bin`
(objects live in chip RAM at $4F000-$60000 and in fast RAM at $2196D0.., all linked from list -698(A6)).

NOTE FOR TRACE USERS: the program image that was running when the trace was taken is NOT byte-identical
to `amprog.asm`. It has 15 small insertions, so listing addresses map to runtime addresses with a growing
shift: +0 below $20F2E8, +10 from $20F2E8, +16 from $20F308, +18 from $212E98, +24 from $213990,
+38 from $214208, +36 from $214720, +40 from $215940, +42 from $216048, +40 from $2164B0, +44 from
$217410, +48 from $2175A0, +50 from $217E80, +62 from $217F40, +60 from $2192A8
(e.g. LAB_04E9 $211FF2 runs at $212002, LAB_0720 $215FE6 at $21600E, LAB_0634 $21466A at $214690).
Trace records hold the registers BEFORE the instruction at `pc` executes.

## Kernel header (0..275)
| off | type | meaning |
|---|---|---|
| 0 | .l | next object in task list (list head = -698(A6) for game objects) |
| 4 | .l | prev |
| 8 | .l | clock pointer. `&204(A6)` for map-spawned enemies and their children, `&202(A6)` for the players **(V)**. `-1418(A6)` returns `12(A5) - *8(A5)`: non-zero = "this object has been signalled/killed". |
| 12 | .w | object time. Set = `*8(A5)` at spawn. `-1414(A6)` (= `LAB_053B`) decrements it = SIGNAL self; the game also bumps 202/204(A6) to kill whole generations (level restart). **(V)** `12 == 202(A6) == 2` for live players. |
| 14 | .l | saved SP while not running |
| 16..273 | | the task's own stack (grows down from 274) |
| 274 | .w | priority (100 = normal enemy, 99 = player, 101 = death effects, -2 = collision/bullet task) **(V)** |

## Script parameters / scratch (276..307)
| off | type | meaning |
|---|---|---|
| 276 | .w | type param. Map spawner puts the record's low nibble here (0..15). Handlers use 276..291 (8 words) freely as locals; they are COPIED to children by `LAB_04D1`. For player objects 276.l = pointer to the player record (11176/11356(A6)) **(V)**. |
| 278..291 | .w×7 | handler locals (copied to children) |
| 292..307 | | unused / handler locals (not copied) |

## Hierarchy
| 308 | .l | parent object (0 = none). Set by `LAB_04BA` (attached spawn `LAB_04C9/04CA`). Cleared when the parent is freed (`LAB_04C3`) → child's 542 callback fires. **(V)** rotor 5B280.308 = heli 51F10 |
| 312 | .l | first child |
| 316 | .l | next sibling |

## Motion (all 16.16 fixed point, px units) **(V)**
| 320 | .l | x (screen px, 0..319 playfield; `320(A5).w` = integer x) |
| 324 | .l | y in MAP units (same units as scroll). Screen y = `y - 3542(A6)`; visible when `0 <= y-3542 < 256`. Smaller y = further ahead (the level scrolls towards y=0; 3530/3542 count DOWN). |
| 328 | .l | z = height above ground (px). Shadow is drawn at (x + z/2, y + z). If z goes negative after integration, z/vz/az are zeroed (landed). |
| 332/336/340 | .l | vx, vy, vz (px/VBL, 16.16) |
| 344/348/352 | .l | ax, ay, az (added to v each VBL) |
| 356 | .w | speed (8.8: 256 = 1 px/VBL). `LAB_0515` converts speed+angle → vx,vy. **(V)** 612 & angle 40 → vx = 612*142 = $15378 |
| 358 | .w | angle/direction, 256 units per turn; 0 = +x (right), 64 = +y (down the screen), 128 = left, 192 = up. Only low byte is meaningful (`LAB_0515` masks with $FF). **(V)** |
| 356.l | | copied as a unit to children |

## Combat
| 360 | .w | hit points (LAB_0720 D3). 0 = invulnerable (no bullet event enabled). |
| 362 | .w | score awarded on death (LAB_0720 D4). **(V)** trace: score add D0 = 12 (FODDERA), 50 (MEDTANK) |
| 364 | .w | off-screen margin for the 538 callback, default -64 (= kill when 64 px outside the 320×256 screen) **(V)** |
| 366 | .b | unused |
| 367 | .b | flags: bit0 NO_SHADOW (slot C not drawn); bit1 HIT_FLASH (set by LAB_0726 when damaged, cleared every tick after use; makes the sprite render with flag $10); bit2 FLASH_WITH_PARENT; bit3 ATTACHED (pos = parent pos + own velocity, each tick); bit4 SCREEN_LOCKED (y is adjusted by the scroll delta every tick so the object keeps its SCREEN position — used by the players, air enemies, bullets; ground objects leave it clear and stay fixed on the map). **(V)** heli/yellow planes have $10, fodder 0. |
| 368 | .w | graphic-set word used for the gfx refcount (LAB_0720 D0) |
| 370 | .w | threat value (LAB_0720 D5), summed into 156(A6) while alive |
| 372 | .w | scroll (3542) cached before the yield; used for bit4 |
| 374 | .w | bonus-popup gfx word shown on death (0 = none; e.g. MINE sets $1011) |
| 376 | .l | death-effect handler spawned by LAB_0729 (default LAB_0634 = standard explosion) **(V)** |

## Animation slot A (380..399) — drives the main sprite
Struct (20 bytes) at 380, also used at 422 for slot B:
| +0 | .l | loop point (init = script start; $9800 moves it) |
| +4 | .w | loop counter |
| +6 | .l | read pointer (next word to fetch) |
| +10 | .w | countdown (VBLs) to next frame |
| +12 | .w | frame rate (VBLs per frame, default 3) |
| +14 | .w | user value (set by $A000/$A800 ops) |
| +16 | .b | active (-1 while playing) |
| +17 | .b | flags (render flags byte; ops $B800/$C000 set/clear bits) |
| +18 | .w | current frame = gfx word |
So: 396.b anim-A active, **397.b = sprite render flags** (bit0 commonly set by handlers; bit6 used by popups; bit4 = hit flash is ORed in per frame), **398.w = current gfx word of the main sprite**.
Slot B: 422..441 → 438.b = slot-B active/visible, 439.b = slot-B flags, 440.w = slot-B gfx word.

## Blit slots (22 bytes each) — filled by LAB_04E9 every tick, inserted in display list 208(A6)
| 400 | slot A (main sprite): 408.w sort key = z ^ $7FFF, 410.w gfx (=398), 412/414 x,y, 416.w, 418.w (renderer params, init 0/$100), 421.b flags (=397, |$10 if flashing, &~1 if 155(A6)==0) |
| 442 | slot B (overlay, e.g. turret): 450 key = keyA-1, 452 gfx (=440), 454/456 x,y, 463 flags (=439 | $20); drawn only if 438 != 0 |
| 464 | slot C (shadow): 472 key = -1, 474 gfx (=398), 476/478 = (x + z/2, y + z), 485 flags = (397 | $21) & ~$10; drawn only if z != 0 and !(367 bit0) |

## Timers / collision / events
| 486 | .w | countdown timer, decremented by elapsed VBLs each tick (saturates at 0). Free for handlers. |
| 488 | | collision box node (20 bytes): 488 next, 492 prev, **496.w x, 498.w y** (= 320/324 words, refreshed by LAB_053C at the end of LAB_04E9), **500.w half-width, 502.w half-height** (from the frame descriptor of 398 via LAB_0539; default 8), **504.w collision mask/identity** (LAB_0720 D1 / LAB_053D word), **506.w collision hits = EVENT WORD**, linked in list 11078(A6) by LAB_053E. **(V)** |
| 506 | .w | events pending: the OR of the masks of every box this object overlapped this tick (see VERBS.md §Collision). Cleared by LAB_04FE after dispatch. |
| 508 | .w | event enable mask (bits 0..5). |
| 510 | .l | handler for bit0 = hit by a PLAYER BULLET (default LAB_04DC; LAB_0720 installs LAB_0728) |
| 514 | .l | bit3 = touched by the JEEP |
| 518 | .l | bit1 = touched something that KILLS THE JEEP (used by the jeep itself) |
| 522 | .l | bit4 = touched by the HELICOPTER |
| 526 | .l | bit2 = touched something that KILLS THE HELI (used by the heli itself) |
| 530 | .l | bit5 ("solid") — never enabled by the game |
| 534 | .l | callback run every tick while 169(A6) (smart bomb) is set; LAB_0720 installs LAB_0729 (die). `ST 534(A5)` = immune. Negative = none. |
| 538 | .l | OFF-SCREEN callback (run when outside screen ± 364 margin); default LAB_053B = signal self (kill). `ST 538(A5)` disables, `SF` re-enables. |
| 542 | .l | ORPHAN/think callback: run every tick while 308 == 0 (no parent); default -1 (none); attached children get LAB_053B → they die when the parent goes. |

## Globals (A6 = $2016DC) used by the verbs
| -76(A6).w | VBLs elapsed since the previous game tick (2 during play = 25 Hz logic) **(V)** |
| -72(A6).l | VBL counter; -68(A6).l / -66(A6).w | game tick counter (LAB_04DE/049D use it) |
| 155(A6).b | render flag gate; 156(A6).w | sum of live threat; 160.b game over; 161/162 | pixel-collision scratch |
| 165(A6).b | paused (LAB_0499 keeps yielding); 168.b | player alternation toggle; 169.b | smart bomb active |
| 182(A6).w | difficulty/level step (homing bullet persistence); 202/204(A6).w | generation clocks |
| 206(A6).w | enemy missile budget (neg = disabled); 3530(A6) | true scroll y; 3542(A6) | scroll y used by objects/render; 3586 | map cursor |
| 11058(A6) | collision list head; 11078 | "boxes" sentinel; 11172(A6).l | RNG state; 11176(A6) | HELI player record; 11356(A6) | JEEP player record |
| 11288/11468(A6) | player-bullet tables (30 × 48 bytes); 11566(A6) | gfx-set table (8 bytes/set: .b refcount, +4 .l loaded ptr) |

## Player record (180 bytes; HELI at 11176(A6), JEEP at 11356(A6)) **(V)**
| 8 | name "Lazy Heli"/"Lazy Jeep" | 54.b alive | 56.w player no (heli 1, jeep 0) | 58.w vehicle (0 heli, 1 jeep) | 60.l other record |
| 64.w joystick bits (0 up,1 down,2 left,3 right,5 fire) | 68.w | 70/72/74.w x, y, z of the player object (copied every tick by LAB_0670) | 76.l score |
| 96 fire cooldown | 98,100,102 weapon params | 106.w invulnerability timer | 108.w flicker timer | 276.l player object |

## Graphic words
gfx word = `(frame << 9) | set` (set = bits 0-8, frame = bits 9-15; e.g. $0E05 = set 5 frame 7). `LAB_038D(D0)` → frame
descriptor: +16/+18 = collision half-width/height. `handlers.txt` names are "SET.LIN#frame".
