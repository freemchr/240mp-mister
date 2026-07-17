#!/bin/sh
# 240-MP: MiSTer Edition — on-MiSTer install script.
# Installs the launcher daemon into the boot sequence via the documented
# /media/fat/linux/user-startup.sh mechanism (run by init at every boot).
# Idempotent: safe to run repeatedly.

STARTUP=/media/fat/linux/user-startup.sh
MARK="# 240mp-launcherd"

if [ ! -x /media/fat/240mp/240mp-launcherd ]; then
    echo "error: /media/fat/240mp/240mp-launcherd missing" >&2
    exit 1
fi

if ! grep -q "$MARK" "$STARTUP" 2>/dev/null; then
    [ -f "$STARTUP" ] || printf '#!/bin/sh\n' > "$STARTUP"
    {
        echo "$MARK"
        echo "/media/fat/240mp/240mp-launcherd >> /media/fat/240mp/launcherd.log 2>&1 &"
    } >> "$STARTUP"
    chmod +x "$STARTUP"
    echo "installed launcherd into $STARTUP"
else
    echo "launcherd already installed in $STARTUP"
fi

# (re)start it now without requiring a reboot
killall 240mp-launcherd 2>/dev/null
/media/fat/240mp/240mp-launcherd >> /media/fat/240mp/launcherd.log 2>&1 &
echo "launcherd running (log: /media/fat/240mp/launcherd.log)"
