# The new µTest++ test workspaces

This folder holds one workspace per board:

- `micro-os-scaffold-ctest-examples/` (this folder) — WeAct F411CE, Cortex-M4;
- `rpi4b-smp/` — Raspberry Pi 4 B, AArch64, the SMP test, plus `smp_test4`
  (the StateOS stdc++ package adapted to µOS++: std::thread, future/promise,
  mutexes, condition variables, semaphores, barrier/latch, chrono, TLS);
- `pico2-smp-stdcpp/` — Raspberry Pi Pico 2 (RP2350, 2x Cortex-M33), the same
  stdc++ package as `smp-stdcpp-test`, under QEMU (single-core) and on both
  cores; the scaffolded base is `cortexm-pico2/smp-test4`;
- `rpi-zero-2w-aarch64/` — Raspberry Pi Zero 2 W, AArch64, all eight
  CTest cases (including `mtp-args`) of the hand-made
  `micro-os-scaffold-workspace-aarch64-rpi-zero-2w-mtp` workspace plus the
  arguments check.

The `smp_test4` port is described in detail in `rpi4b-smp/STDCXX.md`/`.pdf`;
the Pico 2 workspace is the same guide's Cortex-M application (chapter 29.11).

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
