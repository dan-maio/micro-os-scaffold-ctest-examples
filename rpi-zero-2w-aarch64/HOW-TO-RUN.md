# Raspberry Pi Zero 2 W (AArch64) — all CTest cases

Re-scaffolded on 2026-11-24 from the restructured source: the platform is
`micro-os-plus-iii/tests/platforms/aarch64-rpi-zero-2w/`, the entry point is
`./run.sh` at this folder's root. [ARCHITECTURE.md](ARCHITECTURE.md)
([ARCHITECTURE.pdf](ARCHITECTURE.pdf)) explains this board's startup path,
µTest++ support and CTest flow; its first page maps its paths, written for
the previous layout, to the new ones. The harness in general is the
source's `micro-os-plus-iii/tests/` (its `README.md`).

This workspace reproduces every CTest case of the earlier `--micro-test-plus`
scaffold `micro-os-scaffold-workspace-aarch64-rpi-zero-2w-mtp` (since
deleted), plus `mtp-args`:

```text
aarch64-rpi-zero-2w-mtp-args-qemu
aarch64-rpi-zero-2w-mtp-args-hwd
aarch64-rpi-zero-2w-mtp-example-qemu
aarch64-rpi-zero-2w-mtp-example-hwd
aarch64-rpi-zero-2w-mtp-smp-qemu
aarch64-rpi-zero-2w-mtp-smp-hwd
aarch64-rpi-zero-2w-smp-mat-test-qemu
aarch64-rpi-zero-2w-smp-mat-test-hwd
```

The µTest++ tests (`mtp-args`, `mtp-example`, `mtp-smp`) use the new support:
static suites, `argc`/`argv` from semihosting, and a complete TAP report written
with `--output-file`. `smp-mat-test` is the classic test and is unchanged.

`mtp-args` is the dedicated check of the transport: it verifies that the
runner's command line reaches `os_main` (`argc >= 3`, `argv[0]` is the test,
`argv[1]` is `--output-file`, `argv[2]` ends in `.tap`).

## Run

```bash
./run.sh list        # the eight cases (* = run by default)
./run.sh             # build + run the four QEMU cases
./run.sh --hwd       # include the four board cases (Zero 2 W attached)
./run.sh mtp-smp     # one test, QEMU + hardware (hardware skipped by default)
./run.sh -c aarch64-rpi-zero-2w-cmake-gcc-debug --hwd-only mtp-smp
./run.sh build       # configure + build only
./run.sh clean       # remove the build tree
```

## Verified under QEMU

The four QEMU cases pass (2026-11-24):

```text
aarch64-rpi-zero-2w-mtp-args-qemu       Passed
aarch64-rpi-zero-2w-mtp-example-qemu    Passed
aarch64-rpi-zero-2w-mtp-smp-qemu        Passed
aarch64-rpi-zero-2w-smp-mat-test-qemu   Passed
```

The µTest++ reports:

```text
# Running: mtp-args    --output-file .../mtp-reports/aarch64-rpi-zero-2w-mtp-args-qemu.tap
# Running: mtp-example --output-file .../mtp-reports/aarch64-rpi-zero-2w-mtp-example-qemu.tap
# Running: mtp-smp     --output-file .../mtp-reports/aarch64-rpi-zero-2w-mtp-smp-qemu.tap

mtp-args:    10 checks passed, 2 test suites (00 Arguments + 00 Cores)
mtp-example: 27 checks passed, 3 test suites
mtp-smp:     78 checks passed, 10 test suites (speed-up 3.62x on 4 cores)
```

## Verified on the board

On the board (2026-11-24, `PROBE=cmsis-dap ./run.sh --hwd-only`), the four
hardware cases pass:

```text
aarch64-rpi-zero-2w-mtp-args-hwd        Passed   10/10
aarch64-rpi-zero-2w-mtp-example-hwd     Passed   27/27
aarch64-rpi-zero-2w-mtp-smp-hwd         Passed   78/78, speed-up 3.84x on 4 cores
aarch64-rpi-zero-2w-smp-mat-test-hwd    Passed
```

`mtp-example` suites (all auto-registered):

```text
00 - Cores
01 - Runtime
02 - Basics
```

`mtp-smp` suites (all auto-registered):

```text
00 - Cores
01 - Affinity
02 - Critical sections
03 - Spinlocks
04 - Mutexes
05 - Scheduler
06 - Cross-core sync
07 - Queue stress
08 - Atomic read-modify-write
09 - Speed-up
```

## Result files

```text
micro-os-plus-iii/tests/build/aarch64-rpi-zero-2w-cmake-gcc-debug/ctest.xml
micro-os-plus-iii/tests/build/aarch64-rpi-zero-2w-cmake-gcc-debug/ctest.log
micro-os-plus-iii/tests/build/aarch64-rpi-zero-2w-cmake-gcc-debug/mtp-reports/
  aarch64-rpi-zero-2w-mtp-args-qemu.tap
  aarch64-rpi-zero-2w-mtp-example-qemu.tap
  aarch64-rpi-zero-2w-mtp-smp-qemu.tap
```

`ctest.xml` is the JUnit summary (CTest caps its console snippet at 1 KiB);
`ctest.log` is the full CTest output; the `.tap` files are the complete
µTest++ reports.

## Old vs new

| Feature | Old workspace | This workspace |
|---|---|---|
| suites | listed in `main.cpp` with `tr.add_suite (...)` | each suite is a `mt::static_suite` in its own file |
| command line | fixed `mtp_board::options ()` | `mtp_board::run (argc, argv)` parses the debugger command line |
| report file | not requested | `--output-file <path>`, written through semihosting |
| scripts | `build.sh`, `test.sh`, `selected-tests.txt` | one `./run.sh` |
| CTest cases | 6 | 8 (mtp-args, mtp-example, mtp-smp, smp-mat-test, qemu + hwd) |

## Moving or renaming this workspace

`run.sh` derives the root from its own location, and the package links in
the build tree are relative, so this folder can be renamed or moved. A build
tree moved with it is detected from its `CMakeCache.txt` and reconfigured
automatically. The tool and library paths in `scripts/env.sh` are absolute:
on another machine, re-scaffold or override them in the environment.
