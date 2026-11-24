# SD Card (SDMMC) support — Luckfox Lyra B (RK3506)

Bare-metal SD-card driver for the RK3506, carried from the predecessor project
(where it served a StateOS port) into this device package.

## Hardware

The RK3506 SDMMC host at **`0xFF480000`** is a **Synopsys DesignWare Mobile
Storage Host Controller ("dw_mmc")** — *not* an SDHCI controller. That matters:
the register map below is the DesignWare one (`CTRL`, `CMD`, `RINTSTS`, `FIFO`),
distilled from U-Boot `drivers/mmc/dw_mmc.c`, **not** `sdhci.c`.

| Block            | Base         | Used for                                    |
|------------------|--------------|---------------------------------------------|
| SDMMC (dw_mmc)   | `0xFF480000` | Command/response, FIFO PIO data             |
| CRU              | `0xFF9A0000` | `GATE_CON17`, `CLKSEL_CON49`, `SOFTRST_CON17` |
| GPIO3 IOC        | `0xFF4D8000` | IOMUX for SDMMC pins                         |

Pins: `GPIO3_A0..A5` → func1 = `SDMMC_CLK, CMD, D0, D1, D2, D3`.

## Files

In this package (`micro-os-plus-iii-devices-rk3506`, `soc/rk3506/`):

| File | Purpose |
|------|---------|
| `include/sdmmc.hpp`, `src/sdmmc.cpp` | register map, SD command set, the `sdcard::SdMmc` driver: DW-MMC host, SD identification, PIO block read/write |
| `include/sd_dwmmc.hpp`, `src/sd_dwmmc.cpp` | the `sd::SdCard` back-end over it (`SD_BACKEND_DWMMC`), which `micro-os-plus::devices-rk3506-sd-posix` presents as the µOS++ POSIX I/O block device `/dev/sd` |

The file system is no longer here: the tests mount the card with the xPack
Chan FatFs through µOS++ POSIX I/O (`micro-os-plus-iii-devices-storage`).
The predecessor's `fatfs/` (FatFs R0.15a), `fatfs_cpp/`, `tests/` and
`reference/uboot-mmc/` folders, and the `sdcard_demo.cpp` self-test, were not
carried.

## API

```cpp
#include <sdmmc.hpp>

sdcard::SdMmc sd;
if (sd.init()) {                       // clocks + pinmux + SD ID sequence
    alignas(4) uint8_t sect[512];
    sd.read_block(0, sect);            // read one 512-byte LBA
    sd.read_blocks(100, buf, 8);       // read 8 consecutive sectors
    sd.write_block(200, sect);         // write one sector
    uint64_t bytes = sd.capacity_bytes();
}
```

`init()` runs the standard SD bring-up: `CMD0 → CMD8 → ACMD41 (loop) → CMD2 →
CMD3 → CMD9 (CSD/capacity) → CMD7 → ACMD6 (4-bit) → CMD16`, then steps the card
clock from 400 kHz up to 25 MHz. Transfers are **polled PIO** through the
`DWMCI_DATA` FIFO — no IDMAC descriptors, no interrupts, no MMU changes — so it
runs from any of the three Cortex-A7 cores.

## The one knob you must tune: `kBusHz`

`sdmmc.hpp` assumes the `cclk_src_sdmmc` source is **100 MHz** (`kBusHz`). The
DesignWare card clock is `kBusHz / (2 × CLKDIV)`. If `init()` fails or reads are
garbage, this is almost always wrong — measure/derive the actual PLL rate that
`CRU_CLKSEL_CON49` selects and update `kBusHz`.

## Wiring into a build

Link `micro-os-plus::devices-rk3506-sd-posix` (CMake, from this package). In
the kernel's tests the Lyra platform does it for every storage test
(`UOS_BOARD_SD_POSIX` in `tests/platforms/aarch32-luckfox-lyra/cmake/board.cmake`):
the card is then `/dev/sd`, mounted at `/sd/` before the test starts.

## Provenance

Reference vendor sources: `/…/Luckfox_Lyrs_B/SDK/u-boot/drivers/mmc/`. The
DesignWare register semantics come from `include/dwmmc.h`; the SD state machine
and CSD-capacity math come from `mmc.c`. (The predecessor kept copies under
`reference/uboot-mmc/`; they were not carried.)
