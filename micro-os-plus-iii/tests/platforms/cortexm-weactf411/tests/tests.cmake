# -----------------------------------------------------------------------------
# What this board's tests need.
#
# mos-test1 and spi-pipeline are the board's own tests and keep the defaults:
# one CPU, the kernel's non-SMP branch.
#
# The three harness suites run here under the kernel's SMP scheduler at
# OS_NCPU=1: the SMP code paths (per-CPU current thread and lock state, the
# affinity-aware picker, the stack_ptr liveness rule) on one Cortex-M4F, at
# the board's 100 MHz (HSE 25 MHz -> PLL, os-hooks.c). The port's upstream
# core carries that branch (src/rtos/os-core.cpp, gated on
# OS_USE_SMP_SCHEDULER); its kernel lock is interrupt masking with an
# owner/depth record, since one CPU has no other core to exclude. The suites
# use neither the LED (PC13) nor the SPI NOR flash; their verdict goes out on
# semihosting and on USART1 (PA9/PA10).
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# The harness suites.
#
# Upstream's suites (tests/sources/<suite>/, test::<suite>), built by the
# tests' builder from the platform's UOS_TESTS_SUITES on the platform's
# start-up: <platform>-test-startup (the end of the run is
# board-tests/cortexm/test-end.cpp, the RESULT line through this platform's
# include/test-end-console.h; -Wl,--wrap=os_main) and <platform>-test-suite
# (the suite's own os-app-config.h, UOS_HARNESS_SUITE). The names below are
# still this board's knobs (single core, defines).
# -----------------------------------------------------------------------------
set (_harness_suites rtos-apis mutex-stress cmsis-os-validator)

function (board_test_defines _app _out)
  # local tests (micro-os-scaffold local tests): tests.local.cmake may answer first.
  if (COMMAND board_test_defines_local)
    set (_local_out "")
    board_test_defines_local ("${_app}" _local_out)
    if (NOT "${_local_out}" STREQUAL "")
      set (${_out} ${_local_out} PARENT_SCOPE)
      return ()
    endif ()
  endif ()
  # The suites build the kernel's SMP branch. NCPU stays the board's 1, so
  # uos_add_app() defines OS_NCPU=1 and, on its own, no SMP scheduler: the
  # define is added here, for these three applications only.
  if ("${_app}" IN_LIST _harness_suites)
    set (${_out} OS_USE_SMP_SCHEDULER=1 PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_libs _app _out)
  # local tests (micro-os-scaffold local tests): tests.local.cmake may answer first.
  if (COMMAND board_test_libs_local)
    set (_local_out "")
    board_test_libs_local ("${_app}" _local_out)
    if (NOT "${_local_out}" STREQUAL "")
      set (${_out} ${_local_out} PARENT_SCOPE)
      return ()
    endif ()
  endif ()
  # µTest++ (micro-os-scaffold --micro-test-plus): every mtp-* test.
  if ("${_app}" MATCHES "^mtp-")
    set (${_out} uos-micro-test-plus PARENT_SCOPE)
    return ()
  endif ()
  set (${_out} "" PARENT_SCOPE)
endfunction ()

# ---- µTest++ (micro-os-scaffold --micro-test-plus) -------------------------------------
# Every test folder named mtp-* gets µTest++ (the framework in the
# workspace's third_party/micro-test-plus) and the board support in
# tests/mtp/, through the uos-micro-test-plus INTERFACE library linked by the
# hook above. The command line (argv[0] and --output-file) comes from
# cmake/uos-tests-run.cmake.
set (_mtp_framework "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/micro-test-plus")
set (_mtp_support "${CMAKE_SOURCE_DIR}/mtp")
if (NOT TARGET uos-micro-test-plus)
  file (GLOB _mtp_framework_sources CONFIGURE_DEPENDS "${_mtp_framework}/src/*.cpp")
  add_library (uos-micro-test-plus INTERFACE)
  target_sources (uos-micro-test-plus INTERFACE
                  ${_mtp_framework_sources} "${_mtp_support}/mtp_board.cpp")
  target_include_directories (uos-micro-test-plus INTERFACE
                              "${_mtp_framework}/include" "${_mtp_support}")
  # MTP_CONSOLE: how stdout reaches the console and the runner (mtp_board.hpp).
  # _GNU_SOURCE: the build is strict C++20, which hides fopencookie () and sbrk ().
  target_compile_definitions (uos-micro-test-plus INTERFACE
                              MICRO_OS_PLUS_MICRO_TEST_PLUS_ENABLED
                              _GNU_SOURCE
                              MTP_CONSOLE=2
                              MTP_HAS_SMP_BOOT=0
                              MTP_CARRY_STARTUP=0
                              MTP_SEMIHOST_ARGS=1
                              MTP_NANO_PRINTF=1
                              MTP_UART_INIT=1)
  # The report file: µTest++ opens --output-file with fopen (); the wrapper
  # in mtp_board.cpp turns it into a semihosting stream.
  target_link_options (uos-micro-test-plus INTERFACE "-Wl,--wrap=fopen")
  # A board whose reset code calls main (0, NULL) itself (RP2350 boot.S):
  # __wrap_main in mtp_board.cpp reads the command line from the debugger.
  target_link_options (uos-micro-test-plus INTERFACE "-Wl,--wrap=main")
  # newlib-nano: no "%zu"; the wrappers drop the 'z', _printf_float keeps "%g".
  target_link_options (uos-micro-test-plus INTERFACE
                       "-Wl,--wrap=printf,--wrap=vprintf,--wrap=fprintf"
                       "-Wl,--wrap=vfprintf,--wrap=snprintf,--wrap=vsnprintf"
                       "-Wl,-u,_printf_float")
