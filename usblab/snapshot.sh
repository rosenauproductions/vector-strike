#!/bin/sh
# USB Lab snapshot: everything useful for a USB problem, as plain text on stdout.
# USB Lab runs this at start and at exit. Over SSH: sh snapshot.sh > /userdata/roms/ports/usblab/reports/manual.txt
# Read-only: it changes nothing on the system.

sec() { echo; echo "===== $1 ====="; }
run() { echo "\$ $*"; sh -c "$*" 2>&1 || true; }

sec "WHEN"
date; uptime 2>/dev/null

sec "VERSION"
run "cat /usr/share/batocera/batocera.version"
run "batocera-version"
run "uname -a"
run "cat /proc/cmdline"

sec "HARDWARE"
run "cat /sys/class/dmi/id/sys_vendor /sys/class/dmi/id/product_name /sys/class/dmi/id/board_name /sys/class/dmi/id/bios_version /sys/class/dmi/id/bios_date"
run "lspci -nnk 2>/dev/null | grep -iA3 'usb\\|xhci\\|ehci'"
for c in /sys/bus/pci/devices/*; do
  [ "$(cat $c/class 2>/dev/null | cut -c1-6)" = "0x0c03" ] || continue
  echo "usb controller $(basename $c) vendor $(cat $c/vendor) device $(cat $c/device) driver $(basename "$(readlink $c/driver)" 2>/dev/null) power $(cat $c/power/control 2>/dev/null)"
done

sec "USB POWER SETTINGS"
run "cat /sys/module/usbcore/parameters/autosuspend"
run "cat /sys/module/usbcore/parameters/quirks"
run "cat /sys/module/usbhid/parameters/quirks"

sec "XBOX WIRELESS DONGLE (xone)"
run "lsusb 2>/dev/null | grep -i 045e"
for d in /sys/bus/usb/devices/*; do
  [ "$(cat $d/idVendor 2>/dev/null)" = "045e" ] || continue
  echo "$(basename $d): 045e:$(cat $d/idProduct) $(cat $d/product 2>/dev/null) speed $(cat $d/speed)M power $(cat $d/power/control)/$(cat $d/power/runtime_status 2>/dev/null)"
  for i in $d/$(basename $d):*; do echo "  $(basename $i) driver=$(basename "$(readlink $i/driver)" 2>/dev/null) pairing=$(cat $i/pairing 2>/dev/null)"; done
done
run "ls -d /sys/module/xone* /sys/module/xpad* /sys/bus/gip 2>/dev/null"
run "ls /sys/bus/gip/devices 2>/dev/null"
run "ls -l /lib/firmware/xow_dongle* /usr/lib/firmware/xow_dongle* 2>/dev/null"
run "modinfo xone_dongle 2>/dev/null | head -8"
run "find /lib/modules/$(uname -r) -name 'xone*' 2>/dev/null"
run "grep -i -E 'xone|xpad|xbox' /userdata/system/batocera.conf"
run "ls /etc/modprobe.d /userdata/system/modprobe.d 2>/dev/null; cat /etc/modprobe.d/*.conf 2>/dev/null | grep -i -E 'xone|xpad|mt76|blacklist'"
dmesg 2>/dev/null | grep -iE 'xone|gip|xow|045e|mt76|xpad' | tail -80

sec "LSUSB"
run "lsusb"
run "lsusb -t"

sec "USB DEVICES (sysfs)"
for d in /sys/bus/usb/devices/*; do
  n=$(basename $d)
  case "$n" in *:*) continue;; esac
  printf '%-10s %s:%s speed=%sM power=%s runtime=%s maxpower=%s driver=%s "%s %s"\n' "$n" \
    "$(cat $d/idVendor 2>/dev/null)" "$(cat $d/idProduct 2>/dev/null)" "$(cat $d/speed 2>/dev/null)" \
    "$(cat $d/power/control 2>/dev/null)" "$(cat $d/power/runtime_status 2>/dev/null)" "$(cat $d/bMaxPower 2>/dev/null)" \
    "$(for i in $d/$n:*; do basename "$(readlink $i/driver)" 2>/dev/null; done | sort -u | tr '\n' ' ')" \
    "$(cat $d/manufacturer 2>/dev/null)" "$(cat $d/product 2>/dev/null)"
done

sec "INPUT DEVICES"
run "cat /proc/bus/input/devices"

sec "LOADED USB / INPUT MODULES"
run "lsmod 2>/dev/null | grep -iE 'usb|hid|xpad|xone|xhci|ehci|ohci|btusb|joy|nintendo|sony|playstation|8bitdo'"

sec "KERNEL LOG (usb / hid / input lines)"
dmesg 2>/dev/null | grep -iE 'usb|xhci|ehci|ohci|hid|input:|xpad|xone|over-current|firmware|descriptor|enumerate' | tail -400

sec "KERNEL LOG (errors and warnings, all)"
dmesg -l err,warn 2>/dev/null | tail -150

sec "BATOCERA CONTROLLER CONFIG"
run "grep -iE 'controllers|bluetooth|global.hidapi|sdl' /userdata/system/batocera.conf"
run "ls /userdata/system/logs"
run "tail -40 /userdata/system/logs/es_launch_stdout.log"
echo
echo "===== END ====="
