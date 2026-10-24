#!/bin/bash
# Vector Strike - Batocera port launcher
DIR="$(dirname "$(readlink -f "$0")")/vectorstrike"
BIN="$DIR/vectorstrike.x86_64"
LOG=/userdata/system/logs/vectorstrike.log
mkdir -p /userdata/system/logs
cd "$DIR" || { echo "missing $DIR" > "$LOG"; exit 1; }
export SDL_JOYSTICK_HIDAPI_WII=0
{
  echo "=== Vector Strike launch $(date) ==="
  # copying over the network share (SMB) can drop the executable bit: restore it
  [ -x "$BIN" ] || chmod +x "$BIN" 2>&1
} > "$LOG" 2>&1
if [ -x "$BIN" ]; then
  VS_HOME="$DIR" "$BIN" "$@" >> "$LOG" 2>&1
else
  VS_HOME="$DIR" /lib64/ld-linux-x86-64.so.2 "$BIN" "$@" >> "$LOG" 2>&1
fi
rc=$?
echo "=== exit code $rc ===" >> "$LOG"
exit $rc
