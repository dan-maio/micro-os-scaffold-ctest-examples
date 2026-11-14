# How the µOS++ test harness works

<p class="subtitle">A plain-English guide to the folders, files and execution flow shared by every board and port: from a test directory to a CTest result</p>

This document lives in the root of `tests/`. It describes the **test harness**
in general, not one board. The Raspberry Pi Zero 2 W workspace is used as an
example when a concrete path helps, but the same structure applies to the
Cortex-M boards, the other AArch32/AArch64 boards and the POSIX host port.

The document answers three questions:

1. **What is where?** — the folders and files, and which layer owns each.
2. **How does a test become a CTest case?** — the CMake path from a directory
   to an executable and to an `add_test()` entry.
3. **What happens when CTest runs it?** — the runner, the image start-up, the
   test body, the `RESULT` line, the report file and the exit code.

---

# 1. The shape of a generated workspace

A scaffolded workspace contains three siblings:

```text
<workspace>/
├── micro-os-plus-iii-smp/       the kernel
├── micro-os-plus-iii-<port>/    one or more ports
├── tests/                       the harness (this directory)
├── third_party/
│   └── micro-test-plus/         µTest++ (when --micro-test-plus was used)
├── scaffold.json                the selection and the tool paths
└── build-logs/                  configure/build logs
```

The kernel and the ports are **normal source trees**. The harness is a
separate CMake project that:

- includes the selected port;
- asks the port to build the tests that live in its `test/<board>/` directory;
- registers the resulting executables with CTest;
- drives QEMU, OpenOCD or the host through small runner scripts.

There are no pre-built libraries. The kernel and the port export CMake
INTERFACE targets; their sources are compiled into every test executable.

---

# 2. The `tests/` directory

```text
tests/
├── CMakeLists.txt                 the harness CMake project
├── cmake/
│   ├── tests-main.cmake           selects the port, adds the platform
│   ├── common-options.cmake       common compile/link options
│   └── global-definitions.cmake
├── platforms/
│   └── <platform>/                one directory per selectable platform
│       ├── CMakeLists.txt         registers the CTest cases
│       ├── cmake/
│       │   ├── definitions.cmake
│       │   ├── dependencies-folders.cmake
│       │   └── platform-library.cmake
│       └── include/               the platform header for harness suites
├── sources/                       portable harness suites (when selected)
│   └── <suite>/
├── device-qemu-cortexm/           generic Cortex-M QEMU device (when selected)
├── xpacks/                        build-helper and the selected libraries
├── run.sh                         the one build/test entry point (generated)
├── scripts/env.sh                 tool paths and UOS_* variables (generated)
├── build/<configuration>/         created by the build
└── ARCHITECTURE.md/.pdf           this document
```

Everything except `build/` and the two generated scripts is ordinary source;
`tests/` is checked in (or copied) as part of the workspace.

## 2.1 `tests/CMakeLists.txt`

The top-level project:

- enables C and C++ (and ASM for bare-metal ports);
- enforces the language standards;
- calls `enable_testing()`;
- loads the build-helper;
- includes `cmake/global-definitions.cmake`;
- includes `cmake/tests-main.cmake`.

## 2.2 `tests/cmake/tests-main.cmake`

This is the "which library is under test?" file:

1. includes the platform's `definitions.cmake` and
   `dependencies-folders.cmake`;
2. `add_subdirectory()`s the selected port (for example
   `micro-os-plus-iii-aarch64` or `micro-os-plus-iii-cortexm`);
3. `add_subdirectory("platforms/<platform>")`.

The port brings the kernel (`micro-os-plus::iii`) and the devices. The harness
does not add the kernel separately: both the kernel and the port define the
same alias, and adding it twice would fail.

## 2.3 `tests/platforms/<platform>/`

A platform is one selectable test target: a board (or a QEMU machine, or the
native host). It carries:

| File | Role |
|---|---|
| `CMakeLists.txt` | registers the CTest cases and the commands that run them |
| `cmake/definitions.cmake` | platform name, `BOARD`, dependencies |
| `cmake/dependencies-folders.cmake` | the harness suites and libraries to add |
| `cmake/platform-library.cmake` | the `micro-os-plus::platform` INTERFACE target |
| `include/cmsis-plus/platform.h` | the header portable suites include |

