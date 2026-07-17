# 240-MP: MiSTer Edition — HPS app build
#
# Local dev (macOS/Linux): uses `zig cc` as the cross compiler — it bundles
# Linux kernel headers and glibc stubs for the target, so no toolchain
# download is needed.  CI uses Debian Bullseye's gcc-arm-linux-gnueabihf
# (glibc 2.31, matching the MiSTer image) — see .github/workflows/build.yml.
#
# Target: armv7-A (Cortex-A9, NEON, hard-float) — MiSTer DE10-Nano HPS.

TARGET_TRIPLE := arm-linux-gnueabihf
# Cortex-A9 + NEON; MiSTer userspace is armhf.  zig uses musl for the static
# build (glibc can't be linked statically by zig) — a static musl binary is
# fully self-contained and runs fine on the glibc-based MiSTer image.
ZIG_TARGET    := arm-linux-musleabihf
MCPU          := cortex_a9+neon

CC_ZIG  := zig cc -target $(ZIG_TARGET) -mcpu=$(MCPU)
CC_GNU  := $(TARGET_TRIPLE)-gcc -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard

# Pick GNU cross gcc if present (CI), else zig (local dev)
CC_CROSS := $(shell command -v $(TARGET_TRIPLE)-gcc >/dev/null 2>&1 && echo "$(CC_GNU)" || echo "$(CC_ZIG)")

CFLAGS  := -O2 -std=c99 -Wall -Wextra
LDFLAGS := -static -lm -lpthread

BUILD := build

.PHONY: all clean
all: $(BUILD)/fbtest $(BUILD)/240mp-launcherd

$(BUILD)/fbtest: src/mister/fbtest.c | $(BUILD)
	$(CC_CROSS) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@file $@ 2>/dev/null || true

$(BUILD)/240mp-launcherd: ../launcher/240mp-launcherd.c | $(BUILD)
	$(CC_CROSS) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@file $@ 2>/dev/null || true

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
