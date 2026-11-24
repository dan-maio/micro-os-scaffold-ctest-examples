# The µTest++ test workspaces

This folder holds nine workspaces, all scaffolded from the restructured
source (`/home/dan/Desktop/micro-os-plu-iii-smp-pr-github`, branch
`align/smp-upstream`: kernel and harness in `micro-os-plus-iii/`, ports by
architecture) on 2026-11-24. The first six are the earlier workspaces,
re-scaffolded; `luckfox-lyra` and the two `*-usb-stdcpp` workspaces were
added later that day. Each workspace mirrors that source:

```text
<workspace>/
  micro-os-plus-iii/            kernel; tests/ is the harness, pruned
    tests/platforms/<platform>/ the board: code, linker script, openocd.cfg, tests/
    tests/mtp/                  µTest++ board support
  <arch>/micro-os-plus-iii-*/   the port and device packages
  third_party/                  micro-test-plus/, stateos-stdc++/
  run.sh                        the one entry point
  scripts/env.sh                tool and library paths (generated)
  scripts/local-tests.txt       the hand-made tests (where there are some)
```

The workspaces:

- `micro-os-scaffold-ctest-examples/` (this folder): WeAct F411CE, Cortex-M4.
  It runs `mos-test1`, `mtp-example` and `mtp-args`, on hardware only.
- `rpi4b-smp/`: Raspberry Pi 4 B, AArch64, 4 cores.
  - `clock-test` and the µTest++ tests `mtp-example`, `mtp-args`, `mtp-smp`,
    `mtp-eth-net` and `mtp-stdcpp`.
  - The local test `smp_test4`: the StateOS stdc++ port described in
    `rpi4b-smp/STDCXX.md`/`.pdf`.
  - Under QEMU: 7/7 passed. `mtp-eth-net` skips under QEMU, by design.
- `pico2-smp-stdcpp/`: Raspberry Pi Pico 2 (RP2350, 2x Cortex-M33).
  - `smp-test4`, the µTest++ tests (hardware only on this board), and the
    local test `smp-stdcpp-test`.
  - `smp-stdcpp-test` runs single-core under QEMU (passed) and on both cores
    on the board.
- `weactf411-spi-stdcpp/`: WeAct F411CE.
  - `spi-pipeline`, the µTest++ tests and the local test `spi-stdcpp-test`:
    the stdc++ package plus the board's SPI NOR flash (JEDEC ID, all
    transports, scratch sector write/restore).
  - Hardware only.
- `rpi-zero-2w-aarch64/`: Raspberry Pi Zero 2 W, AArch64.
  - `smp-mat-test`, `mtp-example`, `mtp-smp` and `mtp-args`.
  - Under QEMU: 4/4 passed.
- `rpi-zero-2w-aarch32/`: Raspberry Pi Zero 2 W, AArch32 (4x Cortex-A53 in
  32-bit mode).
  - `smp-mat-test`, `mtp-example`, `mtp-args`, `mtp-smp` and `mtp-stdcpp`.
  - Under QEMU: 5/5 passed.

