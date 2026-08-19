# itch.io page — SWIV

## Title
SWIV — Native (Amiga)

## Tagline (short description, 140 chars max)
Storm's 1991 Amiga shooter rebuilt as a native game: heli and jeep co-op, no emulator, no disk to mount. Linux + Android.

## Classification
Game · Action / Shoot 'em up

## Release status
In development

## Pricing
No payments (free download)

---

## Description (paste into the page body)

**S.W.I.V.** (Storm / The Sales Curve, 1991) — the vertical co-op shooter with the helicopter and the jeep — running as a **native program**, not under an emulator.

Every part of the game was read out of the original and rebuilt: all 73 enemy behaviour scripts, the helicopter and the jeep (with the jeep's terrain collision, jumps and turret), the scrolling maps with their palette fades, the power-up tokens, the title/attract sequence, hi-score tables and the game's own font, and the sound driver's 29 effects synthesised from their Paula parameters and checked against the real output. The title tune plays from the original module.

### What this is
- A faithful re-implementation in C on raylib: the original's object scripts translated one by one, parity-checked frame by frame against the original's object log.
- No 68000 core, no chipset emulation, no disk image to mount — download, unzip, run.
- Linux x86-64 and Android (arm64, controllers and touch).

### Features
- Two players: **port 2 helicopter** and **port 1 jeep**, either starts the game, the other joins any time
- Controllers (hot-plugged) or keyboard; on Android: built-in or external pad, touch steering, every menu reachable from the pad
- Difficulty: EASY / NORMAL / HARD (hard: 1.5× aerial and 2× moving ground enemies)
- Hi-score table records shots fired, accuracy and difficulty alongside the score
- Options: sound / music volume, fullscreen (centred, letterboxed), title music in game
- Extras: the original box and disk scans, adverts and cheats (Hall of Light)
- SFX tuning screen: pick and adjust any of the original sound routines per game trigger

### Controls
| Action | Helicopter (port 2) | Jeep (port 1) | Controller |
| --- | --- | --- | --- |
| Move | arrows | WASD | stick / d-pad |
| Fire | Space or Enter | Left Shift / Left Ctrl | A, X or trigger |
| Start | fire | fire | A / Start |
| Pause | P | — | Start |
| Back to menu (paused) | Esc | — | Select |
| Touch (Android) | right half = fire | left half = steer | — |

### Install
Linux: unzip and run `swivview` from its own folder (needs OpenGL/X11). Android: install the APK.

### Source
github.com/CrownParkComputing/SWIV-Native — the engine, the behaviour ports, the front end and the tools. Built with the same method as Battle Squadron — Native and Hybris.

---

## Screenshots to upload (itch/screens/)
attract_150.png (title), attract_1000.png (publisher/credits text), attract_2200.png (credits), play_1500.png, play_2600.png

## Legal note for the page
SWIV is © 1991 The Sales Curve / Storm. This is a preservation and porting project; no game data is included in the source repository.
