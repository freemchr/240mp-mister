# 240-MP: MiSTer Edition

A retro VHS-style media player that runs **natively on the MiSTer FPGA** as its own core.
Select **240-MP** from the MiSTer core menu, get a VHS-deck interface on your CRT, and
browse & play from Jellyfin, Emby, Plex, and IPTV (M3U + XMLTV EPG) — entirely on-device.
No PC, no Raspberry Pi, no casting.

Companion project to [240-MP](https://github.com/anthonycaccese/240-MP) (Qt6/QML, Raspberry Pi/macOS).
This is a from-scratch lightweight sibling sharing 240-MP's design language and server API
behavior — not a port (the MiSTer's ARM side has no GPU, so Qt/QML cannot run there).

## How it works

A MiSTer core is an FPGA bitstream (`.rbf`) plus software on the ARM (HPS) side — that's
true of every core. In 240-MP: MiSTer Edition:

- **FPGA side** (`fpga/`): a minimal core derived from the official
  [Template_MiSTer](https://github.com/MiSTer-devel/Template_MiSTer) framework with the
  `MISTER_FB` scaler framebuffer enabled — the same mechanism the MiSTer menu uses. The
  framework provides scanout, scaling, 15 kHz analog / composite / S-Video output, and the
  HPS→FPGA audio path (`alsa.sv`, 48 kHz stereo).
- **HPS side** (`app/`): the media app — software-rendered VHS UI into `/dev/fb0`,
  FFmpeg (software) decode, ALSA audio, evdev gamepad/keyboard input, libcurl server
  backends. Launched automatically by a small daemon (`launcher/`) when the core loads
  (watches `/tmp/CORENAME`); stock `Main_MiSTer` is never replaced.

## Playback constraints (physics, not laziness)

The MiSTer's HPS is a dual-core Cortex-A9 @ ~800 MHz with no hardware video decoder.
Decoding is pure software, so playback is **SD only** (≤480p, 24/30 fps):

- Jellyfin / Emby / Plex: the app requests **server-side transcode** to 480p H.264
  low-bitrate (or MPEG-2, which the A9 decodes with ease).
- IPTV: SD H.264 feeds may direct-play.
- On a CRT through a VHS-styled interface, SD is the aesthetic anyway.

HD/4K direct play, 60 fps video, and FPGA-side media decoding are out of scope.

## Repository layout

| Path | Contents |
|---|---|
| `fpga/` | Quartus 17.0 project for the `240MP.rbf` core (built in CI — Quartus has no macOS support) |
| `app/` | HPS media app (C++17, no Qt), cross-compiled for armv7 gnueabihf / glibc 2.31 |
| `launcher/` | `240mp-launcherd` + install hook (`/media/fat/linux/user-startup.sh`) |
| `dist/` | MiSTer Downloader `db.json` generator + install docs |
| `docs/` | Performance benchmarks, protocol/API notes |

## Status

Early development — Phase 0 (hardware interface validation). See `docs/` as it fills in.

## License

GPL-3.0 (see [LICENSE](LICENSE)). FPGA framework code under `fpga/sys/` is
GPL-2.0-or-later from MiSTer-devel and is included unmodified, as required by the
MiSTer project. No code is taken from GPL-2.0-only or unlicensed third-party senders.
