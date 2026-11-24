# µOS++ III — Rockchip RK3506 devices

The RK3506 (Luckfox Lyra B) SoC headers and the DesignWare SD host (DW-MMC).

It is an xPack: a CMake project of INTERFACE targets, consumed with xpm.
It reads no board; a board (in the consumer's test platforms) names the
targets it needs.

## Targets

- `micro-os-plus::devices-rk3506-sd-posix` (the SD host over DW-MMC, with `SD_BACKEND_DWMMC`, as a µOS++ POSIX I/O block device)

## Origin

Moved, with its git history, from `soc/rk3506/` of micro-os-plus-iii-aarch32.

## Status

Local only: a pre-release (`0.1.0-pre.1`) used through `xpm link` by the
tests of micro-os-plus-iii. Not published.