- `luckfox-lyra/`: Luckfox Lyra (Rockchip RK3506, 3x Cortex-A7, AArch32),
  WCH-Link probe (SWD), hardware only.
  - The whole platform: the 8 Lyra tests (`smp_test5` to `smp_test7`,
    `smp_test_int` to `smp_test_int5`) and the 13 shared ones.
  - On the board (2026-11-24): 21/21 passed, each run on its own with a
    5-minute cap (16 s to 271 s per test).
  - `smp_test6` and `smp_test7` are USB tests. Their host tool is
    `micro-os-plus-iii/tests/platforms/aarch32-luckfox-lyra/tests/smp_test6/host/`
    (`make`, then `output/usb_test_host --big <file>`). A 1 MiB file sent
    during `smp_test7` echoed back identical for its first 512 KiB, until
    the test ended.
  - µTest++ (`mtp-args`, `mtp-example`, `mtp-smp`) on the board
    (2026-11-24): 10/10, 27/27, and 75/75 with a 2.88x speed-up on
    3 cores. `smp_test0` passes alongside.
  - The fix that makes µTest++ work here is in
    `micro-os-plus-iii/tests/platforms/aarch32-luckfox-lyra/openocd.cfg`.
    It is now in the source repository too (commit `06d365fe` on
    `align/smp-upstream`), so a re-scaffold brings it.
    - With many semihosting calls (µTest++'s console lines and report
      file), OpenOCD's background polling sometimes stopped noticing that
      core 0 had halted at a semihosting trap. The core waited, OpenOCD
      still reported it as running, and the test hung until its timeout.
      Core 0's debug registers confirmed it (halted, by the SVC vector
      catch), and a single poll served the call.
    - A Tcl timer in the config now polls core 0 every 100 ms.
    - Without it, `mtp-args` and a 32 KiB semihosting file test hung;
      with it, they pass.
  - Each semihosting call costs about 0.55 s through the WCH-Link (its
    USB HID transfers), so a µTest++ test takes minutes: `mtp-args` 148 s,
    `mtp-example` 196 s, `mtp-smp` 378 s.
  - The source gives each Lyra hardware case a 1200 s CTest timeout, which
    only matters when a test hangs.

- `rpi-zero-2w-aarch64-usb-stdcpp/`: Raspberry Pi Zero 2 W, AArch64,
  USB gadget + SD card.
  - `usb_test`, `sd_test`, the µTest++ tests and `mtp-stdcpp` from the
    source.
  - The local test `usb-sd-stdcpp`: `usb_test`'s device, protocol and SD
    storage, with the application written on the stdc++ package:
    - `std::thread` for every thread;
    - mutex/condition-variable channels;
    - `std::promise`/`std::future` for the SD requests;
    - `std::async` for the CRC;
    - `std::chrono` for the timings;
    - `std::atomic` for the core tally.
  - The same host apps drive it (`host_xfer.py`, `send_file.py`, in its
    folder).
  - Under QEMU: 7/7 passed (the USB tests report SKIP, no device mode).
  - On the board (2026-11-24):
    - `host_xfer.py`'s full matrix (0 B to 1 MiB) passed PUT, GET, LIST and
      the CRCs in 7 to 9 s: 19 commands, 4 cores active, `RESULT: PASS`;
    - a random 1 MiB file matched byte for byte.
  - The test prints a short UART dump of each stored file, so replies are
    immediate. `usb_test`'s full dump of a 1 MiB file takes about
    5 minutes, which outlasts the host's 120 s wait.
  - The test found a USB driver bug: a receive buffer sharing its last
    cache line with other data lost the last 48 bytes of each 1 MiB
    transfer. It is fixed in the source's bcm283x device package
    (`027acb3`: the driver receives into its own aligned buffer). The
    test's `README.md` has the details.

- `rpi-zero-2w-aarch32-usb-stdcpp/`: the same as the previous workspace, on
  the Zero 2 W in AArch32 mode.
  - `usb_test`, `sd_test`, µTest++, `mtp-stdcpp`, and the local test
    `usb-sd-stdcpp`. Its code is the same as the AArch64 one, with 16 KiB
    `std::thread` stacks (`STATEOS_STDCPP_THREAD_STACK_BYTES=16384`;
    the stdc++ default is 4 KiB outside AArch64).
  - JTAG at 10000 kHz: the workspace keeps its own `bcm2837-aarch32.cfg`
    (`keep` in `scripts/local-tests.txt`).
  - Under QEMU: 7/7 passed (the USB tests report SKIP).
  - On the board (2026-11-24):
    - `host_xfer.py`'s full matrix (0 B to 1 MiB) passed in 3.5 s:
      19 commands, 4 cores active, `RESULT: PASS`;
    - a random 1 MiB file matched byte for byte, with the board's CRC equal
      to the host's.
  - This confirms the bcm283x USB driver fix (`027acb3`) for the AArch32
    driver too.

