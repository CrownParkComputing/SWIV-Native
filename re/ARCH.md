# SWIV (Amiga) AMPROG.OBJ + Sales Curve Kernel — architecture notes (measured)

Files: `amprog.asm` (IRA listing of AMPROG.OBJ, ORG $20BD20, address in `;xxxxxx:` comments),
`kernel.asm` (kernel at $200000), `ghidra/swiv_decomp.c` (Ghidra C, weak on A6 thunks),
`trace/state_2500.bin` (3M-record register trace, 18 x u32 LE per record: pc,d0-d7,a0-a7,sr;
gameplay frames 2500-2700), `trace/fast_2700.bin` (RAM $200000-$2FFFFF at frame 2700),
`trace/m_XXXXX.bin` RAM dumps every 100 frames, `trace/f_XXXXX.ppm` screenshots.

## Registers / globals
- A6 = $2016DC always (kernel+game global base). `N(A6)` = global. Negative offsets = kernel.
- 202(A6) frame counter (incremented by game main loop). 3530(A6) scroll position (map y of
  screen, counts DOWN as the level scrolls; init $E860). 3586(A6) map record cursor y (init $E9C0).
  3542(A6) current scroll y used by objects (372(A5) caches it per object). 156(A6) sum of
  "threat" (370 of live enemies). 186(A6) -> level tile dictionary. -76(A6) frames elapsed
  since last update (kernel int3 handler). -72(A6) kernel tick (long). -66(A6) another tick.
- Kernel jump table via A6: -1418 → $200E2E `D0 = 12(A5) - *8(A5)` (obj time vs clock; flags);
  -1438 → $200E76 YIELD to next task; -1502 → $200CFE alloc object (D0=size, A0=result, Z on fail);
  -1450 → $200F32 start task on object A1 with PC A0, priority D0 (list -698(A6));
  -1494 → $200D0E; -1522 → $200A14; -1422 → $200E54 set clock ptr A0 (8(A5)=A0,12(A5)=*A0);
  -1410 → $200E4A self-clock; -1414 → $200E3A 12(A5)--; -1526/-1530/-1478/-1474/-1470/-1490
  see kernel.asm.
- Tasks: cooperative; 3 lists (-390, -698, -1006 (A6)). Each object holds its own stack just
  below offset 274 (initial stack: SR, PC). Kernel int3 (VBL) runs list -698 each frame.

## Object record (A5 = current object, size $222 = 546 bytes)
 8.l clock ptr, 12.w obj time, 14.l saved SP, 274.w priority,
 276.w type param (record low nibble), 308.l parent object (bit3 of 367 = attached),
 320.l x (16.16? used as .w at 320 = integer x), 324.l y (map y when ground), 328.l z (height),
 332/336/340.l vx,vy,vz, 344/348/352.l ax,ay,az (integrated in LAB_04E9 per elapsed frame),
 358.w direction/angle, 360.w hit points, 362.w (LAB_0720 D4), 364.w off-screen margin (-$40),
 367.b flags (bit0,1,2,3 attached,4 = ground-locked: y follows scroll), 368.w gfx word,
 370.w threat/score (LAB_0720 D5), 372.w cached scroll, 376.l handler (LAB_0634 default),
 397.b flags, 398.w, 400.. blit slot A (LAB_0383), 442.. slot B (shadow?), 464.. slot C,
 486.w countdown timer, 506.w event mask, 508.w event enable,
 510/514/518/522/526/530.l event handlers (bits 0,3,1,4,2,.. of 506&508; default LAB_04DC = die),
 534.l per-frame callback when 169(A6) set, 538.l per-frame callback (think), 542.l callback.

## Behaviour dispatch
 LAB_059E ($213182): table (gfx word, off) → handler = $2132C4 + 2*off. Full list in
 `handlers.txt`. The map interpreter (LAB_02EB $20F372) consumes records within 256 px; objects
 (type != 0) are spawned via LAB_02F2 ($20F41E): alloc, LAB_059F (gfx→handler), LAB_04CD
 (create task), obj.x=record x (9 bits), obj.y=record map y, obj.276=type nibble.
 Per-frame step every script calls: LAB_04E9 ($211FF2): integrate motion, timers, blit slots,
 callbacks, events (LAB_04FE). LAB_04E4(D0=n) = n frames; LAB_04E8 = until event; LAB_04DE =
 until tick; LAB_04D1/04CD = spawn child with handler A0 (A5 fields copied).
 LAB_0720(D0 gfx, D1 anim?, D2 collide?, D3 hp, D4 ?, D5 threat/score) = enemy init; LAB_0725 = die.
