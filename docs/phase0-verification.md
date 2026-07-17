# Phase 0 — hardware-interface verification (fbtest)

`fbtest` validates every MiSTer HPS interface the media app depends on, in one binary.

## Deploy & run

```bash
# from the repo root (MiSTer default credentials: root / 1)
scp app/build/fbtest root@<mister-ip>:/media/fat/
ssh root@<mister-ip>

# on the MiSTer — run while the MENU core is active
/media/fat/fbtest              # 640x480 XRGB8888, rb=1 (console-fb defaults)
```

Exit with `ESC` / `Q` on a keyboard, `BTN_MODE` (guide button) on a pad, or Ctrl-C over SSH.

## Acceptance checklist

| Check | Pass looks like |
|---|---|
| Framebuffer path | SMPTE bars + ramp fill the CRT (via ascal scaler) |
| Channel order | Swatch labeled **R** is red, **G** green, **B** blue. If R/B are swapped, rerun with `--rb 0` and note it |
| VSync pacing | Bouncing orange box moves smoothly, no tearing; status line shows `VSYNC OK` |
| Overscan | White 2px frame at extreme edge; yellow ticks at 8/16/24 px insets show how much the CRT crops |
| Audio path | Steady 440 Hz tone (concert A). Wrong pitch = sample-rate mismatch — record actual pitch |
| Input | Key/button presses print `input: devN key/btn code X down` over SSH |

## Variations to try

```bash
/media/fat/fbtest 320x240        # low-res mode the UI may use
/media/fat/fbtest --rb 0         # if colors were swapped
/media/fat/fbtest --no-audio
```

Record results (incl. `fb0:` info line and `MiSTer_fb mode param` output) in this file
before starting Phase 1.

## Results

**2026-07-17 — PASS** (MiSTer "MiSTerCRT", image 250402, kernel 5.15.1-MiSTer, menu core)

| Check | Result |
|---|---|
| Framebuffer path | ✅ Bars/ramp/pattern rendered on CRT |
| `fb_cmd1` resize | ✅ 640×480 applied (`mode: 8888 1 640 480 2560`) — **but only when the console fb is active** (see finding below) |
| Channel order | ✅ Bar order white→yellow→cyan→green→magenta→red→blue confirmed correct with `--rb 1` |
| VSync pacing | ✅ Smooth animation, `VSYNC OK` (FBIO_WAITFORVSYNC works) |
| Audio path | ✅ Steady test tone via /dev/MrAudio |
| Input | ✅ evdev events received ("input: dev5 … down"), exit key worked |

### Key finding — console-buffer scanout

Writing to `/dev/fb0` is only **visible** when Main_MiSTer is scanning out the Linux
console buffer (fb buffer 0). On the idle menu core, Main displays the wallpaper
buffers instead, so a background-launched app draws to invisible memory. Running via
the **Scripts menu** activates the console buffer and everything shows.

**Implication for Phase 1**: the launcher must ensure console scanout when our core
loads (the Scripts path proves Main can do it; find the equivalent trigger — e.g. the
script-launch mechanism or fb console enable via Main — rather than assuming fb0 is
always visible).