Some platforms add a `src/platform-support.cpp` for the harness suites: a
strong `main()` and the startup hooks. The port's own board tests carry their
own start-up, so they do not need it.

## 2.4 `tests/sources/<suite>/`

Portable harness suites (`rtos-apis`, `mutex-stress`, `cmsis-os-validator`,
...) live here, not in a port. They are INTERFACE libraries:

```text
tests/sources/<suite>/
├── CMakeLists.txt        add_library(test::<suite> INTERFACE ...)
├── include/              suite headers, cmsis-plus/os-app-config.h
└── *.cpp                 the suite implementation
```

A board test links one through its `board_test_libs()` hook. A lean workspace
that selected no such test does not contain `tests/sources/`.

## 2.5 `tests/xpacks/`

The xPack dependencies the selected build needs: `build-helper` (CMake helpers
and toolchain files), and the libraries the harness suites link (`arm-cmsis`,
`arm-cmsis-core`, `chan-fatfs`, `libucontext`, ...). They are copied so the
workspace builds without `xpm install` at test time.

## 2.6 `tests/run.sh` and `tests/scripts/env.sh`

`run.sh` is the single entry point. It is generated from `scaffold.json` and
contains the configuration table and the selected CTest names. `env.sh` is the
generated tool-path file: `UOS_TOOL_<NAME>`, `UOS_TOOLCHAIN_FILE_<ARCH>`,
`UOS_LIB_<NAME>`, `SCAFFOLD_ROOT`.

---

# 3. The test side: where the tests live

The test source is not in `tests/`; it is in the **port**:

```text
<port>/test/
├── CMakeLists.txt              builds every <board>/<app> twice
├── boards/
│   └── <board>/                board support: startup, linker, probe, hw.sh
└── <board>/
    ├── tests.cmake             the per-test hooks
    ├── include/                headers shared by this board's tests
    ├── src/                    sources shared by this board's tests
    ├── <app-1>/                one test application
    ├── <app-2>/
    └── ...
```

The **board directory** describes the hardware; the **test directory**
contains the applications. A test is a directory with at least one `.cpp`
file. There is no central list of tests: a directory is a test.

## 3.1 The port builder: `<port>/test/CMakeLists.txt`

It:

1. includes the board's `board.cmake` (CPU flags, linker scripts, core count,
   capabilities);
2. includes the board's `tests.cmake` (the hooks);
3. globs `<port>/test/<board>/*` and keeps the directories that contain at
   least one `.cpp`;
4. for every application, calls `uos_add_app()` once per variant.

The variants are decided by the board:

- a board with `UOS_BOARD_LINKER_QEMU` gets `qemu` and `hwd`;
- a board without it gets only `hwd`;
- the POSIX host gets `host`.

The `qemu` and `hwd` images are compiled from the **same sources**. Only the
defines differ (`QEMU_BUILD` or `HW_BUILD`).

## 3.2 `uos_add_app()`: one executable

`micro-os-plus-iii-smp/cmake/uos-app.cmake` is the one helper that turns a
test directory into an executable:

```cmake
uos_add_app(<app>-<variant>
  SOURCES       <the test's *.cpp> <shared board-test sources>
  NCPU          4
  LINKER_SCRIPT <board linker>
  DEFINES       TRACE SEMIHOST ... QEMU_BUILD|HW_BUILD
  INCLUDES      <test dir> <board test include> <port include>
  LIBRARIES     <port> <extra test libraries>
  OUTPUT_NAME   <app>-<variant>)
```

It compiles the kernel and the port **from source** into the executable,
applies the common bare-metal options (`-nostartfiles`, `-Wl,--gc-sections`, a
map file), and for a raw-binary target it also `objcopy`s a `.bin`.

The executable name is the CTest name suffix: `<app>-<variant>`.

## 3.3 `tests.cmake`: the per-test hooks

A board's `tests.cmake` holds only what a directory listing cannot express:

