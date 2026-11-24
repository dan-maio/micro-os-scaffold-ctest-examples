# µOS++ III — Storage drivers

The chip-independent storage drivers of µOS++ III: the SD card interface `sd.hpp`, which selects the controller back-end a device package provides (`SD_BACKEND_*`), and the card as a µOS++ POSIX I/O block device, for the `@xpacks/chan-fatfs` file system.

It is an xPack: a CMake project of INTERFACE targets, consumed with xpm.
It reads no board; a board (in the consumer's test platforms) names the
targets it needs.

## Targets

- `micro-os-plus::devices-storage-sd`: `sd.hpp`
- `micro-os-plus::devices-storage-posix`: the SD card as a µOS++ POSIX I/O block device (`sd-block-device.h`, `sd::block_device`), for the `@xpacks/chan-fatfs` POSIX I/O file system; the card driver comes from the consumer

## Origin

Moved, with its git history, from `drivers/` of micro-os-plus-iii-aarch32, -aarch64 and -posix-arch (identical in the three); history from -aarch64. The flatfs file system and the FatFs R0.15a copy with its disk glue were removed once every SD test used the posix-io block device and `@xpacks/chan-fatfs`, proven on hardware (2026-11-18).

## Status

Local only: a pre-release (`0.1.0-pre.1`) used through `xpm link` by the
tests of micro-os-plus-iii. Not published.
