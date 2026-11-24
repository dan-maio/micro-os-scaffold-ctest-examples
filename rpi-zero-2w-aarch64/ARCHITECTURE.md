# How the Raspberry Pi Zero 2 W AArch64 workspace works

<p class="subtitle">A plain-English walk from power-on to the CTest result: the folders, the files, the startup code, the test framework and the runner</p>

**Board:** Raspberry Pi Zero 2 W, BCM2837, four Cortex-A53 cores, AArch64<br>
**Workspace:** `/home/dan/Work/micro-os-scaffold-ctest-examples/rpi-zero-2w-aarch64`<br>
**Configuration:** `aarch64-rpi-zero-2w-cmake-gcc-debug`<br>
**CTest cases:** eight — four tests, each in a `qemu` and a `hwd` variant

This document explains how the application and its tests are organised, and
what actually happens from the moment the image starts until CTest records a
pass or a fail. It is written for someone who wants to understand or change
the workspace, not for someone looking for a one-line command.

> **Read this first: the workspace layout changed on 2026-11-24.** This guide was
> written for the previous layout. The workspace was then re-scaffolded from the
> restructured source, and the tests, the startup code, the µTest++ support and
> the results are the same. The paths and the runners moved:
>
> | In this guide | In the re-scaffolded workspace |
> | --- | --- |
> | `tests/run.sh`, `cd tests` | `./run.sh` at the workspace root |
> | `tests/scripts/env.sh` | `scripts/env.sh` |
> | `tests/build/<configuration>/` | `micro-os-plus-iii/tests/build/<configuration>/` |
> | `.../.qemu-logs/<test>.tap`, `.../.hw-logs/<test>.tap` | `micro-os-plus-iii/tests/build/<configuration>/mtp-reports/<platform>-<test>-qemu.tap` (or `-hwd.tap`) |
> | `micro-os-plus-iii-smp/` (the kernel) | `micro-os-plus-iii/` |
> | `micro-os-plus-iii-<arch>/` (the port) | `<arch>/micro-os-plus-iii-<arch>/` |
> | `micro-os-plus-iii-<arch>/test/<board>/<test>/`, `.../tests.cmake` | `micro-os-plus-iii/tests/platforms/<platform>/tests/<test>/`, `.../tests/tests.cmake` (for example `micro-os-plus-iii/tests/platforms/aarch64-rpi-zero-2w/tests/mtp-smp/`) |
> | `micro-os-plus-iii-<arch>/test/boards/<board>/` (board code, `linker.ld`, `hw.sh`) | `micro-os-plus-iii/tests/platforms/<platform>/` (`src/`, `include/`, `device/linker-scripts/linker.ld`, `openocd*.cfg`) |
> | `micro-os-plus-iii-<arch>/test/CMakeLists.txt` (the port's test builder) | `micro-os-plus-iii/tests/board-tests/CMakeLists.txt` (one builder, the same hooks) |
> | `micro-os-plus-iii-<arch>/test/mtp/` | `micro-os-plus-iii/tests/mtp/` |
> | `test_smpl/run-qemu.sh`, `run-hw.sh`, the board's `hw.sh` | CTest runs QEMU and OpenOCD itself: `micro-os-plus-iii/tests/cmake/uos-tests-run.cmake` and `uos-hwd.tcl`; the µTest++ command line is added there (marked `SCAFFOLD µTest++`) |
>
> The chapters on the runner scripts describe the old shell runners. The steps are
> the same (halt, enable semihosting, load, resume, read `RESULT:`), but CTest now
> issues them as OpenOCD commands.

## Contents

1. The one-paragraph version
2. The folder map
3. The four tests and the eight CTest cases
4. How CMake turns these sources into test executables
5. The source execution flow, from power-on to the end of a test
6. How the µTest++ tests are put together
7. The CTest framework in this workspace
8. What each test does, and why it is shaped that way
9. Startup, memory and the runtime rule
10. Semihosting, in one page
11. The board, the probe and the one-test-per-power-cycle rule
12. Adding a test or a suite
13. Command and file cheat sheet
14. The shortest possible summary

---

# 1. The one-paragraph version

The workspace is a complete, self-contained copy of the µOS++ SMP kernel
(`micro-os-plus-iii-smp/`), the AArch64 Raspberry Pi port
(`micro-os-plus-iii-aarch64/`) and the test harness (`tests/`). CMake compiles
the kernel and the port **from source** into every test executable; there are
no pre-built libraries. For each test directory under
`micro-os-plus-iii-aarch64/test/rpi-zero-2w/`, the port's builder creates two
executables: `<test>-qemu` and `<test>-hwd`. The harness registers each one as a
CTest case named `aarch64-rpi-zero-2w-<test>-<variant>`.

The `qemu` case runs the image in QEMU (`raspi3b`, four cores); the `hwd` case
runs the same sources on the real board over OpenOCD and a JTAG probe. The
test prints a `RESULT: PASS` or `RESULT: FAIL` line; the runner is a small
shell script that reads that line and exits with 0 or 1. CTest only sees the
script's exit code. µTest++ tests additionally write a complete TAP report to
a file; CTest writes the JUnit summary and the full log.

---

# 2. The folder map

## 2.1 Top level

```text
rpi-zero-2w-aarch64/
├── README.md                    generated guide for this selection
├── HOW-TO-RUN.md                the short commands
├── ARCHITECTURE.md/.pdf         this document
├── scaffold.json                what was selected, which tools, which build
├── .scaffold-manifest.json      what was copied and patched
├── build-logs/                  the configure/build logs of the verification
├── micro-os-plus-iii-smp/       the kernel (complete copy)
├── micro-os-plus-iii-aarch64/   the AArch64 port (selected board + its tests)
├── third_party/
│   └── micro-test-plus/         µTest++ 6.0.0, unchanged
└── tests/                       the CTest harness
```

The kernel and the port are **siblings**, not parent and child. The harness
under `tests/` expects them at `../micro-os-plus-iii-smp` and
`../micro-os-plus-iii-aarch64`; that is the one structural change the
scaffolder makes to the upstream harness.

## 2.2 The kernel: `micro-os-plus-iii-smp/`

This is the complete µOS++ III SMP kernel. The important parts for this
workspace are:

```text
micro-os-plus-iii-smp/
├── CMakeLists.txt               INTERFACE targets: micro-os-plus::iii, ...
├── include/                     public headers (cmsis-plus/rtos/...)
├── src/                         the kernel sources
│   ├── rtos/                    scheduler, threads, mutexes, queues, clocks
│   ├── startup/                 the generic AArch startup helpers
│   ├── libc/                    _sbrk and the newlib glue
│   ├── semihosting/             the kernel's own semihosting syscall layer
│   └── memory/                  the free-store implementation
├── cmake/
│   └── uos-app.cmake            the single "declare one executable" helper
└── test_smpl/                   the shared runners used by the harness
    ├── run-qemu.sh              QEMU: run one image, parse RESULT
    ├── run-hw.sh                OpenOCD: reset, load, run one image, parse RESULT
    ├── run-host.sh              host: run the process, parse RESULT
    └── include/hw_result.hpp    the PASS/FAIL and SYS_EXIT helper
```

The kernel is **complete**, not pruned: the test is written against its
interface, and the interface is allowed to refer to anything in the kernel.

## 2.3 The port: `micro-os-plus-iii-aarch64/`

This is the AArch64 port for the Raspberry Pi family. The board-specific
parts are under `test/`.

```text
micro-os-plus-iii-aarch64/
├── CMakeLists.txt               INTERFACE targets: micro-os-plus::aarch64,
│                                the SoC, the board, the devices
├── include/                     port headers (uart.hpp, exception_handler.hpp,
│                                semihosting.hpp, cmsis-plus/...)
├── src/                         port sources
│   ├── rtos/                    the AArch64 scheduler core, context switch
│   ├── handlers.cpp             C++ init/fini, strong _write/_Exit, syscalls
│   └── ...
├── soc/bcm2837/                 BCM2837 register definitions and support
├── drivers/                     SD, USB, FatFs drivers (only when a test
│                                links micro-os-plus::devices)
└── test/
    ├── CMakeLists.txt           the builder: globs test directories, creates
    │                            <app>-qemu and <app>-hwd via uos_add_app()
    ├── boards/
    │   └── rpi-zero-2w/         the board: startup.S, vectors, linker.ld,
    │                            uart, LED, SD, OpenOCD configs, hw.sh,
    │                            config.txt, hw-park/
    ├── rpi-zero-2w/             the tests of this board
    │   ├── include/             board-test helper headers
    │   ├── src/                 board-test shared sources
    │   ├── tests.cmake          per-test hooks and the µTest++ block
    │   ├── smp-mat-test/        the classic SMP matrix test
    │   ├── mtp-args/            the semihosting argc/argv test
    │   ├── mtp-example/         the µTest++ starter
    │   └── mtp-smp/             the µTest++ SMP test
    └── mtp/                     the µTest++ board support (all mtp-* tests)
```

### The board directory

```text
test/boards/rpi-zero-2w/
├── board.cmake                  CPU flags, 4 cores, linker scripts, capabilities
├── linker.ld                    the memory map: flash/RAM, stack, heap,
│                                .init_array, _Heap_Begin/_Heap_Limit
├── src/startup.S                the first instructions: EL2/EL1, stack, BSS,
│                                MMU, hooks, __libc_init_array, main
├── src/mmu.cpp                  the page tables
├── src/vectors.S                the exception vectors
├── include/                     board headers, os-app-config.h, uart.hpp, ...
├── openocd-cmsisdap-rpi3.cfg    the CMSIS-DAP JTAG configuration
├── openocd-jlink-rpi3.cfg       J-Link
├── openocd-olimex.cfg           Olimex
├── hw.sh                        the hardware runner entry point
├── config.txt                   the boot-partition config for this board
└── hw-park/                     a tiny parking kernel for debug-in-RAM runs
```

`board.cmake` is the only file that says "this is a Zero 2 W":

```cmake
set (UOS_BOARD_FLAGS -mcpu=cortex-a53)
set (UOS_BOARD_NCPU  4)
set (UOS_BOARD_LIBS  micro-os-plus::soc-bcm2837)
set (UOS_BOARD_DEVICES micro-os-plus::devices)
set (UOS_BOARD_LINKER_HW   "${UOS_BOARD_SRC_DIR}/linker.ld")
set (UOS_BOARD_LINKER_QEMU "${UOS_BOARD_SRC_DIR}/linker.ld")
set (UOS_BOARD_DEFINES
     SOC_BCM2837
     SEMIHOST_TRAP_CHOSEN SEMIHOST_TRAP_HLT
     OS_SMP_IPI_SGI=0
     LED_PIN=29
     "PORT_GREETING=\"µOS++ Raspberry Pi Zero 2W (BCM2837, 4x Cortex-A53)\"")
```

The same linker script is used for QEMU and hardware, because QEMU's
`raspi3b` maps the image at the same `0x80000` entry address.

## 2.4 The tests: `micro-os-plus-iii-aarch64/test/rpi-zero-2w/`

There is one directory per test. The directory name is the test name, and the
glob in `test/CMakeLists.txt` finds it; there is no test list.

```text
test/rpi-zero-2w/
├── smp-mat-test/        classic test, no µTest++
│   └── main.cpp         one os_main(), one RESULT line
├── mtp-args/            µTest++ test
│   ├── main.cpp         record argc/argv, start, run
│   └── suite-args.cpp   "00 - Arguments"
├── mtp-example/         µTest++ starter
│   ├── main.cpp         start, release cores, run
│   ├── suite-runtime.cpp "01 - Runtime"
│   └── suite-basics.cpp  "02 - Basics"
└── mtp-smp/             µTest++ SMP test
    ├── main.cpp         start, release cores, run
    ├── smp_helpers.hpp  shared helpers (ncpu, spawn, finish, fmt, ms, ...)
    ├── suite-affinity.cpp
    ├── suite-critical.cpp
    ├── suite-spinlock.cpp
    ├── suite-mutex.cpp
    ├── suite-scheduler.cpp
    ├── suite-cross-core.cpp
    ├── suite-queue-stress.cpp
    ├── suite-atomics.cpp
    └── suite-speedup.cpp
```

The "00 - Cores" suite is not in these directories: it lives in the board
support (`test/mtp/mtp_board.cpp`) and registers itself for every test on a
multi-core board.

## 2.5 The µTest++ board support: `test/mtp/`

```text
test/mtp/
├── mtp_board.hpp        the public interface of the support
├── mtp_board.cpp        the runner, console, report file, RESULT, cores,
│                        spinlock and atomics
├── mtp_semihosting.hpp  raw SYS_GET_CMDLINE / SYS_OPEN / SYS_WRITE / SYS_CLOSE
├── mtp_startup.hpp      the carried startup: main(), command line, scheduler,
│                        main thread, os_main()
└── README.md            the support's own notes
```

Everything in `mtp_*` is compiled into **every** `mtp-*` test through the
`uos-micro-test-plus` INTERFACE library declared in
`test/rpi-zero-2w/tests.cmake`.

## 2.6 The harness: `tests/`

```text
tests/
├── CMakeLists.txt             top-level test project
├── cmake/
│   ├── tests-main.cmake       selects the port, adds the platform
│   ├── common-options.cmake   warnings, common compile options
│   └── global-definitions.cmake
├── platforms/
│   └── aarch64-rpi-zero-2w/
│       ├── CMakeLists.txt     registers the CTest cases
│       ├── cmake/definitions.cmake
│       ├── cmake/dependencies-folders.cmake
│       ├── cmake/platform-library.cmake
│       └── include/           the harness platform header
├── run.sh                     the one entry point (generated)
├── scripts/env.sh             tool paths and UOS_* variables (generated)
├── xpacks/                    build-helper, validator, chan-fatfs, libucontext
└── build/<config>/            created by the build (removed in lean mode)
```

The harness is the only place that knows about **CTest**. The kernel, the port
and the tests do not include any CTest header or call any CTest function.

---

# 3. The four tests and the eight CTest cases

| Test | Kind | Suite files | What it proves | CTest cases |
|---|---|---|---|---|
| `smp-mat-test` | classic | one `main.cpp` | parallel block Gaussian elimination (`A x = b`, `N = 120`, block 20) on four cores, compared with a classical single-core LU solve; plus a 10-worker/20-task thread-pool demo | `...-qemu`, `...-hwd` |
| `mtp-args` | µTest++ | `suite-args.cpp` | the runner's command line reaches `os_main` (`argc`, `argv[0]`, `--output-file`, TAP path) | `...-qemu`, `...-hwd` |
| `mtp-example` | µTest++ | `suite-runtime.cpp`, `suite-basics.cpp` | runtime rule (constructors, heap, sbrk), kernel object basics, TAP/report plumbing | `...-qemu`, `...-hwd` |
| `mtp-smp` | µTest++ | `suite-affinity.cpp` ... `suite-speedup.cpp` | affinity, critical sections, spinlocks, mutexes, scheduler, cross-core sync, queue stress, atomics, speed-up | `...-qemu`, `...-hwd` |

The exact names are:

```text
aarch64-rpi-zero-2w-smp-mat-test-qemu
aarch64-rpi-zero-2w-smp-mat-test-hwd
aarch64-rpi-zero-2w-mtp-args-qemu
aarch64-rpi-zero-2w-mtp-args-hwd
aarch64-rpi-zero-2w-mtp-example-qemu
aarch64-rpi-zero-2w-mtp-example-hwd
aarch64-rpi-zero-2w-mtp-smp-qemu
aarch64-rpi-zero-2w-mtp-smp-hwd
```

Each name is `platform-app-variant`:

- **platform** — `aarch64-rpi-zero-2w`, the platform directory name;
- **app** — the test directory name;
- **variant** — `qemu` for the emulated image, `hwd` for the board.

`smp-mat-test` is the classic style: one `os_main()`, one result, no TAP. The
three `mtp-*` tests use µTest++: suites, test cases, checks, TAP, and the
report file.

---

# 4. How CMake turns these sources into test executables

## 4.1 The entry point

`tests/run.sh` runs CMake:

```text
cmake -S tests -B tests/build/<config> -G Ninja \
  -D CMAKE_BUILD_TYPE=Debug \
  -D PLATFORM_NAME=aarch64-rpi-zero-2w \
  -D CMAKE_TOOLCHAIN_FILE=.../aarch64-none-elf-gcc.cmake
```

Then it runs `cmake --build`, which runs Ninja.

## 4.2 The CMake chain

```text
tests/CMakeLists.txt
  └── enable_testing()
  └── include("cmake/global-definitions.cmake")
  └── include("cmake/tests-main.cmake")
        ├── include("cmake/common-options.cmake")
        ├── include("platforms/aarch64-rpi-zero-2w/cmake/definitions.cmake")
        ├── include("platforms/aarch64-rpi-zero-2w/cmake/dependencies-folders.cmake")
        ├── add_subdirectory("${UOS_AARCH64_DIR}" "port-bin")
        │     └── micro-os-plus-iii-aarch64/CMakeLists.txt
        │           └── (adds the kernel, the SoC, the board, the devices)
        └── add_subdirectory("platforms/aarch64-rpi-zero-2w" "platform-bin")
              └── platform CMakeLists.txt
                    ├── include("cmake/platform-library.cmake")
                    ├── add_subdirectory("${UOS_AARCH64_DIR}/test" "port-tests/test")
                    │     └── micro-os-plus-iii-aarch64/test/CMakeLists.txt
                    │           └── for each test dir: uos_add_app(<app>-qemu)
                    │                                       uos_add_app(<app>-hwd)
                    └── for each test dir: add_test(...qemu), add_test(...hwd)
```

The port's builder is deliberately **reused**: the harness does not describe
the tests, it asks the port to build what is in `test/rpi-zero-2w/`, then
registers those executables with CTest.

## 4.3 What `uos_add_app()` does

For every `<app>-<variant>`:

```cmake
add_executable(<app>-<variant>
  <the test's *.cpp>
  <the board's shared test sources>
  <the kernel sources through micro-os-plus::iii>
  <the port sources through micro-os-plus::aarch64>)
```

The important properties:

| Property | Value | Why |
|---|---|---|
| `SOURCES` | the test's own `*.cpp`, plus shared board-test sources | one executable per test |
| `NCPU` | `4` from `board.cmake` | defines `OS_NCPU=4` and `OS_USE_SMP_SCHEDULER=1` |
| `LINKER_SCRIPT` | `test/boards/rpi-zero-2w/linker.ld` | entry `0x80000`, heap, stack, `.init_array` |
| `DEFINES` | `TRACE`, `__ARM_EABI__`, `__ARM_ARCH_8A__`, `SEMIHOST`, and `QEMU_BUILD` or `HW_BUILD` | the same sources know which variant they are |
| `INCLUDES` | the test directory, the board test include, the port include | headers beside the sources work |
| `LIBRARIES` | `micro-os-plus::aarch64`; for `mtp-*`, `uos-micro-test-plus` | the framework and its support |
| `OUTPUT_NAME` | `<app>-<variant>` | exactly the CTest name suffix |
| `-nostartfiles` | yes | the kernel/port supply `_start` and `main` |
| `-Wl,--gc-sections` | yes | dead code removed |
| `-Wl,-Map,...` | yes | the `.map` beside the ELF |

The AArch64 toolchain makes a `.elf`; the builder also `objcopy`s a `.bin`
(QEMU `-kernel` needs the raw binary) and prints `size`.

The two variants share **all sources**. The only difference is the compile
defines:

- `QEMU_BUILD` — this image runs under QEMU;
- `HW_BUILD` — this image runs on the board.

Board code may branch on them (for example, `hw_result` and the UART mirror),
but the test code is the same.

## 4.4 Where the build lands

```text
tests/build/aarch64-rpi-zero-2w-cmake-gcc-debug/
├── ctest.xml                    written by CTest after a run
├── ctest.log                    written by tests/run.sh after a run
├── platform-bin/
│   └── port-tests/
│       └── test/
│           ├── mtp-args-qemu.elf/.bin/.map
│           ├── mtp-args-hwd.elf/.bin/.map
│           ├── mtp-example-qemu...
│           ├── mtp-smp-qemu...
│           ├── smp-mat-test-qemu...
│           └── ...
└── Testing/Temporary/LastTest.log
```

The `-hwd` executable also has a no-extension companion symlink, which is the
name the hardware runner loads. The `.elf` remains the real file.

---

# 5. The source execution flow, from power-on to the end of a test

This section follows one image, step by step. The same flow applies to QEMU,
the board and the classic test; the differences are called out.

## 5.1 Before the first instruction

### QEMU

`run-qemu.sh` starts:

```text
qemu-system-aarch64 -M raspi3b -smp 4 \
  -nographic -serial none \
  -kernel <app>-qemu.bin \
  -semihosting-config enable=on,target=native,arg=<app>,...
```

QEMU loads the raw binary at `0x80000`, starts four Cortex-A53 cores in
AArch64, and services semihosting calls on the host.

### The board

`hw.sh` → `run-hw.sh` does, in order:

1. optionally reset the board through the BCM watchdog
   (`mww 0x3f100024 0x5a000001; mww 0x3f10001c 0x5a000020`), then wait for it
   to boot;
2. open the JTAG probe, `init`, halt the cores;
3. enable semihosting on the cores;
4. set the semihosting command line on **core 0**;
5. `load_image` the `<app>-hwd` ELF into RAM at `0x80000`;
6. resume core 0 at `0x80000`; cores 1-3 stay parked in the firmware spin-table
   loop until the kernel releases them;
7. read OpenOCD's log until `RESULT: PASS`, `RESULT: FAIL`, `RESULT: SKIP`, a
   timeout, or OpenOCD dying.

The board's RUN pin must be high; a low RUN (`nRESET = 0` at the probe) means
the SoC is held in reset and nothing starts. See section 11.

## 5.2 `_start` in `startup.S`

The first instructions are in
`micro-os-plus-iii-aarch64/test/boards/rpi-zero-2w/src/startup.S`.

```text
_start:
  read CurrentEL
  EL2 -> set up EL1 and drop to EL1
  set the stack (core 0)
  install VBAR_EL1 with vector_table
  enable FP/SIMD (CPACR_EL1.FPEN)
  zero .bss
  mmu_init
  os_startup_initialize_hardware_early()   (board hook)
  os_startup_initialize_hardware()         (board hook)
  __libc_init_array()                      (C++ static constructors)
  main()                                   (C or C++ main)
```

The order matters. The heap is given to the µOS++ free store **before**
`__libc_init_array()`, so a static constructor may allocate. `__libc_init_array`
runs the functions in `.init_array`; the linker script keeps that section, and
the scaffolder checks that before it adds µTest++.

Cores 1-3 do not run this path at boot: they wait in the GPU firmware's
spin-table loop. The kernel releases them later, by writing their release
address (`0xD8 + 8*core`) and sending `SEV`.

## 5.3 The static constructors: where the suites appear

Before `main()` runs, the `.init_array` constructors run. Two things happen
there:

1. A `mt::static_suite` object in each suite file constructs itself and inserts
   itself into the framework's list of suites. It only stores a reference to
   the runner; it does not call it yet.
2. The board support's `"00 - Cores"` suite registers the same way.

The runner itself is a **function-local static**:

```cpp
mt::runner&
mtp_board::runner () noexcept
{
  static mt::runner the_runner;
  return the_runner;
}
```

The first static suite that calls `runner()` constructs it. This is safe
because the constructor of a `static_suite` only stores the reference; the
framework deliberately allows a suite to be constructed before its runner.

There is no `main`-time list of suites: `tr.add_suite(...)` is not used
anywhere. Adding a suite is adding a file that defines a `static_suite`; the
runner discovers it through the constructors.

## 5.4 `main()`: two families

### 5.4.1 µTest++ tests (`mtp-*`)

The test's `main.cpp` includes `mtp_startup.hpp` at the end. On this board
`MTP_CARRY_STARTUP=1`, so that header provides a strong `main()`:

```text
main(argc, argv):
  1. mtp_semihosting::args(mtp_argc, mtp_argv)
       SYS_GET_CMDLINE from OpenOCD/QEMU, parse into argc/argv
  2. set and initialize the interrupts stack
  3. scheduler::initialize()
  4. create the static "main" thread with a 32 KiB stack
  5. os_main_thread = &main_thread
  6. os_startup_create_thread_idle()
  7. scheduler::start()          -> the main thread runs mtp_main_trampoline()
```

The trampoline calls `os_main(mtp_argc, mtp_argv)` and then `std::exit(code)`.
`std::exit` runs the atexit/destructor path; on this port the strong `_Exit`
ends the run through semihosting `SYS_EXIT`.

The command line is read on **core 0**, before the scheduler starts, because
`SYS_GET_CMDLINE` must run on the core the debugger is servicing. The parsed
strings live in a static 512-byte buffer and a 32-pointer argv array in
`mtp_semihosting.hpp`.

### 5.4.2 Classic test (`smp-mat-test`)

`smp-mat-test/main.cpp` carries its own `main()` and its own startup hooks. It
does not read the command line and does not use µTest++.

```text
main():
  set the interrupts stack
  scheduler::initialize()
  create the "main" thread (8 KiB stack)
  create the idle thread
  scheduler::start()
       -> custom_main_trampoline()
            -> std::exit(os_main(0, nullptr))
```

It calls `uart::init`, `led::init`, `timer_init`, and the free-store setup in
its `os_startup_initialize_hardware` hook. Its `os_main` runs the matrix work
on the four cores, compares the result with the classical result, prints
`RESULT: PASS` (or `FAIL`), and calls `hw_result::ok()/fail()`.

The classic test is included to show that not every CTest case has to use
µTest++. CTest treats both families the same way: it runs a command and
checks its exit code.

## 5.5 `os_main()`: the test's own entry point

For the three µTest++ tests, `os_main` is short and nearly identical:

```cpp
int
os_main (int argc, char* argv[])
{
  mtp_board::start ("mtp-smp");
#if defined(MTP_HAS_SMP_BOOT) && MTP_HAS_SMP_BOOT
  mtp_board::release_cores ();
#endif
  return mtp_board::run (argc, argv);
}
```

The three calls do a lot:

### `mtp_board::start(name)`

- records the test name and the start time;
- installs the console:
  - **AArch64 (model 1):** replaces `stdout` with a `fopencookie` stream. Every
    write goes to the UART at once and is batched into semihosting
    `SYS_WRITE0` for the host. The batch is flushed at the end of a suite, on
    an `ok`/`not ok` line, or when 3 KiB are waiting;
  - Cortex-M (model 2) would install a strong `_write`;
  - POSIX (model 3) leaves the host's stdout;
- prints the banner:
  `# mtp-smp, under QEMU` or `# mtp-smp, on the board`.

### `mtp_board::release_cores()`

On this board `MTP_HAS_SMP_BOOT=1`: the port's `test-smp-boot` helpers are
used.

- pins the runner to core 0 (`cpu_affinity(1u << 0)`);
- installs the boot threads;
- `smp::start_secondary_cores()`;
- waits up to 3 seconds for all four cores to join the scheduler;
- prints `# cores: c1=3 c2=3 c3=3, 0 ms`.

The "00 - Cores" suite then checks that this really happened.

### `mtp_board::run(argc, argv)`

```cpp
int
mtp_board::run (int argc, char* argv[]) noexcept
{
  initialise (argc, argv);             // options() + runner().initialise()
  return finish (runner ().run_suites ());
}
```

`initialise()`:

1. `options(argc, argv)` parses the command line into `mt::runner_options`:
   `--output-file <path>`, `--verbose`, `--quiet`, `--filter=<re>`, ...
   It also stores the raw `argc`/`argv` for `mtp-args`
   (`argument_count()`, `argument(i)`).
2. The reporter is forced to TAP.
3. `runner().initialise(options)` creates the reporter, starts the session and
   opens the output file (if any).

`run_suites()` runs the static suites **sorted by name**. For `mtp-smp` that
is:

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

Each suite body calls `ts.test("name", [] (auto& t) { ... })`. The test case
runs immediately, on the same thread and stack; it reports `ok`/`not ok` and
its checks to the TAP reporter. A failed `t.expect` is recorded and the test
case continues; a failed `t.require` would abort, and this build has no
exceptions, so the tests use `expect`.

`finish(exit_code)`:

- flushes all streams (`std::fflush(nullptr)`), so the report file and the
  console are complete before the run stops;
- prints the `RESULT: PASS (...)` or `RESULT: FAIL (...)` line;
- on the board, calls `hw_result::ok()`/`fail()`, which is `SYS_EXIT` and stops
  the core. Under QEMU, `SYS_EXIT` also makes QEMU exit.

## 5.6 The report file and the console

There are two outputs.

**The console** is the UART plus the debugger's semihosting console. The
`fopencookie` stream writes every byte to the UART and batches the same text
to the host. The batching exists because each semihosting call halts the cores
until the debugger answers; a call per character would make the test crawl.

**The report file** is requested with `--output-file <path>`. µTest++'s
reporter calls `fopen(path, "w")`. The support links with
`-Wl,--wrap=fopen`; the wrapper in `mtp_board.cpp` opens the path with
`SYS_OPEN` and builds a newlib cookie stream whose writes use `SYS_WRITE`.
That is why no board needs `_open()` or a filesystem: the file is on the host,
created by OpenOCD or QEMU. The file always gets the complete report, whatever
the console verbosity.

The runner adds `--output-file` only for `mtp-*` tests:

```text
QEMU:   -semihosting-config ...,arg=<app>,arg=--output-file,arg=<log>.tap
OpenOCD: arm semihosting_cmdline <app> --output-file <log>.tap
host:   <exe> --output-file <log>.tap
```

The `mtp-args` test is the proof that this path works: it checks `argc >= 3`,
`argv[0]`, `argv[1] == "--output-file"`, `argv[2]` ends in `.tap`, and that
`mtp_board` parsed the same count.

## 5.7 The end of the run

| Environment | What ends the run | What CTest sees |
|---|---|---|
| QEMU | `SYS_EXIT` from `hw_result`/`_Exit`; QEMU exits | `run-qemu.sh` sees the process end and greps the log for `RESULT: PASS`; the script exits 0/1 |
| Board | `SYS_EXIT` from `hw_result`; OpenOCD sees the semihosting exit and stops the core | `run-hw.sh` greps its log for `RESULT: PASS`; `hw.sh` exits 0/1 |
| Host | the process returns from `main`/`os_main` | `run-host.sh` greps its log and exits 0/1 |

The `RESULT` line is the contract between the test and the runner. It is
printed by the test itself; the runner does not interpret the TAP report to
decide pass/fail, it reads the final `RESULT` line. The TAP report is kept for
people and CI tools.

`finish()` prints the line **after** flushing the report, so the `.tap` file
and the console agree.

---

# 6. How the µTest++ tests are put together

## 6.1 `main.cpp`

A µTest++ test's `main.cpp` is only the orchestration:

```cpp
#include <mtp_board.hpp>

int
os_main (int argc, char* argv[])
{
  mtp_board::start ("mtp-args");
#if defined(MTP_HAS_SMP_BOOT) && MTP_HAS_SMP_BOOT
  mtp_board::release_cores ();
#endif
  return mtp_board::run (argc, argv);
}

#include <mtp_startup.hpp>
```

There is no suite list. `mtp_startup.hpp` supplies `main()` and the startup
hooks; `mtp_board::run()` runs whatever static suites the linker collected.

## 6.2 A suite is a file

Every suite is a namespace-scope object in its own `.cpp` file:

```cpp
mt::static_suite suite_affinity{
  "01 - Affinity", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("the runner is on core 0", [] (auto& t) {
      t.expect (mt::eq (mtp_board::cpu_id (), 0u));
    });
    ...
  }
};
```

The name is sorted by the runner. The `NN - ` prefix gives the report order:
`00 - Arguments`, `00 - Cores`, `01 - Affinity`, ... . Two suites may share a
prefix; for equal names the order of construction wins.

Shared helpers live in a normal header. `mtp-smp/smp_helpers.hpp` has `ncpu`,
`last`, `ms()`, `load()`, `store()`, `spawn()`, `finish()`, `spin_until()`,
`wait_until()`, `spin_until_bounded()`, `busy_loop()`, `fmt()` and `via()`.
The suite files include it; it contains no suite and no registration.

## 6.3 The board support

`mtp_board.hpp` / `mtp_board.cpp` provide:

| Function / type | Purpose |
|---|---|
| `runner()` | the one `mt::runner` for this image (function-local static) |
| `start(name)` | console, banner, start time |
| `options(argc, argv)` | parse the command line, force TAP |
| `argument_count()`, `argument(i)` | the raw command line, for `mtp-args` |
| `run(argc, argv)` | initialise + `run_suites()` + `finish()` |
| `finish(code)` | `RESULT` line, `SYS_EXIT` |
| `skip(reason)` | `RESULT: SKIP`, ended as a pass |
| `release_cores()`, `cores_joined()`, `cores_summary()` | the other cores |
| `cpu_id()`, `irqs_masked()` | per-core facts |
| `sync_method()`, `spinlock`, `atomic_add()` | the board's cross-core method |
| `suite_cores(ts)` | the "00 - Cores" suite |

The console model for this board is 1. The support defines **weak**
`_write`, `_read`, `_close`, `_fstat`, `_isatty` and `_lseek` helpers for the
newlib paths that bypass `stdout`; the port keeps its own strong versions
where it has them. The report file does **not** use `_open`: it goes through
the `fopen` wrapper.

`mtp_semihosting.hpp` is the raw layer:

- `SYS_GET_CMDLINE` → a 512-byte line, split into `argc`/`argv` with quotes
  and blanks;
- `SYS_OPEN`, `SYS_WRITE`, `SYS_CLOSE`, `SYS_SEEK`, `SYS_FLEN`;
- one parameter-block type, `word_t = uintptr_t` (64-bit on AArch64, 32-bit on
  Cortex-M/AArch32);
- the trap: `hlt #0xF000` on AArch64, `hlt`/`svc` on AArch32, `bkpt 0xAB` on
  Cortex-M.

`__wrap_fopen` uses that layer to make a `FILE*` for the report, so the
reporter's `fopen`/`vfprintf`/`fclose` work unchanged.

## 6.4 The `00 - Cores` suite

`mtp_board.cpp` registers it when `MTP_HAS_SMP_BOOT` is non-zero:

```cpp
namespace {
  mt::static_suite mtp_cores_suite{
    "00 - Cores", mtp_board::runner (), mtp_board::suite_cores
  };
}
```

It checks that `release_cores()` was called, that every core joined, and that
it happened within 3 s. It is board support, not a test file, because every
multi-core test gets it.

---

# 7. The CTest framework in this workspace

## 7.1 `tests/run.sh`: the single entry point

`tests/run.sh` is generated from `scaffold.json`. It does not contain test
logic; it configures, builds, selects cases and calls `ctest`.

```bash
cd tests
./run.sh                  # build + the selected tests (qemu; hwd skipped)
./run.sh build            # configure + build only
./run.sh list             # list every registered case (* = selected)
./run.sh mtp-args         # one test, both variants (hwd skipped)
./run.sh --hwd            # include the hardware cases
./run.sh --hwd-only       # hardware cases only
./run.sh all --hwd        # everything
./run.sh clean            # remove the build tree
```

`-c <configuration>` picks another build configuration (there is one here).
`--regex <re>` hands a regular expression to CTest; `--no-build` skips the
build; `-v` adds `-V` to CTest.

The script writes:

```text
build/aarch64-rpi-zero-2w-cmake-gcc-debug/ctest.xml   JUnit summary
build/aarch64-rpi-zero-2w-cmake-gcc-debug/ctest.log   full CTest output
```

CTest caps the console snippet in each JUnit `<testcase>` at 1 KiB (the XML
says so); `ctest.log` is copied from `Testing/Temporary/LastTest.log` and has
the full text. The complete TAP report is the `.tap` file.

## 7.2 How CMake registers the cases

`tests/platforms/aarch64-rpi-zero-2w/CMakeLists.txt`:

```cmake
add_test (
  NAME "${PLATFORM_NAME}-${_app}-qemu"
  COMMAND bash "${_runner}/run-qemu.sh" "${_port_bin}" "${_qemu}" -M raspi3b -smp 4
)
set_tests_properties (... PROPERTIES
  ENVIRONMENT "UOS_QEMU_ONLY=${_app};UOS_TEST_SRC_DIR=${_port_test_dir}"
  LABELS "qemu" TIMEOUT 2400)

add_test (
  NAME "${PLATFORM_NAME}-${_app}-hwd"
  COMMAND bash "${_board_hw}" "${_app}" 600
)
set_tests_properties (... PROPERTIES
  ENVIRONMENT "BUILD=${CMAKE_CURRENT_BINARY_DIR}/port-tests"
  LABELS "hwd" TIMEOUT 1200)
```

Two details matter:

- `UOS_QEMU_ONLY=${_app}` tells `run-qemu.sh` to run **one** app, not every
  `*-qemu.bin` in the directory. CTest calls the script once per case.
- `BUILD=.../port-tests` tells `hw.sh` where the ELFs are. `hw.sh` appends
  `/test`, which is the folder `uos_add_app` used.

## 7.3 The QEMU case

```text
ctest
  -> run-qemu.sh <build>/port-tests/test <qemu> -M raspi3b -smp 4
       only app = UOS_QEMU_ONLY (one app)
       for that app:
         timeout <per-app> qemu-system-aarch64 ... \
           -nographic -serial none \
           -kernel <app>-qemu.bin \
           -semihosting-config enable=on,target=native,arg=<app>,...
         read the log
         RESULT: PASS -> exit 0
         RESULT: SKIP -> exit 3
         RESULT: FAIL -> exit 1
         timeout / no result -> exit 1
```

The per-app timeout table is in `run-qemu.sh` (`smp-mat-test` longer, the
small tests shorter); CTest's own limit is 2400 s.

QEMU does not touch the UART: `-serial none`. The console reaches the host
through semihosting. That is also why the QEMU log contains the whole TAP
report.

## 7.4 The hardware case

```text
ctest
  -> hw.sh <app> 600          (PROBE=cmsis-dap, BUILD=.../port-tests)
       -> run-hw.sh <BUILD>/test <app> 600
            optional pre-load watchdog reset
            openocd -f openocd-cmsisdap-rpi3.cfg -c init
            halt the cores
            arm semihosting enable on the cores
            arm semihosting_cmdline <app> --output-file <BUILD>/test/.hw-logs/<app>.tap
            load_image <BUILD>/test/<app>-hwd
            resume core 0 at 0x80000; cores 1-3 stay parked
            watch the OpenOCD log for RESULT: PASS/FAIL/SKIP
            exit 0/1/3
```

The board has no usable SRST, and the script does not use GDB. "One test per
power cycle" is the rule: the script loads into RAM over whatever the previous
test left there, and it cannot reset the SoC itself. The pre-load watchdog
write is the closest thing to a reset; it is attempted, and if it fails the
script reports it and does not pretend the test ran.

The script never opens `/dev/ttyACM0`. The UART belongs to the person at the
bench; the runner only reads OpenOCD's semihosting output.

## 7.5 What CTest actually checks

For both variants, CTest runs a **shell command** and checks its **exit
status**. It does not parse TAP, does not know about the board, and does not
know about semihosting. The shell script is the adapter:

- QEMU / host: the script greps its captured log for `RESULT: PASS`;
- board: `run-hw.sh` greps the OpenOCD log for `RESULT: PASS`.

If the image never prints the line (a hang, a wrong entry address, a probe
failure), the script reports a timeout or no-result and exits non-zero.

## 7.6 The result files

| File | Written by | Contents |
|---|---|---|
| `.qemu-logs/<app>.tap` | µTest++ via semihosting `SYS_OPEN`/`SYS_WRITE` | the complete TAP report |
| `.hw-logs/<app>.tap` | same, on the board | the complete TAP report |
| `.host-logs/<app>.tap` | same, on the host | the complete TAP report |
| `build/<config>/ctest.xml` | CTest `--output-junit` | one `<testcase>` per case; labels, time, truncated console |
| `build/<config>/ctest.log` | `tests/run.sh` | the full CTest console, copied from `Testing/Temporary/LastTest.log` |
| `.qemu-logs/<app>.log` | `run-qemu.sh` | QEMU's own console/log |
| `.hw-logs/<app>.log` | `run-hw.sh` | OpenOCD's log (the semihosting console) |

The `.tap` file is the authoritative complete report. `ctest.xml` is the CI
summary; CTest deliberately keeps only a 1 KiB snippet of the console text in
it.

---

# 8. What each test does, and why it is shaped that way

## 8.1 `smp-mat-test` — the classic SMP test

A single `main.cpp`. It does not use µTest++, does not read the command line,
and does not write TAP. It is the reference for "a CTest case can be any
program that prints RESULT".

What it proves:

- the four cores are released and run;
- a 10-worker thread pool executes 20 tasks, so cross-core dispatching works;
- the real test solves `A x = b` (`N = 120`, block size `B = 20`) by parallel
  block Gaussian elimination: six row blocks distributed round-robin over the
  cores, generation-based spin barriers, core 0 doing the diagonal LU and the
  back substitution;
- a classical `N x N` LU solve on core 0 is the ground truth;
- the two solutions are compared (`max_diff < 0.01f`), both are timed, and the
  speed-up is printed;
- the verdict is printed as
  `RESULT: PASS (block == classical within tolerance)`.

It writes to the PL011 UART, blinks the LED while the solver runs and keeps it
as a heartbeat after the summary.

Its `os_main` carries the startup hooks and the `main` thread. This is the
classic pattern: the test owns its start-up and its `os_main`.

## 8.2 `mtp-args` — the transport test

The smallest µTest++ test, and the one that answers "did the command line
arrive?".

`main.cpp` records `argc`/`argv` in the `mtp_args` namespace, calls
`mtp_board::start`, optionally releases the other cores, and calls
`mtp_board::run`.

`suite-args.cpp` checks:

1. `argc >= 3` — the program name, `--output-file`, and its path;
2. `argv[0]` contains `mtp-args`;
3. `argv[1]` is exactly `--output-file`;
4. `argv[2]` ends in `.tap`;
5. `argv[argc]` is a null pointer;
6. `mtp_board::argument_count()` equals the `argc` that `os_main` saw.

It prints the raw line at the start of the report, so the log shows exactly
what arrived. On this four-core board it also gets the support's
`00 - Cores` suite, so its report has two suites and ten checks.

## 8.3 `mtp-example` — the runtime and starter test

`main.cpp` is the same three-line shape. Its suites are:

- **00 - Cores** (from the support, on this board) — release and join;
- **01 - Runtime** — `.data`/`.bss`, the static constructors, `malloc`/`new`/
  `calloc`, a quarter of the linker heap, `sbrk` agreement with
  `_Heap_Begin`/`_Heap_Limit`;
- **02 - Basics** — comparisons, a kernel semaphore, `test_each`.

This is the test to copy when starting a new one: it shows the three layers
(board support, runtime rule, ordinary checks) without a lot of hardware
detail.

## 8.4 `mtp-smp` — the SMP test

One suite per file, each a static suite:

| Suite | What it checks |
|---|---|
| 00 - Cores | `release_cores()` and the join of cores 1-3 |
| 01 - Affinity | the runner is on core 0; pinned threads stay pinned; affinity changes at run time |
| 02 - Critical sections | the kernel lock excludes all cores, masks/nests/restores this core's IRQs, `scheduler::critical_section` is local |
| 03 - Spinlocks | no lost update from four cores; `try_lock` refused while held |
| 04 - Mutexes | four-core contention, `timed_lock`, recursive, priority inheritance across cores |
| 05 - Scheduler | preemption, yield, per-core sleep, load spread |
| 06 - Cross-core sync | semaphore ping-pong, ordered queue, event flags, barrier |
| 07 - Queue stress | one producer per core, no loss, no duplicates |
| 08 - Atomic RMW | `atomic_add()` from every core via LDAXR/STLXR; `std::atomic` too |
| 09 - Speed-up | the same work on one core vs all four |

`smp_helpers.hpp` has only helpers; no suite. `main.cpp` has no list.

Under QEMU the speed-up is reported but not checked against a hard floor,
because the four "cores" are host threads and the host is shared. On the board
it must be at least 0.8 × 4.

---

# 9. Startup, memory and the runtime rule

The linker script (`test/boards/rpi-zero-2w/linker.ld`) defines:

- entry `0x80000`;
- `.text`, `.rodata`, `.data`, `.bss`;
- `.init_array` / `.fini_array`, kept;
- `_Heap_Begin` / `_Heap_Limit` — the region the free store and `sbrk` share;
- the stacks (`__stack`, `__fiq_stack_top`, `__irq_stack_top`).

Before µTest++ is added, the scaffolder checks the "runtime rule" in the
sources:

1. the linker script keeps `.init_array` and defines
   `__init_array_start`/`__init_array_end`;
2. it defines `_Heap_Begin`/`_Heap_Limit`;
3. the boot code runs the static constructors;
4. the heap is handed to the µOS++ free store before the constructors run;
5. `sbrk` uses the same region.

`mtp-example`'s "01 - Runtime" checks the same facts on the running target.
This is why the static suites (which are static objects) can allocate during
construction.

The startup stack is the exception stack; the main thread has its own stack
(32 KiB in `mtp_startup.hpp`, 8 KiB in `smp-mat-test`). The scheduler switches
to it in `scheduler::start()`.

---

# 10. Semihosting, in one page

Semihosting is a convention: the target executes a special instruction, the
debugger or emulator traps it, and the host performs the operation (read a
file, write a string, read the command line, exit). On this port:

- **AArch64:** `hlt #0xF000`; operation in `x0`, parameter block in `x1`.
- **AArch32:** `hlt` on the Pi (OpenOCD `aarch64` target) or `svc 0x123456` on
  a classic ARMv7 target.
- **Cortex-M:** `bkpt 0xAB`.

The parameter block is an array of machine words (`uintptr_t`): 32-bit on
Cortex-M/AArch32, 64-bit on AArch64.

The operations used by this workspace:

```text
SYS_GET_CMDLINE  0x15   read the command line
SYS_OPEN         0x01   open the report file
SYS_WRITE        0x05   write report bytes
SYS_CLOSE        0x02   close it
SYS_EXIT         0x18   end the run with PASS/FAIL
```

The command line is a single string on the host side. OpenOCD's
`arm semihosting_cmdline` joins its arguments with spaces; QEMU's
`-semihosting-config arg=...` does the same. The target splits it into
`argc`/`argv` in `mtp_semihosting::split`, honoring single and double quotes.

The report file is opened by `SYS_OPEN` with the host's filesystem path; QEMU
and OpenOCD create it in the host. The `fopen` wrapper is what makes µTest++'s
portable `fopen` path work on a bare-metal image.

`SYS_EXIT` is how a hardware run stops. Without it, the core would idle until
the runner times out; the `RESULT` line is printed before the exit.

---

# 11. The board, the probe and the one-test-per-power-cycle rule

## 11.1 The boot card

The board boots `kernel8.img` from the FAT boot partition. For debug-in-RAM
runs, the card should carry the `hw-park` kernel from
`test/boards/rpi-zero-2w/hw-park/`, and a `config.txt` next to it with:

```text
arm_64bit=1
kernel=kernel8.img
enable_uart=1
core_freq=250
gpu_mem=16
enable_jtag_gpio=1
```

The repository's `config.txt` and `hw-park/config.txt` already carry the first
five lines; **add `enable_jtag_gpio=1` to the card's file**. It selects Alt4
for GPIO 22-27 and enables the ARM JTAG interface. Without it, the debugger
sees a silent JTAG scan chain (`all zeroes`).

`hw-park` prints one line per core on the PL011 UART and parks. Cores 1-3 stay
in the firmware spin-table loop, which is exactly the state the loader expects.

## 11.2 The probe

The Zero 2 W has no JTAG connector; the signals are on GPIO 22-27:

```text
TCK, TMS, TDI, TDO and GND
```

The workspace is set up for a Raspberry Pi Debug Probe / Pico running the
CMSIS-DAP firmware (`2e8a:000c`). Choose it with:

```bash
PROBE=cmsis-dap ./run.sh --hwd-only
```

The other configs are `PROBE=jlink` (the default) and `PROBE=olimex`.

`hw.sh` loads the matching OpenOCD config. The CMSIS-DAP config uses
`interface/cmsis-dap.cfg`, `transport select jtag`, `board/rpi3.cfg`, and
`gdb port disabled`.

## 11.3 The handshake

`run-hw.sh`:

1. attempts a watchdog reset (this is a **pre-load** reset, not SRST);
2. waits for the board to re-enable JTAG;
3. `init`, halts the cores;
4. enables semihosting on the cores;
5. sets `arm semihosting_cmdline` on core 0;
6. `load_image` the ELF;
7. resumes core 0 at `0x80000`; cores 1-3 stay parked until the kernel writes
   their release slots and sends `SEV`;
8. watches the log for `RESULT: PASS|FAIL|SKIP`.

The probe reports `nRESET = 0` when the board's RUN line is held low. That
means the SoC is in reset and the JTAG scan cannot see the TAP; the runner
reports "watchdog reset FAILED" and does not pretend the test ran. Release
RUN (or remove the probe's reset lead) and power-cycle the board.

## 11.4 The UART is the user's

`hw.sh` and `run-hw.sh` never open `/dev/ttyACM0`. The UART console belongs to
the person at the bench. The runner captures the OpenOCD semihosting log; the
complete test report is in `.hw-logs/<app>.tap`.

---

# 12. Adding a test or a suite

## 12.1 A new µTest++ suite

Add a file next to the existing suites:

```cpp
// suite-<name>.cpp
#include <mtp_board.hpp>

mt::static_suite suite_my_check{
  "10 - My check", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("all cores see the same value", [] (auto& t) {
      t.expect (mt::eq (mtp_board::cpu_id (), 0u));
    });
  }
};
```

The port's builder globs `*.cpp` in the test directory; the new file is picked
up by `CONFIGURE_DEPENDS`. The runner discovers the suite; no `main.cpp` or
CMake change is needed. The name determines the report order.

## 12.2 A new test

Create a directory under `test/rpi-zero-2w/<name>/` with at least one `.cpp`.
The builder creates `<name>-qemu` and `<name>-hwd`, and the platform registers
two CTest cases. For a µTest++ test, copy `mtp-args` (smallest), `mtp-example`
(runtime), or `mtp-smp` (multi-core, one file per suite).

If the test needs extra sources, includes, defines, a linker script or a
different CPU count, add a branch to the hooks in
`test/rpi-zero-2w/tests.cmake` (`board_test_sources`, `board_test_includes`,
`board_test_defines`, `board_test_linker`, `board_test_ncpu`,
`board_test_libs`). Those hooks are the board's decision; a directory listing
cannot express them.

## 12.3 Selection

`scaffold.json` records the selected tests. `tests/run.sh` embeds their exact
CTest names in `SELECTED`. To add a test to the default run, re-scaffold with
the selection (or edit `scaffold.json` and re-run the scaffolder); it is not a
hand-maintained tab file anymore.

---

# 13. Command and file cheat sheet

## 13.1 Commands

```bash
cd /home/dan/Work/micro-os-scaffold-ctest-examples/rpi-zero-2w-aarch64/tests

./run.sh list                     # what CTest has
./run.sh                          # qemu cases of the selected tests
./run.sh mtp-args                 # one test
./run.sh --hwd                    # include the board cases
PROBE=cmsis-dap ./run.sh --hwd-only
./run.sh all                      # everything, hardware included
./run.sh clean                    # remove the build tree
```

## 13.2 Where to look when something fails

| Symptom | First file |
|---|---|
| configure fails | `build-logs/<config>.configure.log` |
| compile/link fails | `build-logs/<config>.build.log` |
| a test fails under QEMU | `tests/build/<config>/platform-bin/port-tests/test/.qemu-logs/<app>.log` and `.tap` |
| a board test fails before the app | `.hw-logs/<app>.reset.log` (JTAG/reset) |
| a board test fails during the app | `.hw-logs/<app>.log` and `.tap` |
| CTest summary | `ctest.xml` |
| full CTest console | `ctest.log` |
| the whole run | `Testing/Temporary/LastTest.log` |

## 13.3 File roles at a glance

| File | Role |
|---|---|
| `scaffold.json` | selection, tools, configuration, build result |
| `tests/run.sh` | the one build/test entry point |
| `tests/scripts/env.sh` | tool paths and `UOS_*` variables |
| `tests/platforms/aarch64-rpi-zero-2w/CMakeLists.txt` | registers the CTest cases |
| `micro-os-plus-iii-aarch64/test/CMakeLists.txt` | builds each app twice |
| `micro-os-plus-iii-smp/cmake/uos-app.cmake` | one executable from sources |
| `micro-os-plus-iii-aarch64/test/rpi-zero-2w/tests.cmake` | per-test hooks + the µTest++ block |
| `micro-os-plus-iii-aarch64/test/boards/rpi-zero-2w/src/startup.S` | the first instructions |
| `micro-os-plus-iii-aarch64/test/boards/rpi-zero-2w/linker.ld` | memory, heap, `.init_array` |
| `micro-os-plus-iii-aarch64/test/boards/rpi-zero-2w/hw.sh` | board runner entry |
| `micro-os-plus-iii-smp/test_smpl/run-qemu.sh` | QEMU runner |
| `micro-os-plus-iii-smp/test_smpl/run-hw.sh` | OpenOCD runner |
| `micro-os-plus-iii-aarch64/test/mtp/mtp_board.cpp` | runner, console, report, cores |
| `micro-os-plus-iii-aarch64/test/mtp/mtp_semihosting.hpp` | command line and file syscalls |
| `micro-os-plus-iii-aarch64/test/mtp/mtp_startup.hpp` | carried `main` and startup hooks |

---

## 13.4 Moving or renaming the workspace

The workspace can be renamed. `tests/scripts/env.sh` and `tests/run.sh`
find the root from their own location, and CMake uses the sibling paths.

A build tree copied or moved with the workspace is detected: its
`CMakeCache.txt` still holds the old absolute folders, and `tests/run.sh`
compares `CMAKE_CACHEFILE_DIR` and `CMAKE_HOME_DIRECTORY` with the current
ones, removes the stale tree and configures again. The first build after a
move is therefore a clean build; no manual `clean` is required.

Two things stay absolute: the tool paths in `tests/scripts/env.sh` (same
machine, they work; another machine, re-scaffold or override
`UOS_TOOL_<NAME>`) and the metadata in `scaffold.json` (`destination`, the
µTest++ source path), which the build does not use.

# 14. The shortest possible summary

1. CMake compiles the kernel, the port and one test directory into two
   executables: `-qemu` and `-hwd`.
2. CTest runs a shell script for each executable.
3. The script loads the image into QEMU or onto the board, and waits for
   `RESULT: PASS`.
4. The image starts in `startup.S`, runs the static constructors, reaches
   `main`, starts the scheduler, and runs `os_main` on the main thread.
5. A µTest++ `os_main` installs the console, releases the cores, parses the
   command line, runs the self-registering static suites, writes the TAP
   report, prints `RESULT`, and exits through semihosting.
6. The shell script sees the result and exits 0 or 1; CTest records it in
   `ctest.xml` and `ctest.log`.