| Hook or list | Use |
|---|---|
| `BOARD_TEST_NEED_DEVICES` | tests that link the board's driver set |
| `BOARD_TEST_SELF_CONTAINED` | tests that must not get the shared `src/` |
| `BOARD_TEST_NO_KERNEL` | bare-metal probes that link no kernel |
| `BOARD_TEST_HWD_ONLY` | tests that have no `qemu` image on a board that has one |
| `board_test_defines(_app _out)` | extra compile definitions |
| `board_test_sources(_app _out)` | extra `.c`/`.S`/`.cpp` files |
| `board_test_includes(_app _out)` | extra include directories |
| `board_test_options(_app _out)` | extra compile options for the test's own sources |
| `board_test_linker(_app _out)` | an alternate linker script |
| `board_test_ncpu(_app _out)` | a per-test `OS_NCPU` |
| `board_test_libs(_app _out)` | extra libraries, including `test::<suite>` |

The names of the hooks are not identical in every port (for example
`board_test_libraries` on the POSIX port); the board's own `tests.cmake` uses
the names its port's builder declares.

## 3.4 What a test looks like

There are two families.

**Classic test** — one `main.cpp`, its own `os_main`, its own start-up hooks
where the board needs them. It prints a `RESULT:` line and calls
`hw_result::ok()`/`fail()`. Example: `smp-mat-test` on the AArch ports, the
SPI-flash tests on the WeAct board.

**µTest++ test** — `main.cpp` plus one file per suite. `main.cpp` is only
`start` + `run`; each suite registers itself. Example: `mtp-args`,
`mtp-example`, `mtp-smp`.

Both are ordinary test directories to CMake. CTest does not know which family
it is running; it only runs the executable through a runner and checks the
result.

---

# 4. How a test becomes a CTest case

## 4.1 The CMake path

```text
tests/CMakeLists.txt
  └── cmake/tests-main.cmake
        ├── include platforms/<platform>/cmake/definitions.cmake
        ├── add_subdirectory(<port>)
        └── add_subdirectory(platforms/<platform>)
              ├── include cmake/platform-library.cmake
              ├── add_subdirectory(<port>/test ...)
              │     └── for each <board>/<app>:
              │           uos_add_app(<app>-qemu)
              │           uos_add_app(<app>-hwd)
              └── for each <app>:
                    add_test(NAME <platform>-<app>-qemu ...)
                    add_test(NAME <platform>-<app>-hwd  ...)
```

The platform CMake lists the **same directories** the port builder found. It
does not compile anything itself; it only names the tests and the commands
that run them.

## 4.2 The naming rule

```text
<platform>-<app>-<variant>
```

Examples from different ports:

```text
aarch64-rpi-zero-2w-mtp-args-qemu
aarch64-rpi-zero-2w-mtp-args-hwd
aarch64-rpi4b-mtp-smp-qemu
cortexm-weactf411-flash-id-hwd
native-mtp-example-host
```

- **platform** — the directory name under `tests/platforms/`;
- **app** — the directory name under `<port>/test/<board>/`;
- **variant** — `qemu`, `hwd` or `host`.

## 4.3 The commands

### QEMU variant

```cmake
add_test(NAME "${PLATFORM_NAME}-${_app}-qemu"
  COMMAND bash "${_runner}/run-qemu.sh" "${_port_bin}" "${_qemu}" -M ... -smp ...)
set_tests_properties(... PROPERTIES
  ENVIRONMENT "UOS_QEMU_ONLY=${_app}" LABELS "qemu" TIMEOUT ...)
```

`run-qemu.sh` is in the kernel copy (`test_smpl/`). The platform supplies the
QEMU binary and the machine arguments. `UOS_QEMU_ONLY` tells the runner to run
exactly one app, because CTest calls it once per case.

### Hardware variant

```cmake
add_test(NAME "${PLATFORM_NAME}-${_app}-hwd"
  COMMAND bash "${_board_hw}" "${_app}" 600)
set_tests_properties(... PROPERTIES
  ENVIRONMENT "BUILD=${CMAKE_CURRENT_BINARY_DIR}/port-tests"
  LABELS "hwd" TIMEOUT ...)
```

The command is the board's `hw.sh`. Its details differ per board (OpenOCD
config, probe, reset, load address), but its contract is the same: run one
application and exit 0 only if it passed.

### Host variant

