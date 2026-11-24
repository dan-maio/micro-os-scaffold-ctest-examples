# micro-os-plus-iii-cortexm

The Cortex-M port of µOS++ III, an xPack used by the kernel's test project
(`micro-os-plus-iii/tests`) and by applications, as upstream's Cortex-M port
is. The boards and their tests are in the kernel's `tests/platforms/cortexm-*/`;
the chips' support is in the device packages `micro-os-plus-iii-devices-rp2xxx`
and `micro-os-plus-iii-devices-stm32f4`.

## What is here

```
micro-os-plus-iii-cortexm/
├── CMakeLists.txt
├── include/cmsis-plus/rtos/port/    the port contract the kernel includes
├── include-m33/                     the generic Cortex-M33 port headers
├── include-rp2350/                  the RP2350 SMP port headers
├── include-rp2040/                  the RP2040 SMP port headers
├── src/rtos/os-core.cpp             upstream: PendSV, SysTick, critical sections
├── src/rtos/os-core-m33.cpp         the generic M33 core (SSE-200 CPUID/MHU)
├── src/rtos/os-core-rp2350.cpp      the RP2350 SMP core (SIO lock/FIFO/CPUID)
├── src/rtos/os-core-rp2040.cpp      the RP2040 SMP core (ARMv6-M, SIO)
├── src/semihosting-exit.cpp         strong _Exit() through SYS_EXIT
├── src/libc/getentropy.c            getentropy() for libstdc++
└── test/                            empty (.gitkeep), as upstream's
```

