# -----------------------------------------------------------------------------
# What this board's tests need.
#
# Which tests exist is the directory listing beside this file. This file is
# only for what a directory listing cannot say: which tests link the board's
# driver set, which bring their own start-up code, and -- for the six carried
# over from the predecessor that drive the USB gadget, the C++ FatFs or the
# Cortex-M0 -- exactly which sources they compile.
#
# That last one has to be written down. smp_test6 and smp_test7 each ship a
# DIFFERENT usb_vendor_gadget.cpp, and smp_test_int4 a different sdmmc.cpp,
# from the ones the board carries in platforms/aarch32-luckfox-lyra/. Which copy a
# test compiles is a decision the predecessor's Makefiles made per test, and
# this file makes it the same way. A glob would link both and the build would
# fail on duplicate symbols -- or, worse, pick one.
#
# smp_test_int and smp_test_int2 are this board's own, carried over byte for
# byte: they exercise the GIC-400 SGI paths, which no other board has.
# -----------------------------------------------------------------------------

# Where this file is, captured now: inside a function, CMAKE_CURRENT_LIST_DIR
# is the directory of whoever is calling.
set (_lyra_tests "${CMAKE_CURRENT_LIST_DIR}")

# The SD card, through µOS++ POSIX I/O and Chan FatFs (mounted at /sd/ by
# uos_test_storage_start()). smp_test5 and smp_test7 reach it through the
# board's set (UOS_BOARD_SD_POSIX: the RK3506 SD host as /dev/sd).
# smp_test_int4 does NOT: it drives the card from its own interrupt-driven
# sdmmc.cpp, as /dev/sd (sd/sd-block-device-irq.cpp, board_test_sd_posix
# OWN); linking both copies would define sdcard::SdMmc twice.
list (APPEND UOS_TESTS_STORAGE smp_test5 smp_test7 smp_test_int4)
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
  if (_app STREQUAL "smp_test_int4")
    set (${_out} OWN PARENT_SCOPE)
  else ()
    set (${_out} "${UOS_BOARD_SD_POSIX}" PARENT_SCOPE)
  endif ()
endfunction ()

# Carried over whole from the predecessor, each with its own
# smp_install_boot_threads(). Adding the board's shared copy from src/ would
# be a duplicate definition.
set (BOARD_TEST_SELF_CONTAINED
     smp_test_int smp_test_int2
     smp_test5 smp_test6 smp_test7
     smp_test_int3 smp_test_int4 smp_test_int5)

# The -D flags the predecessor's Makefiles carried for these three. They are
# not defaults the sources fall back on: each selects a diagnostic or a
# scheduling behaviour the test was run and reported with.
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
  set (_d "")
  # The USB tests keep their console on the UART (UOS_CONSOLE_UART_ONLY, see
  # the board's uart.hpp): only the verdict goes through semihosting.
  if (_app STREQUAL "smp_test6" OR _app STREQUAL "smp_test7")
    set (_d UOS_CONSOLE_UART_ONLY)
  elseif (_app STREQUAL "smp_test_int")
    # The UART RX echo is interrupt-driven; a console byte mirrored through
    # semihosting while the IRQ runs can drop the WCH-Link. Keep the console
    # on the UART, only the verdict goes through semihosting.
    set (_d UOS_CONSOLE_UART_ONLY)
  elseif (_app STREQUAL "smp_test_int3")
    set (_d SMP_TEST_INT3_DIAG SMP_TEST_INT3_DYNAMIC_AFFINITY
            UOS_CONSOLE_UART_ONLY)
  elseif (_app STREQUAL "smp_test_int4")
    set (_d SMP_TEST_INT4_DIAG SMP_TEST_INT4_DYNAMIC_AFFINITY
            UOS_CONSOLE_UART_ONLY)
  elseif (_app STREQUAL "smp_test_int5")
    # Its console stays on the UART: two lines per M0 interrupt, each a
    # semihosting write over the WCH-Link, dropped the link and ran past the
    # 600 s cap. Only the verdict goes through semihosting (report_result()).
    set (_d SMP_TEST_INT5_SELFTRIG UOS_CONSOLE_UART_ONLY)
  endif ()
  set (${_out} "${_d}" PARENT_SCOPE)
