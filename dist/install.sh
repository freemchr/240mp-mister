#!/bin/sh
# 240-MP: MiSTer Edition — on-MiSTer install.
#
# Uses the MiSTer.ini `main=` mechanism: when the 240MP core loads, stock
# Main_MiSTer hands off (exec) to our HPS binary, which owns the framebuffer
# and input for the duration and re-launches Main to return to the menu.
# This is the same approach Groovy_MiSTer uses.
#
# Adds (idempotently):
#   [240MP]
#   main=240mp/240mp
# to /media/fat/MiSTer.ini.  Run once after copying the 240mp/ folder.

INI=/media/fat/MiSTer.ini
BIN=/media/fat/240mp/240mp

if [ ! -x "$BIN" ]; then
    echo "error: $BIN missing or not executable" >&2
    exit 1
fi

# Remove any legacy launcher-daemon hook from the pre-main= approach.
STARTUP=/media/fat/linux/user-startup.sh
if [ -f "$STARTUP" ] && grep -q "240mp-launcherd" "$STARTUP"; then
    grep -v "240mp" "$STARTUP" > "$STARTUP.tmp" && mv "$STARTUP.tmp" "$STARTUP"
    echo "removed legacy launcherd hook from $STARTUP"
fi
killall 240mp-launcherd 2>/dev/null

if grep -qi "^\[240MP\]" "$INI" 2>/dev/null; then
    echo "[240MP] section already present in $INI"
else
    [ -f "$INI" ] || printf '[MiSTer]\n' > "$INI"
    printf '\n[240MP]\nmain=240mp/240mp\n' >> "$INI"
    echo "added [240MP] main= entry to $INI"
fi

echo "Install complete. Select the 240MP core from the MiSTer menu to launch."
