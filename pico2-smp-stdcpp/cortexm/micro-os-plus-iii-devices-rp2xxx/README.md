# µOS++ III — Raspberry Pi RP2040 / RP2350 devices

The CMSIS device headers and SystemCoreClock of the RP2040 (Pico) and RP2350 (Pico 2), written from the datasheets; no Pico SDK.

It is an xPack: a CMake project of INTERFACE targets, consumed with xpm.
It reads no board; a board (in the consumer's test platforms) names the
targets it needs.

## Targets

- `micro-os-plus::soc-rp2040`, `::soc-rp2350`

## Origin

Moved, with its git history, from `soc/rp2040/` and `soc/rp2350/` of micro-os-plus-iii-cortexm.

## Status

Local only: a pre-release (`0.1.0-pre.1`) used through `xpm link` by the
tests of micro-os-plus-iii. Not published.