endfunction ()

# Beyond each test's own main.cpp: the board's carried code it takes, and the
# subdirectory it brings itself. One entry per test, from that test's Makefile.
#
#   smp_test5      nothing: the card is the board's /dev/sd (posix-io)
#   smp_test6      USB vendor gadget: the eight shared DWC2 sources plus its
#                  own gadget, DMA pool and environment
#   smp_test7      the same eight and its own three: it streams a file
#                  off the card over USB (the card: the board's /dev/sd)
#   smp_test_int3  none of the shared USB sources -- its usb1_int3.cpp drives
#                  the DWC2 registers directly, from the interrupt
#   smp_test_int4  the same, plus its own interrupt-driven SD driver as
#                  /dev/sd under posix-io and Chan FatFs
#   smp_test_int5  m0_blob.S, which embeds the Cortex-M0 firmware built below
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
  set (_s "")
  if (_app STREQUAL "smp_test6")
    set (_s ${UOS_BOARD_USB_CORE_SOURCES}
            "${_lyra_tests}/smp_test6/usb/usb_vendor_gadget.cpp"
            ${UOS_BOARD_USB_GADGET_SOURCES})
  elseif (_app STREQUAL "smp_test7")
    set (_s ${UOS_BOARD_USB_CORE_SOURCES}
            "${_lyra_tests}/smp_test7/usb/usb_vendor_gadget.cpp"
            ${UOS_BOARD_USB_GADGET_SOURCES})
  elseif (_app STREQUAL "smp_test_int3")
    set (_s ${UOS_BOARD_USB_INT_SOURCES}
            "${_lyra_tests}/smp_test_int3/usb/usb_env_int3.cpp")
  elseif (_app STREQUAL "smp_test_int4")
    set (_s ${UOS_BOARD_USB_INT_SOURCES}
            "${_lyra_tests}/smp_test_int4/usb/usb_env_int3.cpp"
            "${_lyra_tests}/smp_test_int4/sd/sdmmc.cpp"
            "${_lyra_tests}/smp_test_int4/sd/sd-block-device-irq.cpp")
  elseif (_app STREQUAL "smp_test_int5")
    set (_s "${_lyra_tests}/smp_test_int5/m0_blob.S")
  endif ()
  set (${_out} "${_s}" PARENT_SCOPE)
endfunction ()

# smp_test_int4's own sd/ comes first, so its sdmmc.hpp wins over the board's.
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
  set (_i "")
  if (_app STREQUAL "smp_test6")
    set (_i ${UOS_BOARD_USB_INCLUDE})
  elseif (_app STREQUAL "smp_test7")
    set (_i ${UOS_BOARD_USB_INCLUDE})
  elseif (_app STREQUAL "smp_test_int3")
    set (_i ${UOS_BOARD_USB_INCLUDE})
  elseif (_app STREQUAL "smp_test_int4")
    set (_i "${_lyra_tests}/smp_test_int4/sd" ${UOS_BOARD_USB_INCLUDE})
  endif ()
  set (${_out} "${_i}" PARENT_SCOPE)
endfunction ()

# -----------------------------------------------------------------------------
# The Cortex-M0 firmware smp_test_int5 starts.
#
# The RK3506's M0 is not one of the SMP cores -- different ISA, outside the A7
# coherency domain -- so it cannot be scheduled on. smp_test_int5 uses it as
# an interrupt SOURCE: the A7 copies this image to System SRAM at 0xFFF80000,
# releases the M0, and the M0 raises the mailbox interrupt the test counts.
#
# It is a second, tiny firmware inside this build: same arm-none-eabi
# toolchain, -mcpu=cortex-m0, its own linker script. m0_blob.S puts the
# result in .rodata with `.incbin "m0/m0_fw.bin"`, which the assembler
# resolves against its own include path -- hence the -Wa,-I below.
set (_m0_src "${_lyra_tests}/smp_test_int5/m0")
set (_m0_out "${CMAKE_CURRENT_BINARY_DIR}/m0")
set (_m0_bin "${_m0_out}/m0_fw.bin")

