# µOS++ III — Broadcom BCM283x devices

BCM2837 (Raspberry Pi 3 B, Zero 2 W) and BCM2711 (Pi 4 B) SoC support, and their SD and USB drivers. `drivers/aarch32/` and `drivers/aarch64/` are the two ports' copies, kept as they were (four files differ) until they are reconciled and re-proven on the boards.

It is an xPack: a CMake project of INTERFACE targets, consumed with xpm.
It reads no board; a board (in the consumer's test platforms) names the
targets it needs.

## Targets

- `micro-os-plus::soc-bcm2837`, `::soc-bcm2711` (headers; a board compiles the SoC's `src/`)
- `micro-os-plus::devices-sd-posix` (the SD card as a µOS++ POSIX I/O block device), `::devices-sdhost-posix` (the same over the SDHOST controller), `::devices-usb` (the DWC2 USB gadget)

## Origin

Moved, with its git history, from `soc/` and `drivers/` of micro-os-plus-iii-aarch64 and -aarch32.

## Status

Local only: a pre-release (`0.1.0-pre.1`) used through `xpm link` by the
tests of micro-os-plus-iii. Not published.