The hand-made tests are **local tests**. `scripts/local-tests.txt` lists
them, and `micro-os-plus-iii/tests/platforms/<p>/tests/tests.local.cmake`
holds their CMake wiring:

- `smp_test4`, `smp-stdcpp-test` and `spi-stdcpp-test` link `stateos-stdc++`
  and µTest++;
- the two `usb-sd-stdcpp` tests link `stateos-stdc++` and the USB device
  library, and add the SD card and their defines.

A re-scaffold keeps both files and the test folders byte for byte, along
with any `keep <path>` (the two AArch32 Zero 2 W workspaces keep their
`bcm2837-aarch32.cfg`). `./run.sh` runs the local tests by default, and
`./run.sh list` marks them `L`.

## Board results

Re-scaffolded workspaces, on the boards (2026-11-24):

- **Pi 4 B** (`rpi4b-smp`, `./run.sh --hwd-only`, JTAG probe on Pico): 7/7
  hardware cases passed, each after its reset fixture.
  - `mtp-args` 10/10, `mtp-example` 27/27, `mtp-eth-net` 25/25.
  - `mtp-smp` 78/78, speed-up 3.85x on 4 cores (717 ms on one core, 186 ms
    on four).
  - `mtp-stdcpp` 75/75, `smp_test4` (local) 75/75, `clock-test` passed.
- **Pico 2** (`pico2-smp-stdcpp`, `UOS_HW_ADAPTER_SERIAL=E6633861A34C2E38
  ./run.sh --hwd-only`, Debug Probe): 6/6 passed.
  - `mtp-args` 10/10, `mtp-example` 27/27.
  - `mtp-smp` 71/71, speed-up 1.99x on 2 cores (450 ms on one core, 226 ms
    on two).
  - `mtp-stdcpp` 75/75, `smp-stdcpp-test` (local) 75/75, `smp-test4`
    passed.
  - The first run failed `mtp-args` with `argc=0`: the Pico 2's `boot.S`
    calls `main (0, NULL)` and skips the kernel's `startup ()`, which is
    what reads the command line. The µTest++ support now links Cortex-M
    tests with `-Wl,--wrap=main`. A `main ()` entered without arguments
    reads them from the debugger, so `os_main ()` gets them.

- **WeAct F411CE** (WCH-Link):
  - this folder: 3/3 passed (`mos-test1`, `mtp-args` 7/7, `mtp-example` 24/24);
  - `weactf411-spi-stdcpp`: 5/5 passed (`spi-pipeline`, `mtp-args` 7/7,
    `mtp-example` 24/24, `mtp-stdcpp` 72/72, `spi-stdcpp-test` 104/104).
  - `spi-stdcpp-test` is a µTest++ test not named `mtp-*`, so it first got
    no command line and failed its 4 argv checks. Its `tests.local.cmake`
    now opts it in with
    `set_property (GLOBAL APPEND PROPERTY UOS_MTP_APPS spi-stdcpp-test)`.

From the previous layout's workspaces, on the same boards and with the same
test sources (not yet repeated after the re-scaffold):

- **Zero 2 W AArch32:** `mtp-stdcpp` 75/75; `mtp-smp` 78/78, with a 3.93x
  speed-up.
- **Pi 4 B:** `mtp-stdcpp` / `smp_test4` 75/75.
- **Pico 2:** `smp-stdcpp-test` 75/75 on both cores.

WeAct console wiring: USART1 at 115200 on the WCH-Link's serial port, WeAct
A9 (TX) to the WCH-Link's **RX** and A10 to its TX. With TX and RX swapped
nothing arrives, and USART1's receiver shows noise (RXNE and overrun set)
while the board's registers are all correct.

