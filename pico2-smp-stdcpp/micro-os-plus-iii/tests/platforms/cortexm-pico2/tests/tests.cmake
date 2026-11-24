# -----------------------------------------------------------------------------
# What this board's tests need.
#
# The board carries source sets that are NOT compiled into every application --
# see platforms/cortexm-pico2/board.cmake for why. This file says which test takes
# which, and it is a transcription of the predecessor's Makefiles, not a new
# arrangement:
#
#   every test    the board's uart.cpp and its RTOS glue
#   smp-test5     the glue only -- it defines its own uart:: in main.cpp
#   the SMP tests multicore.cpp, the bootrom core-1 launch
#   smp-test1     no multicore.cpp: it is the single-core RTOS bring-up
#   sc-test-ko    no multicore.cpp: the single-core kernel-object test
#   the USB pair  TinyUSB -- the core, one class driver, and their own
#                 tusb_config.h, which is why their include directory goes
#                 first
#   the CDC test  ws2812.cpp as well: its console is the USB, so it reports
#                 with the RGB LED
#
# smp-test1 and sc-test-ko are also the two tests that run at OS_NCPU=1 on a
# two-core board. That is not a simplification: sc-test-ko is the only test on
# this silicon that exercises the kernel's non-SMP branch, which is what makes
# it worth keeping beside its SMP twin.
# -----------------------------------------------------------------------------

set (_single_core smp-test1 sc-test-ko)

# The two applications that compile NO KERNEL. They are the bare-metal probes
# that run before a scheduler exists: smp-test0 starts the second core itself
# and checks the two can talk, exc-test asks whether exception ENTRY works at
# all. Their Makefiles list no kernel source and no port core -- nine files
# and seven files respectively -- so they link the port's bare target.
set (BOARD_TEST_NO_KERNEL smp-test0 exc-test)

# Tests that get NO `qemu` image. The USB pair needs the RP2350's USB
# peripheral, the multicore ones need the SIO block the SMP design rests on,
# and QEMU's generic Cortex-M33 machine models neither. What is left --
# exc-test, smp-test1 and sc-test-ko, the single-core bring-up and the
# kernel-object test -- builds and runs emulated.
set (BOARD_TEST_HWD_ONLY
     cmsis-os-validator-ram
     rtos-apis-ram
     mutex-stress-ram
     smp-mat-test-ram
     exc-test
     smp-mat-test
     smp-test0
     smp-test2
     smp-test3
     smp-test4
     smp-test5
     smp-test-ko
     smp-test-usb-cdc-acm
     smp-test-usb-hid
     clock-test)

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
  # The harness suites are single-core here too. On the RP2350, launching core
  # 1 is a board act the test performs (multicore::launch_core1), and the
  # harness suites never do it: their mains are the kernel's portable
  # tests/sources/<suite>/, and this board has no platform-support.cpp to
  # install the secondary idle and release it (the AArch harness platforms
  # have one). Building them at OS_NCPU=2 made them link the SMP kernel while
  # CPU 1 never ran -- `os_idle_thread_core[1]` stayed nullptr and the
  # fp-switch suite's whole cross-core claim was vacuous (migrated_rounds
  # never incremented, and the verdict does not require it). Until the board
  # supplies the launch, they run single-core, which is what they actually do.
  # The qemu variant already forces NCPU=1 in test/CMakeLists.txt.
  if ("${_app}" IN_LIST _single_core OR "${_app}" IN_LIST _harness_suites)
    set (${_out} 1 PARENT_SCOPE)
  else ()
    set (${_out} "${UOS_BOARD_NCPU}" PARENT_SCOPE)
  endif ()
endfunction ()

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

  # The kernel-less pair take the board's start-up and nothing that needs a
  # scheduler: no rtos-glue.cpp, no trace, no syscalls. smp-test0 drives both
  # cores itself, so it does take multicore.cpp; exc-test does not.
  if ("${_app}" IN_LIST BOARD_TEST_NO_KERNEL)
    set (_srcs "${UOS_BOARD_UART}")
    if (_app STREQUAL "smp-test0")
      list (APPEND _srcs "${UOS_BOARD_MULTICORE}")
    endif ()
    set (${_out} "${_srcs}" PARENT_SCOPE)
    return ()
  endif ()
  # smp-test5 brings its own uart::; every other test takes the board's.
  if (_app STREQUAL "smp-test5")
    set (_srcs "${UOS_BOARD_RTOS_GLUE}")
  else ()
    set (_srcs "${UOS_BOARD_UART}" "${UOS_BOARD_RTOS_GLUE}")
  endif ()

  if (NOT "${_app}" IN_LIST _single_core)
    list (APPEND _srcs "${UOS_BOARD_MULTICORE}")
  endif ()

  if ("${_app}" IN_LIST _usb_cdc_tests)
    list (APPEND _srcs ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_CDC_SOURCES}
                       "${UOS_BOARD_WS2812}")
  elseif ("${_app}" IN_LIST _usb_hid_tests)
    list (APPEND _srcs ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_HID_SOURCES})
  endif ()

  set (${_out} "${_srcs}" PARENT_SCOPE)
endfunction ()