The CMSIS device headers and `system_*.c` (stm32f4xx, rp2350, rp2040; no
Pico SDK) are in the device packages. The boards, in the kernel's
`tests/platforms/cortexm-<board>/` (board facts, code, linker scripts,
`openocd.cfg`, the board's own tests): seven boards, three Cortex-M profiles,
one and two CPUs:

| board | part | CPUs | applications |
|---|---|---|---|
| `nucleof411` | STM32F411RE, Cortex-M4F | 1 | `mos-test1`, `cmsis-os-validator`, `mutex-stress`, `rtos-apis` |
| `weactf411` | STM32F411CE, Cortex-M4F | 1 | `mos-test1`, `spi-pipeline`, `cmsis-os-validator`, `mutex-stress`, `rtos-apis` |
| `weactf412` | STM32F412RE, Cortex-M4F | 1 | `mos-test1`, `uart-test1`, `cmsis-os-validator`, `mutex-stress`, `rtos-apis` |
| `pico2` | RP2350, 2× Cortex-M33 | **2** | `smp-test0`…`smp-test5`, `clock-test`, `smp-mat-test`, `sc-test-ko`, `fp-switch`, `exc-test`, the USB tests, `cmsis-os-validator`, `mutex-stress`, `rtos-apis`, `cmsis-os-validator-ram`, `mutex-stress-ram`, `rtos-apis-ram`, `smp-mat-test-ram` |
| `pico2-pizero` | RP2350B, 2× Cortex-M33 | **2** | the `pico2` hardware set (no PSRAM) |
| `pico2-rp2350b-psram` | RP2350B + 8 MB PSRAM, 2× Cortex-M33 | **2** | the `pico2` set plus the PSRAM tests |
| `pico` | RP2040, 2× Cortex-M0+ | **2** | the `pico2` set, adapted (`smp-mat-test` at N=80 to fit 264 KB; `fp-switch` has no FPU to test) |

## Building and running

From the kernel's test project, with the port and the device packages linked
(`xpm link` once in each, then `xpm run link-deps --config <configuration>`):

```sh
cd micro-os-plus-iii/tests
xpm run prepare --config cortexm-nucleof411-cmake-gcc-debug
xpm run build   --config cortexm-nucleof411-cmake-gcc-debug
cd build/cortexm-nucleof411-cmake-gcc-debug
ctest -R '^cortexm-nucleof411-mos-test1-hwd$' --output-on-failure   # one, on the board
ctest -L hwd                                                         # all, on the board
```

Hardware only for the three STM32 boards and `pico2-pizero`: they are
programmed over an ST-Link or a CMSIS-DAP probe and run on the desk. The
three RP2350 boards are programmed over CMSIS-DAP. The `pico` (RP2040) is
programmed through the JTAGprobe (CMSIS-DAP v2, `2e8a:000c`) over SWD — the
RP2040 has no JTAG — at 15 MHz by default; `UOS_HW_ADAPTER_KHZ` overrides it.
CTest runs OpenOCD with the board's `openocd.cfg` and the commands in the
platform's `CMakeLists.txt`: program, enable semihosting, reset, run. There
is no GDB in this path and nothing is redirected; the test's semihosting exit
ends OpenOCD and its `RESULT` line is the verdict.

`pico2`, `pico` and `pico2-rp2350b-psram` also build emulated images
(`ctest -L qemu`). The `pico` images run on QEMU's `mps2-an385`
(`-cpu cortex-m3`, single core), the RP2350 ones on `mps2-an500`: the kernel
tests only, since QEMU models neither the SIO, nor core 1, nor the USB.

## Pure RAM Execution on Pico 2 (RP2350)

Four tests run **entirely from internal SRAM** (`0x20000000`, 520 KB) without writing anything to external QSPI flash:
- `cmsis-os-validator-ram`
- `mutex-stress-ram`
- `rtos-apis-ram`
- `smp-mat-test-ram` (dual-core SMP parallel block linear solver)

Each test folder (`tests/platforms/cortexm-pico2/tests/<app>/`, in the kernel) contains:
- `linker-ram.ld`: Links `.text`, `.rodata`, `.data` in SRAM `0x20000000`. Sections `.bss`, `.heap`, `.stack` are marked `(NOLOAD)` and `.embedded_end_block` is embedded compactly in `.text` so the downloaded ELF size remains compact (~26–68 KB).
- its run, in the platform's `CMakeLists.txt` (`_hwd_ram`), which:
  1. Relocates OpenOCD's target work-area to `0x20070000` (`rp2350.cm0 configure -work-area-phys 0x20070000 -work-area-size 0x8000`) so fast CRC verification never overlaps with application memory.
  2. Downloads the ELF into SRAM `0x20000000` via `load_image` and validates via `verify_image`.
  3. Resumes Core 1 (`rp2350.cm1`) to wait in bootrom.
  4. Configures Core 0 (`rp2350.cm0`) VTOR (`0xE000ED08`) to `0x20000000`, sets SP from `[0x20000000]` and PC from `[0x20000004] & ~1` (`_reset_handler`), enables semihosting, and resumes.
  5. The test's semihosting exit ends OpenOCD; its `RESULT` line is the verdict.
- `hw.sh`, `load-ram.cfg`: the former runner and its OpenOCD script, kept
  until the CTest run is proven on the board.
- `README.md` and `README.pdf`: Self-contained test documentation and architecture notes.

The `pico` (RP2040) has the same four tests in
`tests/platforms/cortexm-pico/tests/<app>/`, on its 264 KB SRAM:
`linker-ram.ld` discards `.boot2`, and the run puts OpenOCD's work-area at
`0x20041000` (size `0x1000`) and drives
`rp2040.core1` / `rp2040.core0`.

## Generic QEMU cores

Besides the board cores, the port exports generic single-core QEMU cores that
the tests' emulated configurations link, with `tests/device-qemu-cortexm`
(vector table, CMSIS core, linker script):

| target | CPU | used by |
|---|---|---|
| `micro-os-plus::cortexm-qemu-m0` | Cortex-M0 (`-mfloat-abi=soft`) | `qemu-cortex-m0` |
| `micro-os-plus::cortexm-qemu-m3` | Cortex-M3 (`-mfloat-abi=soft`) | `qemu-cortex-m3` |
| `micro-os-plus::cortexm-qemu-m4f` | Cortex-M4F (`-mfpu=fpv4-sp-d16`) | `qemu-cortex-m4f` |
| `micro-os-plus::cortexm-qemu-m7` | Cortex-M7F (`-mfpu=fpv5-d16`) | `qemu-cortex-m7f` |
| `micro-os-plus::cortexm-qemu-m33` | Cortex-M33 (`-mfpu=fpv5-sp-d16`), 1 or 2 CPUs | `pico2-1cpu`, `2xcortex-m33` |

`micro-os-plus::cortexm-qemu` is kept as an alias of the M7 core. Each core
shares `src/rtos/os-core.cpp` (or `os-core-m33.cpp`), `src/semihosting-exit.cpp`
and the generic `include/`, and differs only in the `-mcpu` flags. The harness
platform sets `OS_NCPU` and, for the SMP M33 case, `OS_USE_SMP_SCHEDULER`; the
`qemu-cortex-*` platforms run the SMP kernel single-core at `OS_NCPU=1`.

## SMP, and where it lives

`pico2` is the SMP board. Two identical, cache-coherent Cortex-M33 in one
coherency domain — unlike the RK3506's Cortex-M0 companion, these two are
peers, so the scheduler places threads on both and either can take an IPI
from the other.

Three pieces of RP2350 silicon make that work, and all three are the
**board's**, not the port's:

| | |
|---|---|
| kernel lock | SIO hardware spinlock 0 — not LDREX/STREX; the RP2350 has no global exclusive monitor, so the architectural route does not work across the two cores |
| IPI | the SIO inter-core FIFO on `SIO_IRQ_FIFO` (external IRQ 25); the handler drains the FIFO and pends its own core's PendSV |
| CPU index | `SIO_CPUID` |

Core 1 is launched through the bootrom FIFO handshake with its own MSP. Only
core 0 advances the RTOS clock; core 1's SysTick reschedules core 1 alone.

The SIO spinlocks survive a debugger reset and a re-flash, so the board's
`boot.S` releases all 32 on core 0 at reset: a previous run that ended holding
spinlock 0 would otherwise hang the next image at its first critical section.

Each thread keeps its own FPU registers, on either core: FPCCR.ASPEN and
LSPEN are on (lazy stacking of s0–s15 and FPSCR on exception entry), and
PendSV saves s16–s31 for a thread whose frame is extended. The suite
`fp-switch` checks it.

`pico` is the same design one generation earlier: the RP2040's SIO gives the
same three pieces (spinlock 0, the inter-core FIFO on `SIO_IRQ_PROC0` /
`SIO_IRQ_PROC1` -- IRQs 15 and 16, one per core -- and `SIO_CPUID`), and
core 1 is launched the same way, after the bootrom's 256-byte flash second
stage (the platform's `src/boot2.S`). The Cortex-M0+ is ARMv6-M, so its
core (`src/rtos/os-core-rp2040.cpp`) saves context with Thumb-1 code, has no
BASEPRI (critical sections are PRIMASK) and no FPU (soft-float). It cannot
read DHCSR from software either, so `src/semihosting-exit.cpp` and
the kernel's `tests/board-tests/cortexm/debug-host.h` skip the "debugger attached" check on
`__ARM_ARCH_6M__`.

## One CPU, and why that matters

`src/rtos/os-core.cpp` is upstream's Cortex-M port, carried over unmodified.
It is single-core, and on these three boards that is what runs: `OS_NCPU=1`,
the kernel's SMP branches compile out, and **no kernel patch is applied**.
That is exactly how the predecessor proved these boards, and it is what makes
them the witness that the SMP port contract is optional rather than assumed.

`pico2` uses a different core — `src/rtos/os-core-rp2350.cpp`, with the port
headers that differ in `include-rp2350/`, searched before `include/` — and names it in its `board.cmake`
(`UOS_BOARD_PORT_CORE`, `UOS_BOARD_PORT_INCLUDE`). The two are 91% the same
file: pico2's is upstream's plus an `OS_USE_SMP_SCHEDULER` branch. `pico`
does the same with `src/rtos/os-core-rp2040.cpp` and `include-rp2040/`, the
RP2350 core with the ARMv6-M context switch and the RP2040's FIFO IRQs.

They are not merged today, for a stated reason rather than an oversight. The
three STM32 boards are hardware-proven on upstream's core, and moving them
onto a different 748-line scheduler has to be re-proven on the desk, not
assumed. A board therefore names the core it was proven with, and adding
`pico2` reached no STM32 board. Merging them is a later step.