JTAG clock on the Zero 2 W AArch32: 10000 kHz, in `rpi-zero-2w-aarch32` and
`rpi-zero-2w-aarch32-usb-stdcpp`. The restructured source sets 15000 kHz in
`micro-os-plus-iii/tests/platforms/aarch32-rpi-zero-2w/bcm2837-aarch32.cfg`.
Each of these workspaces says 10000 in its copy of that file, and its
`scripts/local-tests.txt` keeps the file (`keep ...`) across re-scaffolds. The scaffolder warns on every run that
source updates of that file are not applied. `UOS_HW_ADAPTER_KHZ=<kHz>`
overrides the clock for one run.

## One command

In any workspace folder:

```bash
./run.sh list        # build, then list the CTest cases (* = selected, L = local)
./run.sh             # build, then run the selected tests (no hardware cases)
./run.sh mtp-example # one test, by folder name (all its cases)
./run.sh --hwd       # the selected tests, hardware cases included
./run.sh --hwd-only  # only the hardware cases (board and probe attached)
./run.sh all         # every registered case
./run.sh build       # configure + build only
./run.sh clean       # remove the build tree
```

- `-c <configuration>` picks another configuration from
  `micro-os-plus-iii/tests/package.json`.
- `-R <regex>` passes a CTest expression.
- `./run.sh --help` lists every option.

The build tree is `micro-os-plus-iii/tests/build/<configuration>/`.
`run.sh` links the workspace's own package copies and the pinned tools
there, then runs the configuration's CMake command. It never runs
`xpm link`, so the machine-wide xpm registrations stay as they are.

Hardware cases: CTest starts OpenOCD with the platform's `openocd.cfg` and
reads the test's `RESULT:` line through semihosting. Two environment
variables select the probe:

- `PROBE=jlink|olimex|cmsis-dap` on the AArch Pi boards;
- `UOS_HW_ADAPTER_SERIAL=<serial>` when several CMSIS-DAP probes are
  attached.

## What the µTest++ tests show

- **A self-registering suite.**
  `micro-os-plus-iii/tests/platforms/<p>/tests/mtp-example/` has `main.cpp`
  (start + run) and one file per suite. Each suite is a namespace-scope
  `mt::static_suite`, and nothing lists the suites.
- **A real command line.** `os_main (argc, argv)` receives `mtp-example
  --output-file <report>`. The registration helpers in
  `micro-os-plus-iii/tests/cmake/uos-tests-run.cmake` add it:
  - `arm semihosting_cmdline` for OpenOCD;
  - `-semihosting-config ...,arg=...` for QEMU.
- **Results in files.** In `micro-os-plus-iii/tests/build/<configuration>/`:
  - `mtp-reports/<ctest name>.tap`: the complete TAP report, written through
    semihosting;
  - `ctest.xml`: the CTest JUnit summary;
  - `ctest.log`: the full CTest output.
- **No board `_open ()` needed.** `-Wl,--wrap=fopen` sends the report to
  semihosting (`micro-os-plus-iii/tests/mtp/mtp_board.cpp`).

## Rebuilding a workspace

From the scaffold project, with the command at the end of the workspace's
`README.md`. For this folder:

```bash
cd /home/dan/Work/micro-os-scaffold
node bin/scaffold.js /home/dan/Work/micro-os-scaffold-ctest-examples \
  --source /home/dan/Desktop/micro-os-plu-iii-smp-pr-github \
  --select cortexm-weactf411.mos-test1 \
  --micro-test-plus https://github.com/micro-os-plus/micro-test-plus-xpack.git \
  --micro-test-plus-ref v6.0.0 \
  --yes
```

A re-scaffold keeps the local tests and their `tests.local.cmake`. It does
not touch the nested workspaces or the hand-written guides (`HOW-TO-RUN.md`,
`STDCXX.md`, `ARCHITECTURE.md`).
