# Phase 1 — custom 240MP core + launcher

**Goal:** a `240MP.rbf` core, selectable from the MiSTer menu, that displays the
HPS app on the CRT with no dependency on Main_MiSTer's fb-terminal state.

## Result — PASS (2026-07-17)

Selecting **Console → 240MP** loads the core and the launcher auto-starts the app,
which draws a clean full-screen 640×480 test pattern with audio. Exit
(ESC/Q/guide) returns to the MiSTer menu.

## Architecture

- **Core** (`fpga/MP240.sv`): minimal `emu` derived from Menu_MiSTer. 15 kHz
  NTSC/PAL timing, core RGB held black, and **`MISTER_FB` enabled** so ascal
  scans out an HPS-written framebuffer. The app owns the picture.
- **Launcher** (`launcher/240mp-launcherd.c`): watches `/tmp/CORENAME`; on
  `240MP`, clears the framebuffer (uninitialized DDR) and runs
  `/media/fat/240mp/run.sh`; returns to the menu when the app exits. Installed
  into boot via `dist/install.sh` → `/media/fat/linux/user-startup.sh`.

## The framebuffer-address bug (the hard part)

Symptom evolved as observation sharpened: "VHS static" → "top border" → a hard
**top-40% corruption, bottom clean**. Root cause found via an on-device readback
test (write known values to `/dev/mem`, read back, count corrupted rows):

| Base | Result |
|---|---|
| `0x20000000` | rows 0–192 corrupted (ascal `RAMBASE` — scaler's 8 MB scratch) |
| `0x21000000` | rows 0–192 corrupted **identically** (aliases/overlaps the same scratch) |
| `0x22000000` | **clean** over a 15 s hold — Main's canonical HPS-fb base |

`0x20000000` and `0x21000000` corrupt identically, which is why relocating
between them changed nothing on screen. **`FB_BASE = 0x22000000`** (in the core,
`fbtest --devmem`, and the launcher) is the fix.

**Lesson:** don't place the core framebuffer in ascal's `RAMBASE` region
(`0x20000000` + 8 MB). Use `0x22000000`. Objective readback testing beat several
rounds of visual-symptom guessing.

## Two facts worth carrying forward

- `/dev/fb0` (Main's Linux console fb) is only *scanned out* when the fb-terminal
  is active — unusable for a custom core. Owning the framebuffer in fabric
  (`MISTER_FB` + `/dev/mem` writes) sidesteps Main entirely. (An earlier
  uinput Ctrl+Alt+F9 approach worked on the menu core but never on a custom core.)
- CI builds the rbf (Quartus 17.0 in `raetro/quartus` docker via `docker run`,
  not `container:` — the image glibc is too old to host GitHub's Node actions).