# The two USB gadget tests. Both link the 4 MB script, which is why they are on
# THIS board: the predecessor built them against ld/pico2-rp2350.ld, not
# against either 16 MB map.
#
# The CDC one it called smp-test-mini-a-usb-cdc-acm, after the RP2350B-MINI-A
# it was brought up on -- the WS2812 defines below are that board's wiring
# (an RGB LED on GPIO20, where a plain Pico 2 has a single LED on GPIO25).
# Nothing else about the test is board-specific, so it is carried under the
# name of what it tests. Its `_agy` sibling is not carried: main.cpp and
# Makefile are byte-identical to this one.
set (_usb_cdc_tests smp-test-usb-cdc-acm)
set (_usb_hid_tests smp-test-usb-hid)

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
  if ("${_app}" IN_LIST _usb_cdc_tests)
    set (${_out} "${UOS_BOARD_USB_CDC_INCLUDE}" ${UOS_BOARD_USB_INCLUDE} PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _usb_hid_tests)
    set (${_out} "${UOS_BOARD_USB_HID_INCLUDE}" ${UOS_BOARD_USB_INCLUDE} PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

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
  if ("${_app}" IN_LIST _usb_cdc_tests)
    # The MINI-A's RGB LED, exactly as that Makefile declared it.
    set (${_out} PICO2_HAS_WS2812 PICO2_WS2812_PIN=20 WS2812_CLK_KHZ=150000
         PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

# The `qemu` image does not link the board at all. The RP2350's boot.S, its
# interrupt vectors and its UART are what run on real silicon; on QEMU's
# generic Cortex-M33 machine the image is the kernel plus a generic device
# (micro-os-plus::device, whose vectors-cortexm.c owns the vector table) plus
# the board's semihosting console glue under qemu/. The test's own sources are
# unchanged.
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
  set (
    ${_out}
    micro-os-plus::cortexm-qemu
    micro-os-plus::iii-startup
    micro-os-plus::iii-trace-semihosting
    micro-os-plus::iii-semihosting
    micro-os-plus::iii-newlib-reent
    micro-os-plus::iii-posix-io
    micro-os-plus::device
    PARENT_SCOPE
  )
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
  set (${_out} "${UOS_BOARD_SRC_DIR}/qemu/src/uart.cpp"
               "${UOS_BOARD_SRC_DIR}/qemu/src/led.cpp"
               "${UOS_BOARD_SRC_DIR}/qemu/src/clocks.cpp"
       PARENT_SCOPE)
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
  set (${_out} "${UOS_BOARD_SRC_DIR}/qemu/include" PARENT_SCOPE)
endfunction ()

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
set (_harness_suites rtos-apis rtos-apis-ram mutex-stress mutex-stress-ram cmsis-os-validator cmsis-os-validator-ram fp-switch)

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
  # mtp-stdcpp (micro-os-scaffold --stdcpp): the stdc++ package on top of µTest++.
  if (_app STREQUAL "mtp-stdcpp")
    set (${_out} stateos-stdc++ uos-micro-test-plus PARENT_SCOPE)
    return ()
  endif ()
  # µTest++ (micro-os-scaffold --micro-test-plus): every mtp-* test.
  if ("${_app}" MATCHES "^mtp-")
    set (${_out} uos-micro-test-plus PARENT_SCOPE)
    return ()
  endif ()
  # The -ram variants: the same suites, run from RAM (their own linker script
  # and runner), on the same start-up and suite targets as the suites.
  set (_suite_libs ${PLATFORM_NAME}-test-startup ${PLATFORM_NAME}-test-suite)
  if (_app STREQUAL "rtos-apis-ram")
    set (${_out} test::rtos-apis xpacks::chan-fatfs micro-os-plus::iii-posix-io
         ${_suite_libs} PARENT_SCOPE)
  elseif (_app STREQUAL "mutex-stress-ram")
    set (${_out} test::mutex-stress ${_suite_libs} PARENT_SCOPE)
  elseif (_app STREQUAL "cmsis-os-validator-ram")
    set (${_out} test::cmsis-os-validator xpacks::arm-cmsis-os-validator
         ${_suite_libs} PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
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
  if (_app STREQUAL "cmsis-os-validator-ram")
    set (${_out} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cmsis-os-validator-ram/linker-ram.ld" PARENT_SCOPE)
  elseif (_app STREQUAL "rtos-apis-ram")
    set (${_out} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/rtos-apis-ram/linker-ram.ld" PARENT_SCOPE)
  elseif (_app STREQUAL "mutex-stress-ram")
    set (${_out} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/mutex-stress-ram/linker-ram.ld" PARENT_SCOPE)
  elseif (_app STREQUAL "smp-mat-test-ram")
    set (${_out} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/smp-mat-test-ram/linker-ram.ld" PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
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
                              MTP_HAS_SMP_BOOT=2
                              MTP_CARRY_STARTUP=0
                              MTP_SEMIHOST_ARGS=1
                              MTP_NANO_PRINTF=1)
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
# This board's QEMU image replaces the board (board_test_qemu_libs): the
# µTest++ tests, checked against the board, run on the board only.
file (GLOB _mtp_apps LIST_DIRECTORIES true CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_LIST_DIR}"
      "${CMAKE_CURRENT_LIST_DIR}/mtp-*")
list (APPEND BOARD_TEST_HWD_ONLY ${_mtp_apps})

# ---- stateos-stdc++ (micro-os-scaffold --stdcpp) ------------------------------------
# The StateOS stdc++ package adapted to µOS++ III (the workspace's
# third_party/stateos-stdc++). Its include directory goes BEFORE the
# toolchain's C++ headers: it provides <bits/gthr-default.h> and the
# kernel-backed C++20 <barrier>/<latch>/<semaphore>. An INTERFACE library:
# a test that links it (board_test_libs) compiles its sources too.
set (_stateos_stdcpp "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/stateos-stdc++")
if (NOT IS_DIRECTORY "${_stateos_stdcpp}")
  message (FATAL_ERROR "stateos-stdc++ not found at ${_stateos_stdcpp}")
endif ()
if (NOT TARGET stateos-stdc++)
  add_subdirectory ("${_stateos_stdcpp}" "${CMAKE_BINARY_DIR}/stateos-stdc++-bin")
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