```cmake
add_test(NAME "${PLATFORM_NAME}-${_app}-host"
  COMMAND bash "${_runner}/run-host.sh" "${_port_bin}")
set_tests_properties(... PROPERTIES
  ENVIRONMENT "UOS_RUN_ONLY=${_app}" LABELS "host" ...)
```

The POSIX port builds `<app>-host`; `run-host.sh` runs the executable and
parses its `RESULT` line.

## 4.4 Why CTest only sees an exit status

CTest does not know about semihosting, UARTs, JTAG probes or QEMU. It runs a
command and checks its exit status. The runners are the adapters:

```text
RESULT: PASS  -> runner exit 0
RESULT: SKIP  -> runner exit 3 (CTest may treat it as a skip)
RESULT: FAIL  -> runner exit 1
timeout / no RESULT -> runner exit 1
```

That is the whole integration. The TAP report, the JUnit file and the logs are
for people and CI tools, not for CTest's pass/fail decision.

---

# 5. CMake in more detail

## 5.1 The build directory

```text
tests/build/<configuration>/
├── CMakeCache.txt
├── build.ninja
├── platform-bin/
│   └── port-tests/
│       └── test/
│           ├── <app>-qemu.elf/.bin/.map
│           ├── <app>-hwd.elf/.bin/.map
│           └── <app>-hwd            (a no-extension companion)
├── ctest.xml                        after a run
├── ctest.log                        after a run (copied by run.sh)
└── Testing/Temporary/LastTest.log   CTest's own full log
```

The exact `platform-bin` layout is the port's choice; the runner is told where
to find the executables through the CTest environment (`BUILD=...` for
hardware, the first argument for QEMU).

## 5.2 The two variants

| Define | Meaning |
|---|---|
| `QEMU_BUILD` | this image is intended for the emulator |
| `HW_BUILD` | this image is intended for real silicon |

The test sources are identical. Board code may branch: a board whose UART is
on a physical pin may keep the UART quiet under QEMU; a board whose QEMU
machine does not model a controller may return `mtp_board::skip("...")` in the
QEMU image; a board with a different linker script for QEMU uses it.

## 5.3 The CTest labels and timeouts

Labels are `qemu`, `hwd` and `host`. `ctest -L hwd` runs only the board cases,
`ctest -LE hwd` excludes them. Timeouts are per platform and per runner; the
hardware timeout must cover the probe reset and the test, the QEMU timeout the
emulator and the test.

---

# 6. `tests/run.sh`: the one entry point

`run.sh` is generated from `scaffold.json`; it knows the configuration
(name, platform, build type, toolchain) and the selected CTest names.

```bash
cd tests
./run.sh                  # configure, build, run the selected tests
./run.sh build            # configure and build only
./run.sh list             # list the registered cases (* = selected)
./run.sh all              # build, then every case (hardware included)
./run.sh clean            # remove the build tree
```

Options:

```text
-c, --config <name>   choose a configuration
--hwd                 include the hardware cases
--hwd-only            only the hardware cases
--all                 every registered case
-R, --regex <re>      ad-hoc CTest regular expression
--no-build            do not build first
-v                    verbose CTest output
```

The default run excludes `-hwd` cases, because they need a board and a probe.
If the selected tests are hardware-only, `run.sh` says so and runs them; it
does not silently do nothing.

A build tree copied or moved with the workspace has absolute paths in its
CMake cache. `run.sh` compares `CMAKE_CACHEFILE_DIR` and
`CMAKE_HOME_DIRECTORY` with the current folders; on a mismatch it removes the
stale tree and configures afresh, so a renamed workspace builds without a
manual `clean`.

The selection can name:

- an application directory (`mtp-smp`, `flash-id`, `smp-mat-test`);
- a full CTest name
  (`aarch64-rpi-zero-2w-mtp-smp-qemu`);
- a selection id recorded in `scaffold.json` (for example `3.1.12`).

## 6.1 What it writes

```text
build/<configuration>/ctest.xml    CTest JUnit summary
build/<configuration>/ctest.log    full CTest console
```

CTest itself writes `Testing/Temporary/LastTest.log`; `run.sh` copies it to
`ctest.log` because the JUnit file keeps only a 1 KiB snippet of each test's
console (the XML says so).