endif ()

# ---- local tests (micro-os-scaffold local tests) ---------------------------------------
# A user-owned tests.local.cmake next to this file holds the CMake wiring of the
# tests listed in scripts/local-tests.txt (at the workspace root): any
# board_test_<hook>_local (_app _out) answers first for the tests it sets
# ${_out} for. It may also append to the lists (BOARD_TEST_HWD_ONLY, ...).
# A re-scaffold keeps it.
include ("${CMAKE_CURRENT_LIST_DIR}/tests.local.cmake" OPTIONAL)

# The hooks this file does not define consult the local twins too, then
# give the builder's default answer.
function (board_test_sources _app _out)
  # local tests (micro-os-scaffold local tests): tests.local.cmake may answer first.
  if (COMMAND board_test_sources_local)
    set (_local_out "")
    board_test_sources_local ("${_app}" _local_out)
    if (NOT "${_local_out}" STREQUAL "")
      set (${_out} ${_local_out} PARENT_SCOPE)
      return ()
    endif ()
  endif ()
  set (${_out} "" PARENT_SCOPE)
endfunction ()
function (board_test_includes _app _out)
  # local tests (micro-os-scaffold local tests): tests.local.cmake may answer first.
  if (COMMAND board_test_includes_local)
    set (_local_out "")
    board_test_includes_local ("${_app}" _local_out)
    if (NOT "${_local_out}" STREQUAL "")
      set (${_out} ${_local_out} PARENT_SCOPE)
      return ()
    endif ()
  endif ()
  set (${_out} "" PARENT_SCOPE)
endfunction ()
function (board_test_linker _app _out)
  # local tests (micro-os-scaffold local tests): tests.local.cmake may answer first.
  if (COMMAND board_test_linker_local)
    set (_local_out "")
    board_test_linker_local ("${_app}" _local_out)
    if (NOT "${_local_out}" STREQUAL "")
      set (${_out} ${_local_out} PARENT_SCOPE)
      return ()
    endif ()
  endif ()
  set (${_out} "" PARENT_SCOPE)
endfunction ()
function (board_test_options _app _out)
  # local tests (micro-os-scaffold local tests): tests.local.cmake may answer first.
  if (COMMAND board_test_options_local)
    set (_local_out "")
    board_test_options_local ("${_app}" _local_out)
    if (NOT "${_local_out}" STREQUAL "")
      set (${_out} ${_local_out} PARENT_SCOPE)
      return ()
    endif ()
  endif ()
  set (${_out} "" PARENT_SCOPE)
endfunction ()
function (board_test_ncpu _app _out)
  # local tests (micro-os-scaffold local tests): tests.local.cmake may answer first.
  if (COMMAND board_test_ncpu_local)
    set (_local_out "")
    board_test_ncpu_local ("${_app}" _local_out)
    if (NOT "${_local_out}" STREQUAL "")
      set (${_out} ${_local_out} PARENT_SCOPE)
      return ()
    endif ()
  endif ()
  set (${_out} "${UOS_BOARD_NCPU}" PARENT_SCOPE)
endfunction ()
function (board_test_qemu_libs _app _out)
  # local tests (micro-os-scaffold local tests): tests.local.cmake may answer first.
  if (COMMAND board_test_qemu_libs_local)
    set (_local_out "")
    board_test_qemu_libs_local ("${_app}" _local_out)
    if (NOT "${_local_out}" STREQUAL "")
      set (${_out} ${_local_out} PARENT_SCOPE)
      return ()
    endif ()
  endif ()
  set (${_out} "" PARENT_SCOPE)
endfunction ()
function (board_test_qemu_sources _app _out)
  # local tests (micro-os-scaffold local tests): tests.local.cmake may answer first.
  if (COMMAND board_test_qemu_sources_local)
    set (_local_out "")
    board_test_qemu_sources_local ("${_app}" _local_out)
    if (NOT "${_local_out}" STREQUAL "")
      set (${_out} ${_local_out} PARENT_SCOPE)
      return ()
    endif ()
  endif ()
  set (${_out} "" PARENT_SCOPE)
endfunction ()
function (board_test_qemu_includes _app _out)
  # local tests (micro-os-scaffold local tests): tests.local.cmake may answer first.
  if (COMMAND board_test_qemu_includes_local)
    set (_local_out "")
    board_test_qemu_includes_local ("${_app}" _local_out)
    if (NOT "${_local_out}" STREQUAL "")
      set (${_out} ${_local_out} PARENT_SCOPE)
      return ()
    endif ()
  endif ()
  set (${_out} "" PARENT_SCOPE)
endfunction ()
function (board_test_sd_posix _app _out)
  # local tests (micro-os-scaffold local tests): tests.local.cmake may answer first.
  if (COMMAND board_test_sd_posix_local)
    set (_local_out "")
    board_test_sd_posix_local ("${_app}" _local_out)
    if (NOT "${_local_out}" STREQUAL "")
      set (${_out} ${_local_out} PARENT_SCOPE)
      return ()
    endif ()
  endif ()
  set (${_out} "${UOS_BOARD_SD_POSIX}" PARENT_SCOPE)
endfunction ()
function (board_test_libraries _app _out)
  # local tests (micro-os-scaffold local tests): tests.local.cmake may answer first.
  if (COMMAND board_test_libraries_local)
    set (_local_out "")
    board_test_libraries_local ("${_app}" _local_out)
    if (NOT "${_local_out}" STREQUAL "")
      set (${_out} ${_local_out} PARENT_SCOPE)
      return ()
    endif ()
  endif ()
  set (${_out} "" PARENT_SCOPE)
endfunction ()

