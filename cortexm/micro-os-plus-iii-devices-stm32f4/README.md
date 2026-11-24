# µOS++ III — STMicroelectronics STM32F4 devices

The STM32F411xE and STM32F412Rx device headers (with the Arm CMSIS core headers they use), SystemInit and the interrupt vectors.

It is an xPack: a CMake project of INTERFACE targets, consumed with xpm.
It reads no board; a board (in the consumer's test platforms) names the
targets it needs.

## Targets

- `micro-os-plus::soc-stm32f411xe`, `::soc-stm32f412rx`

## Origin

Moved, with its git history, from `soc/stm32f4xx/` of micro-os-plus-iii-cortexm.

## Status

Local only: a pre-release (`0.1.0-pre.1`) used through `xpm link` by the
tests of micro-os-plus-iii. Not published.