The complete test report is not the JUnit file: it is the framework's own
report, next to the runner log:

```text
.qemu-logs/<app>.tap      QEMU
.hw-logs/<app>.tap        board
.host-logs/<app>.tap      host
```

The paths are relative to the runner's build directory; the exact root is
printed by the failure messages and is easy to find under
`tests/build/<configuration>/`.

---

# 7. The execution flow, generally

The same pattern applies to every port. The details of `startup.S` or
`startup.cpp` differ; the phases do not.

## 7.1 Phase 1 — the loader

**QEMU:** the runner starts QEMU with the machine, the CPU count, the raw
image and a semihosting configuration. QEMU places the image at the entry
address and starts the cores.

**Hardware:** the board's runner talks to the probe (OpenOCD), resets or
halts the board, enables semihosting, sets the command line, loads the ELF
into RAM and resumes the boot core at the entry address.

**Host:** the runner executes the host binary directly.

## 7.2 Phase 2 — the start-up code

Every port has a start-up file that runs before `main()`:

```text
set the stack(s)
set the exception vectors
zero .bss / copy .data
initialise the clock and the UART (board hooks)
initialise the free store over the linker's heap
run __libc_init_array()   (C++ static constructors)
call main()
```

The **order** is part of the test contract: the static constructors may
allocate, so the heap must be ready before `__libc_init_array()`; the linker
script must keep `.init_array`; the heap symbols (`_Heap_Begin`,
`_Heap_Limit`) must exist and `sbrk` must use the same region.

For µTest++, the static constructors also create the `mt::static_suite`
objects. They register themselves; the runner discovers them later.

## 7.3 Phase 3 — `main()`

The port's start-up calls `main()`. What `main()` does depends on the port and
the test family:

**AArch32 / AArch64 µTest++:** the test includes `mtp_startup.hpp`, which
provides `main()`. It reads the command line with `SYS_GET_CMDLINE`, sets up
the interrupt stack, initialises the scheduler, creates the “main” thread and
starts the scheduler. The main thread calls `os_main(argc, argv)`.

**Cortex-M µTest++:** the kernel's `main(argc, argv)` is used. A strong
`os_startup_initialize_args()` in the board support reads the semihosting
command line; the kernel creates the main thread and calls
`os_main(argc, argv)`.

**Classic test:** the test may carry its own `main()` (as the AArch boards do)
or use the kernel's; either way it calls its own `os_main`.

**POSIX host:** the process `main()` receives the process arguments; the
start-up calls `os_main`.

## 7.4 Phase 4 — `os_main`

A µTest++ `os_main` is typically:

```cpp
int
os_main (int argc, char* argv[])
{
  mtp_board::start ("<test>");
#if defined(MTP_HAS_SMP_BOOT) && MTP_HAS_SMP_BOOT
  mtp_board::release_cores ();
#endif
  return mtp_board::run (argc, argv);
}
```

- `start()` installs the console and prints the banner;
- `release_cores()` starts the secondaries where the board can;
- `run()` parses the command line, initialises the TAP reporter, runs the
  static suites and finishes.

A classic `os_main` does its own work and prints its own `RESULT` line.

## 7.5 Phase 5 — the suites and the checks

For µTest++, `run_suites()` runs the static suites sorted by name. Each suite
body creates test cases; each test case runs its checks:

```cpp
ts.test ("name", [] (auto& t) {
  t.expect (mt::eq (value, expected)) << "what the check means";
});
```

A failed `expect` is recorded and the test case continues. A failed `require`
would stop the case; this build has no exceptions, so tests use `expect`.
Nested test cases run on the caller's stack.

## 7.6 Phase 6 — the verdict

The test prints:

```text
RESULT: PASS (...)
```

or

```text
RESULT: FAIL (...)
```

or, for a test that cannot run in this environment:

```text
RESULT: SKIP (reason)
```

The runner greps for these lines. The framework's `finish()` prints the line
and calls `hw_result::ok()`/`fail()`; that is `SYS_EXIT` on the boards and
QEMU. On the host, the process exit code is used instead.

## 7.7 Phase 7 — the runner and CTest

