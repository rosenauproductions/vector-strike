# USB Lab

A Batocera "port" that diagnoses USB trouble as a game. It was built after an OS update left USB unstable
and the Xbox wireless dongle not connecting pads.

What it shows (one screen, 1080p, vector style):
- **CLEAN timer**: how long USB has gone without a fault (a device dropping, a pad lost, a kernel USB error). Best time is saved.
- **USB devices** from `/sys/bus/usb`: port, speed, vendor:product, driver, and whether power-saving has put it to sleep (ASLEEP).
- **Xbox wireless dongle** (045e:02e6 / 02fe / 02f9 / 091e) checklist: on USB, `xone` driver attached, firmware `xow_dongle*.bin`, awake, Xbox pads connected. BACK+B / P turns on pairing.
- **Ports** per bus (USB2 and USB3 buses of each controller): green = in use, cyan = used earlier, red = kernel errors on that port.
- **Controllers** (SDL, raw): every button and axis. Press them all to get VERIFIED. LOST flashes when a pad drops.
- **Kernel USB log**, live from `/dev/kmsg`, sorted into: enumeration errors (-71/-110), resets, over-current, controller died, firmware, HID errors.
- **Diagnosis**: plain-language hints worked out from all of the above.

Buttons: BACK+A / M mark a problem, BACK+B / P pair the dongle, BACK+X / F turn USB power-saving off (until reboot, press twice),
BACK+Y / S take a snapshot, BACK+START (hold) / Esc / Hotkey+Start quit.

Reports go to `roms/ports/usblab/reports/` (readable from the share):
`usblab-<time>.txt` (events + summary + diagnosis), `-start.txt` / `-end.txt` / `-snapN.txt` (full system snapshot from `snapshot.sh`).
`snapshot.sh` also works on its own over SSH: `sh /userdata/roms/ports/usblab/snapshot.sh > /userdata/roms/ports/usblab/reports/manual.txt`.

## Build / test
`./build.sh` -> `build/usblab.x86_64` (same toolchain as Vector Strike).
Test switches: `USBLAB_HOME`, `USBLAB_WINDOW=WxH`, `USBLAB_SECS` (auto quit), `USBLAB_SHOT=x.bmp`, `USBLAB_DEMO=1` (fake kernel errors),
`USBLAB_SYSROOT=dir` (read a fake /sys tree, for testing without hardware), `USBLAB_MUTE=1`.

## Deploy
- `build/usblab.x86_64` + `snapshot.sh` -> `roms/ports/usblab/`
- `pkg/UsbLab.sh` + `pkg/UsbLab.sh.keys` -> `roms/ports/`
- Then in the Batocera menu: Game settings > Update game lists. Ports > UsbLab.