add_custom_command (
  OUTPUT "${_m0_bin}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${_m0_out}"
  COMMAND "${CMAKE_C_COMPILER}"
          -mcpu=cortex-m0 -mthumb -Os -ffreestanding -fno-builtin
          -ffunction-sections -fdata-sections -Wall -Wextra -g3
          -nostdlib "-T${_m0_src}/linker.ld" -Wl,--gc-sections
          "${_m0_src}/m0_start.c" "${_m0_src}/m0_main.c"
          -o "${_m0_out}/m0_fw.elf"
  COMMAND "${CMAKE_OBJCOPY}" -O binary "${_m0_out}/m0_fw.elf" "${_m0_bin}"
  DEPENDS "${_m0_src}/m0_start.c" "${_m0_src}/m0_main.c" "${_m0_src}/linker.ld"
  COMMENT "Building the Cortex-M0 firmware for smp_test_int5"
  VERBATIM
)
add_custom_target (m0-firmware ALL DEPENDS "${_m0_bin}")

set_source_files_properties (
  "${_lyra_tests}/smp_test_int5/m0_blob.S"
  PROPERTIES
    OBJECT_DEPENDS  "${_m0_bin}"
    COMPILE_OPTIONS "-Wa,-I${CMAKE_CURRENT_BINARY_DIR}"
)

# -----------------------------------------------------------------------------
# The carried DWC2 gadget stack is compiled at -O0, exactly as the predecessor's
# Makefiles compiled it.
#
# This is not a preference and not a workaround we could drop: the source under
# usb/ is BYTE-IDENTICAL to the predecessor's cortex-a7 stack, which enumerates
# and runs on this board -- but only at -O0. Built here at the -O2 every other
# application gets, the same source stops completing USB enumeration: the host
# reads the device descriptor, then resets and reads it again, never advancing
# to the configuration descriptor. Rebuilding just these sources at -O0 removes
# the failure, and the -O0 object code is instruction-for-instruction the
# predecessor's working image (verified with objdump).
#
# The two mechanisms the port already had to work around were only the visible
# edge of this. Driver::instance() is defined inline in usb/include/
# dwc2_driver.hpp while smp_test6/smp_test7 declare it out of line; at -O0 an
# inline function also gets one out-of-line copy, so the tests' declaration
# always linked. At -O2 the copy is not emitted and --fkeep-inline-functions--
# was needed to bring it back. The EP0/descriptor path has the same shape: at
# -O2 the optimizer rewrites it into something the hardware will not enumerate
# with, and -O0 is the only setting under which it was ever validated.
#
# Scoped to the gadget stack alone: the kernel, the ports and every other test
# keep the -O2 this repository builds with. The application's own descriptor
# file (its usb_vendor_gadget.cpp) is part of the same stack and takes the same
# level, so both smp_test6 and smp_test7 are covered.
set_source_files_properties (
  ${UOS_BOARD_USB_CORE_SOURCES}
  ${UOS_BOARD_USB_GADGET_SOURCES}
  "${_lyra_tests}/smp_test6/usb/usb_vendor_gadget.cpp"
  "${_lyra_tests}/smp_test7/usb/usb_vendor_gadget.cpp"
  PROPERTIES COMPILE_OPTIONS "-O0"
)

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
  # hw_result.hpp (the verdict through semihosting) for the AArch boards.
  if (IS_DIRECTORY "${CMAKE_SOURCE_DIR}/smp-support/include")
    target_include_directories (uos-micro-test-plus INTERFACE "${CMAKE_SOURCE_DIR}/smp-support/include")
  endif ()
  # MTP_CONSOLE: how stdout reaches the console and the runner (mtp_board.hpp).
  # _GNU_SOURCE: the build is strict C++20, which hides fopencookie () and sbrk ().
  target_compile_definitions (uos-micro-test-plus INTERFACE
                              MICRO_OS_PLUS_MICRO_TEST_PLUS_ENABLED
                              _GNU_SOURCE
                              MTP_CONSOLE=1
                              MTP_HAS_SMP_BOOT=1
                              MTP_CARRY_STARTUP=1
                              MTP_SEMIHOST_ARGS=2)
  # The report file: µTest++ opens --output-file with fopen (); the wrapper
  # in mtp_board.cpp turns it into a semihosting stream.
  target_link_options (uos-micro-test-plus INTERFACE "-Wl,--wrap=fopen")
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