The runner watches the log, sees the result, and exits 0, 1 or 3. CTest
records the case in `ctest.xml` and `ctest.log`. The complete report is in the
`.tap` file next to the runner log.

---

# 8. The runners

The runners are shared by the ports; they live in
`micro-os-plus-iii-smp/test_smpl/`.

## 8.1 `run-qemu.sh`

```text
run-qemu.sh <build-test-dir> <qemu> <machine-args...>
```

- finds the `*-qemu.bin` images;
- optionally filters one app (`UOS_QEMU_ONLY`);
- starts QEMU with `-nographic -serial none`;
- passes the semihosting configuration, including the command line;
- captures the log, waits, and greps `RESULT`;
- exits 0/1/3.

The console in a QEMU run is semihosting, not a serial port: `-serial none`.

## 8.2 `run-hw.sh`

```text
run-hw.sh <build-test-dir> <app> [run-seconds]
```

- selects the probe configuration from the board's environment
  (`UOS_HW_CFG`);
- optionally resets the board through a board-specific `UOS_HW_PRELOAD`;
- starts OpenOCD, `init`, halts the cores;
- enables semihosting;
- sets `arm semihosting_cmdline` on the boot core;
- loads the ELF;
- resumes the boot core; the other cores are parked until the kernel releases
  them;
- watches the log for `RESULT`;
- exits 0/1/3.

The board's `hw.sh` fills in the board-specific variables and calls this
runner. Boards whose hardware differs (reset sequence, boot address, core
count, probe) change their `hw.sh`, not the shared runner.

## 8.3 `run-host.sh`

```text
run-host.sh <build-test-dir>
```

Runs each `*-host` executable as an ordinary process, captures its log, greps
`RESULT`, and exits 0/1/3. The host has a real process exit status, but the
`RESULT` line keeps the same contract as the boards.

## 8.4 The board `hw.sh` contract

Each board has its own `hw.sh`; some run OpenOCD directly (the STM32 boards),
some delegate to `run-hw.sh` (the AArch boards). Whichever it is, it must:

- accept the application name;
- locate the `<app>-hwd` ELF under `$BUILD/test`;
- run exactly one test;
- set the semihosting command line for `mtp-*` tests;
- return 0 only when the test printed `RESULT: PASS`.

The UART is **not** opened by the runner: the person at the bench owns
`/dev/ttyACM0`. The runner reads OpenOCD's semihosting log.

---

# 9. Semihosting and the command line

Semihosting is how a bare-metal image asks the debugger or emulator to do
something on the host. On the ports used here:

| Core | Trap |
|---|---|
| AArch64 | `hlt #0xF000` |
| AArch32 | `hlt` (Pi, OpenOCD `aarch64` target) or `svc 0x123456` |
| Cortex-M | `bkpt 0xAB` |

The operations the harness relies on:

```text
SYS_GET_CMDLINE   read argc/argv
SYS_OPEN          open the report file
SYS_WRITE         write the report
SYS_CLOSE         close the report
SYS_EXIT          stop the run with PASS/FAIL
```

The command line is one string. The runner forms it as:

```text
OpenOCD:  arm semihosting_cmdline <app> --output-file <path>
QEMU:     -semihosting-config enable=on,target=native,
            arg=<app>,arg=--output-file,arg=<path>
host:     <app> --output-file <path>
```

The target splits it into `argc`/`argv`. The µTest++ support reads it in
`mtp_startup.hpp` (AArch) or in the strong `os_startup_initialize_args()`
(Cortex-M). The `mtp-args` starter test is the self-check: it asserts that
`argc >= 3`, that `argv[0]` names the test, that `argv[1]` is
`--output-file`, and that `argv[2]` ends in `.tap`.

The report file is opened with `SYS_OPEN` and written with `SYS_WRITE`. In the
µTest++ support, `-Wl,--wrap=fopen` redirects the reporter's `fopen` to that
semihosting stream, so the framework needs no board-specific `_open()`.

---

# 10. The two test families in the CTest view

