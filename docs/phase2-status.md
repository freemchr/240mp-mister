# Phase 2 — UI framework (PARKED at the input-architecture wall)

## Status: parked 2026-07-17

The software UI framework is built and the app renders correctly to the
framebuffer. Phase 2 is paused on a genuine MiSTer architecture problem
(input ownership), pending a decision to build a Main_MiSTer fork.

## What works

- **Software renderer** (`app/src/ui/gfx.c` + `font8x8.h`): surface, rects,
  gradients, alpha, scanlines, embedded 8×8 ASCII font, aligned text.
- **Theme** (`app/src/ui/theme.h`): 240-MP's 7 color schemes, mirrored.
- **App** (`app/src/app.c`): home module rail (Jellyfin/Emby/Plex/Live TV/
  Settings), browse list (demo data), settings screen with a live theme
  switcher persisted to `config.json`. Header/footer chrome, scanline CRT feel.
- **Config** (`app/src/config.c`): load/save `/media/fat/240mp/config.json`
  ({"app":{…},"modules":{…}} shape).
- Cross-builds clean (`make -C app` → `build/240mp`).

Verified on hardware: the UI drew correctly (user confirmed "looks good") —
the render layer is done.

## The blocker: input ownership

MiSTer's Main (here **MiSTer_Zaparoo**, the user's global `main=`) holds an
**exclusive `EVIOCGRAB` on every input device whenever a core runs**, forwarding
input to the FPGA over SPI. An HPS app running *alongside* Main receives no
gamepad/keyboard events. Confirmed on-device: all `event*` report BUSY;
console-fb mode does **not** release the grab.

### `main=` handoff — attempted, insufficient alone

Set `[240MP] main=240mp/240mp` in `MiSTer.ini`. Our binary **does** exec and
run as the core's Main (PID confirmed, input readable via our own grab). But
replacing Main early skips Main's **video-output bring-up** (ADV7513 HDMI I2C
init, `video_set_mode` SPI, scaler routing) — so the screen is blank. A
from-scratch minimal binary can't easily reproduce all of Main's hardware init.

## Path forward (chosen approach: fork Main)

Build **from Main_MiSTer's real source** (as Groovy_MiSTer does):

1. Cross-build Main_MiSTer in CI — toolchain `arm-none-linux-gnueabihf` gcc
   10.2 (`setup_default_toolchain.sh`) + deps (Imlib2, freetype, libpng).
2. Patch: when the loaded core is `240MP`, run our UI (`gfx`/`app`) using
   Main's already-initialized framebuffer + input state, instead of the menu
   loop. All of today's UI code is reusable as the render layer.
3. Distribute the patched binary via `main=240mp/…` (per-core, overrides the
   global Zaparoo main just for our core).

This reuses Main's full video + input bring-up — the robust path. It's a
sizable subproject (several build iterations), hence parked pending scheduling.

## Clean-up done

- Removed `[240MP] main=` from the user's `MiSTer.ini`; rebooted → MiSTer
  usable as normal.
- Repo builds clean; `run.sh` / launcher artifacts from Phase 1 remain but are
  superseded by the fork approach.
