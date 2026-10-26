#!/bin/bash
# USB Lab - Batocera port launcher (diagnoses USB / controller / Xbox dongle trouble)
DIR="$(dirname "$(readlink -f "$0")")/usblab"
BIN="$DIR/usblab.x86_64"
mkdir -p "$DIR/reports"
LOG="$DIR/reports/launch.log"
cd "$DIR" || exit 1
{
  echo "=== USB Lab launch $(date) ==="
  [ -x "$BIN" ] || chmod +x "$BIN" 2>&1   # SMB copies can drop the executable bit
} > "$LOG" 2>&1
if [ -x "$BIN" ]; then
  USBLAB_HOME="$DIR" "$BIN" "$@" >> "$LOG" 2>&1
else
  USBLAB_HOME="$DIR" /lib64/ld-linux-x86-64.so.2 "$BIN" "$@" >> "$LOG" 2>&1
fi
rc=$?
echo "=== exit code $rc ===" >> "$LOG"
exit $rc