| | Classic | µTest++ |
|---|---|---|
| Files in the test dir | `main.cpp` (+ helpers) | `main.cpp` + one `suite-*.cpp` per suite |
| Suite list | none | none; each file registers a `mt::static_suite` |
| Output | the test's own lines + `RESULT` | TAP + `RESULT` |
| Report file | none | `--output-file <path>` (TAP) |
| Command line | may be ignored | read by the support; `mtp-args` checks it |
| Verdict seen by CTest | `RESULT` line via the runner | same |
| External report | CTest log / JUnit | `.tap` + CTest log / JUnit |

CTest treats them identically. The difference is inside the executable.

## 10.1 The µTest++ board support

The support lives in `<port>/test/mtp/` and is linked into every `mtp-*` test:

- `mtp_board.hpp/.cpp` — runner, console, report, RESULT, cores, spinlock,
  atomics;
- `mtp_semihosting.hpp` — raw semihosting;
- `mtp_startup.hpp` — the carried startup (AArch boards);
- `README.md` — the support's notes.

The CMake block that links it is at the end of the board's `tests.cmake`. It
adds the framework sources, the support source, the include paths, the
`MTP_*` defines and `-Wl,--wrap=fopen`. Every `mtp-*` directory gets it
through the `board_test_libs()` hook.

## 10.2 Portable harness suites

A suite such as `rtos-apis`, `mutex-stress` or `cmsis-os-validator` lives in
`tests/sources/<suite>/` and is built as `test::<suite>`. The board has a
test directory (often a placeholder) with the same name, and its
`tests.cmake` returns `test::<suite>` from `board_test_libs()`. The platform
also supplies the suite's `os-app-config.h` and, where needed, a strong
`main()`/startup hooks. The resulting CTest case is still
`<platform>-<app>-<variant>`.

---

# 11. Examples across boards

## 11.1 A Cortex-M board with only hardware: WeAct F411CE

- `BOARD_TEST` tests build only `hwd`; there is no QEMU image.
- CTest cases: `cortexm-weactf411-<app>-hwd`.
- The board's `hw.sh` runs OpenOCD directly: `program`, `arm semihosting
  enable`, `arm semihosting_cmdline`, `reset run`.
- The test links the kernel's Cortex-M port; the kernel's startup calls
  `main(argc, argv)`, and the strong `os_startup_initialize_args()` reads the
  command line.
- A µTest++ test writes `<app>.tap` in `.hw-logs/`.

## 11.2 A multi-core AArch64 board with QEMU and hardware: RPi 4 B

- CTest cases: `aarch64-rpi4b-<app>-qemu` and `...-hwd`.
- QEMU runs `raspi4b`, `-smp 4`, with semihosting.
- Hardware uses `run-hw.sh` and a CMSIS-DAP/J-Link probe; the board's
  `hw.sh` supplies the BCM2711 facts and the reset sequence.
- `mtp-smp` runs the same ten suites on both variants; the speed-up floor is
  only checked on hardware.

## 11.3 The native host: POSIX port

- CTest cases: `native-<app>-host`.
- No linker script, no QEMU, no probe.
- `run-host.sh` executes the process; the process's `main(argc, argv)` is the
  test's `os_main` path.
- `mtp-args` verifies the process arguments; the report file is an ordinary
  host file.

## 11.4 A board whose QEMU image replaces the board

Some boards (for example the Pico 2) build their QEMU image against a generic
device, not the real board. Their board tests can be marked
`BOARD_TEST_HWD_ONLY`; the `mtp-*` starters are then hardware-only, because
the runtime rule was checked on the board, not on the emulated stand-in.

---

# 12. Adding a test, a suite or a platform

## 12.1 A new test

Create a directory under `<port>/test/<board>/<name>/` with at least one
`.cpp`. The port builder finds it; the platform registers it; the CTest name
follows `<platform>-<name>-<variant>`.

If the test needs extra sources, includes, defines, a linker script or a
different CPU count, add a branch to the board's `tests.cmake` hooks. Do not
edit the shared builder for one test.

## 12.2 A new suite in a µTest++ test

Add a `suite-<name>.cpp` next to the existing suite files:

```cpp
#include <mtp_board.hpp>

