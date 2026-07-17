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

_(pending first hardware run)_
