# The new µTest++ test workspaces

This folder holds one workspace per board:

- `micro-os-scaffold-ctest-examples/` (this folder) — WeAct F411CE, Cortex-M4;
- `rpi4b-smp/` — Raspberry Pi 4 B, AArch64, the SMP test, plus `smp_test4`
  (the StateOS stdc++ package adapted to µOS++: std::thread, future/promise,
  mutexes, condition variables, semaphores, barrier/latch, chrono, TLS);
- `pico2-smp-stdcpp/` — Raspberry Pi Pico 2 (RP2350, 2x Cortex-M33), the same
  stdc++ package as `smp-stdcpp-test`, under QEMU (single-core) and on both
  cores; the scaffolded base is `cortexm-pico2/smp-test4`;
- `weactf411-spi-stdcpp/` — WeAct F411CE (STM32F411CE, Cortex-M4F), the
  `spi-stdcpp-test`: the stdc++ package plus the board's SPI NOR flash
  (JEDEC ID, all transports, scratch sector write/restore); hardware only,
  scaffolded from `cortexm-weactf411/spi-pipeline`. On the board
  (2026-11-15, WCH-Link): `mtp-args` 7/7, `mtp-example` 24/24, `spi-pipeline`
  passed, `spi-stdcpp-test` 104/104; and in this folder `mos-test1`,
  `mtp-args`, `mtp-example` passed. The console is USART1 at 115200 on the
  WCH-Link's serial port: WeAct A9 (TX) to the WCH-Link's **RX**, A10 to its
  TX. With TX and RX swapped nothing arrives, and USART1's receiver shows
  noise (RXNE and overrun set) while the board's registers are all correct;
- `rpi-zero-2w-aarch64/` — Raspberry Pi Zero 2 W, AArch64, eight CTest
  cases: `smp-mat-test`, `mtp-example`, `mtp-smp` and `mtp-args` (the
  command-line check), each under QEMU and on the board. It reproduces the
  earlier `--micro-test-plus` scaffold
  `micro-os-scaffold-workspace-aarch64-rpi-zero-2w-mtp` (since deleted),
  with `mtp-args` added;
- `rpi-zero-2w-aarch32/` — Raspberry Pi Zero 2 W, AArch32 (4x Cortex-A53 in
  32-bit mode), ten CTest cases: `smp-mat-test`, `mtp-example`, `mtp-args`,
  `mtp-smp` and `mtp-stdcpp`, each under QEMU and on the board. Scaffolded
  from `aarch32-rpi-zero-2w/smp-mat-test` with `--micro-test-plus` and
  `--stdcpp`, so the stdc++ test is the generated `mtp-stdcpp`, the first
  run of the package in 32-bit mode: 75/75 under QEMU and on the board;
  `mtp-smp` 78/78 with a 3.93x speed-up. Run on the board with
  `PROBE=cmsis-dap ./run.sh --hwd-only`; its JTAG clock is set to 10000 kHz
  in `test/boards/rpi-zero-2w/bcm2837-aarch32.cfg` (1000 in the source).

The `smp_test4` port is described in detail in `rpi4b-smp/STDCXX.md`/`.pdf`;
the Pico 2 and WeAct workspaces are the same guide's Cortex-M applications
(chapters 29.11 and 29.12).

The rest of this page describes the WeAct workspace in this folder. The harness in general is described in `tests/ARCHITECTURE.md`/`.pdf`.

## The WeAct F411CE workspace

This folder is the scaffolded test workspace: one WeAct F411CE board
(`cortexm-weactf411`), one classic CTest test (`mos-test1`) and the new
µTest++ tests (`mtp-example`, and `mtp-args` for the command line). It was produced by `micro-os-scaffold` from
`/home/dan/Desktop/micro-os-plu-iii-smp-pr-github`.

## One command

```bash
cd tests
./run.sh list        # build, then list the CTest cases (* = selected)
./run.sh             # build, then run the selected tests
./run.sh mtp-example # one test, by directory name
./run.sh mtp-args    # the semihosting argc/argv check
./run.sh 4.7.2       # one test, by selection id
./run.sh --hwd       # include the hardware cases (the board is attached)
./run.sh build       # configure + build only
./run.sh clean       # remove the build tree
```

`--hwd-only` still runs only the hardware cases. `-c <configuration>` picks
another configuration; `./run.sh --help` lists every option.

The old `build.sh`, `test.sh`, `openocd.sh`, `qemu.sh` and
`scripts/selected-tests.txt` are gone. `tests/scripts/env.sh` remains the
generated tool-path file.

## What the µTest++ test shows

- **A self-registering suite.** `micro-os-plus-iii-cortexm/test/weactf411/mtp-example/`
  has `main.cpp` (start + run) and one file per suite. Each suite is a
  namespace-scope `mt::static_suite`; nothing lists the suites. Add a file and
  the runner finds it at startup.
- **A real command line.** The test's `os_main (argc, argv)` receives the
  debugger's command line. The generated board runner passes:

  ```text
  arm semihosting_cmdline mtp-example --output-file <build>/.hw-logs/mtp-example.tap
  ```

  (QEMU uses `-semihosting-config ...,arg=mtp-example,arg=--output-file,arg=<path>`;
  the host uses the process arguments.)
- **Results in files.** The complete TAP report goes to
  `tests/build/<configuration>/platform-bin/port-tests/.hw-logs/mtp-example.tap`
  through semihosting. The CTest JUnit summary is
  `tests/build/<configuration>/ctest.xml` (its console snippet is capped by
  CTest at 1 KiB), and the full CTest output is
  `tests/build/<configuration>/ctest.log`.
- **No board `_open()` needed.** The µTest++ CMake block adds
  `-Wl,--wrap=fopen`; the wrapper in `test/mtp/mtp_board.cpp` writes the report
  with `SYS_OPEN`/`SYS_WRITE`/`SYS_CLOSE`.

## Run it on the board

The workspace is hardware-only (`-hwd`): connect the WeAct F411CE and its
CMSIS-DAP probe, then:

```bash
cd tests
./run.sh mtp-example
```

The runner programs the flash, enables semihosting, runs the image and stops
at its `RESULT:` line. The TAP file is left next to the runner log.

## Moving or renaming this workspace

`tests/run.sh` and `tests/scripts/env.sh` derive the root from their own
location, so this folder can be renamed. A build tree moved with it is
detected from its `CMakeCache.txt` (`CMAKE_CACHEFILE_DIR` and
`CMAKE_HOME_DIRECTORY`) and rebuilt automatically; no manual `clean` is
needed. The tool paths in `tests/scripts/env.sh` remain absolute: on another
machine, re-scaffold or override them with `UOS_TOOL_<NAME>`.

## Rebuilding this workspace

From the scaffold project:

```bash
cd /home/dan/Work/micro-os-scaffold
node bin/scaffold.js /home/dan/Work/micro-os-scaffold-ctest-examples \
  --source /home/dan/Desktop/micro-os-plu-iii-smp-pr-github \
  --select 4.7.2 \
  --micro-test-plus /path/to/micro-test-plus \
  --yes
```