mt::static_suite suite_my_check{
  "10 - My check", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("a check", [] (auto& t) {
      t.expect (mt::eq (value, expected));
    });
  }
};
```

The file is globbed, the suite registers itself, and the runner runs it sorted
by name. No `main.cpp` or CMake change.

## 12.3 A new platform

Copy `tests/platforms/<similar>/`, adjust `cmake/definitions.cmake` (notably
`BOARD`), `CMakeLists.txt` (the runner command and the machine args), and add
the configuration to `scaffold.json`. Re-run the scaffolder.

## 12.4 A new board

Add `<port>/test/boards/<board>/` with `board.cmake`, the startup/linker/probe
files and `hw.sh`; add `<port>/test/<board>/` with its `tests.cmake` and its
test directories. The board's `board.cmake` selects the CPU, the core count,
the linker scripts and the capabilities.

---

# 13. Why `tests/` looks the way it does

- **One harness, many ports.** The harness is shared; the port owns the
  tests. That is why the test source is under `<port>/test/`, not under
  `tests/`.
- **No test list.** A test is a directory. The globs use `CONFIGURE_DEPENDS`,
  so adding or removing a directory is noticed by the next build.
- **Two variants from one source tree.** QEMU and hardware builds share the
  sources; the defines select the environment. This keeps the test honest:
  the hardware build is not a different implementation.
- **CTest only sees exit codes.** The runners are the only place that knows
  about semihosting, probes and QEMU. CTest stays portable.
- **The report is a file.** The `.tap` file is complete; the JUnit file is a
  summary with a 1 KiB console snippet; the full console is `ctest.log`.

---

# 14. Where to look when something fails

| Symptom | First place |
|---|---|
| Configure fails | `build-logs/<config>.configure.log` |
| Compile/link fails | `build-logs/<config>.build.log` |
| A QEMU test fails | `.qemu-logs/<app>.log` and `.tap` under the runner build dir |
| A board test fails before the app | `.hw-logs/<app>.reset.log` (JTAG/reset) |
| A board test fails during the app | `.hw-logs/<app>.log` and `.tap` |
| The test runs but CTest says pass/fail wrongly | the runner's grep for `RESULT` |
| The report file is missing | `--output-file` in the runner command; `__wrap_fopen`; `SYS_OPEN` |
| `mtp-args` fails | the runner's command line; `UOS_SEMIHOST_CMDLINE`; `argc`/`argv` at `os_main` |
| CTest summary | `ctest.xml` |
| Full CTest console | `ctest.log` |

---

# 15. A short glossary

| Term | Meaning |
|---|---|
| **kernel** | `micro-os-plus-iii-smp`: scheduler, threads, RTOS objects |
| **port** | `micro-os-plus-iii-aarch64`, `-cortexm`, `-posix-arch`, ...: architecture + SoC + board support |
| **board** | one hardware target, under `<port>/test/boards/<board>/` |
| **platform** | one selectable test configuration, under `tests/platforms/<platform>/` |
| **test** | one application directory under `<port>/test/<board>/<app>/` |
| **variant** | `qemu`, `hwd` or `host` |
| **runner** | the shell script CTest executes: `run-qemu.sh`, `run-hw.sh`, `run-host.sh`, a board `hw.sh` |
| **TAP** | Test Anything Protocol, the µTest++ report format |
| **semihosting** | the target's trap into the debugger/emulator for I/O, command line and exit |
| **RESULT line** | `RESULT: PASS|FAIL|SKIP`, the runner's contract |
| **CTest case** | one `<platform>-<app>-<variant>` entry |

---

# 16. The shortest possible summary

1. The port owns the test directories; the harness owns CTest.
2. `tests/CMakeLists.txt` includes the port and the platform.
3. The port's builder compiles each test once per variant (`qemu`, `hwd`,
   `host`) with `uos_add_app()`.
4. The platform CMake registers each executable as a CTest case named
   `<platform>-<app>-<variant>`.
5. CTest runs a runner script; the script loads/runs the image and looks for
   `RESULT: PASS`.
6. The image starts in the port's start-up code, runs the static
   constructors, reaches `main`, starts the scheduler and runs `os_main`.
7. The test runs its checks, writes the report, prints `RESULT` and exits.
8. The runner returns 0/1/3; CTest records it in `ctest.xml` and `ctest.log`;
   the complete µTest++ report is the `.tap` file.
