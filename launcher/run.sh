#!/bin/sh
# /media/fat/240mp/run.sh — launched by 240mp-launcherd when the 240MP core
# loads.  Phase 1: run the hardware test pattern.  Later phases replace this
# with the real media app.
exec /media/fat/240mp/fbtest --devmem
