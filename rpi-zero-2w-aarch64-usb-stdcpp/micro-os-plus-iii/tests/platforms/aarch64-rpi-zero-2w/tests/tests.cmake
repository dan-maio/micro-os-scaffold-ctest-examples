# -----------------------------------------------------------------------------
# What this board's tests need.
#
# Which tests exist is the directory listing beside this file -- a board builds
# what it has, so there is no capability filter and no list of names to keep in
# step with the directories. This file carries only the knobs a test cannot
# state for itself.
# -----------------------------------------------------------------------------

# usb_test reaches the card through µOS++ POSIX I/O and Chan FatFs (mounted at
# /sd/ by uos_test_storage_start()); its USB gadget is
# micro-os-plus::devices-usb (board_test_libs below).
list (APPEND UOS_TESTS_STORAGE usb_test)

# Per-test -D flags, carried over from the per-test Makefiles.
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
  if (_app STREQUAL "usb_test")
    # usb_test's own blink pattern: LED_BLINKS blinks of LED_ON_MS lit and
    # LED_OFF_MS dark, then LED_GAP_MS dark, repeating. Which GPIO that is
    # belongs to the board, so LED_PIN is set for every test, not here.
    list (APPEND _d LED_BLINKS=3 LED_ON_MS=40 LED_OFF_MS=40
                    LED_GAP_MS=300 USB_FORCE_FS)
  elseif (_app STREQUAL "cmsis-os-validator")
    # The validator is Cortex-M code. For its image only, the board maps a
    # "no cycle counter" page where it probes the DWT (src/mmu.cpp), and
    # routes Mailbox 1, its stand-in for NVIC IRQ 0, to its handler
    # (src/rtos/port_isr.cpp). Every other test's image is unchanged.
    list (APPEND _d UOS_CMSIS_OS_VALIDATOR)
  endif ()
  set (${_out} "${_d}" PARENT_SCOPE)
endfunction ()

# -----------------------------------------------------------------------------
# The harness suites (mutex-stress, rtos-apis, cmsis-os-validator) are no
# longer here: they are upstream's, built by the tests' builder from
# tests/sources/<suite>/ (the platform's UOS_TESTS_SUITES), with the
# validator's board glue in platforms/<P>/src/cmsis-os-validator-support.cpp.

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
  if (_app STREQUAL "wifi-net" OR _app STREQUAL "wifi-tcp-server")
    set (${_out} aarch64-rpi-zero-2w-wifi PARENT_SCOPE)
  elseif (_app STREQUAL "usb_test")
    set (${_out} micro-os-plus::devices-usb PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()


# -----------------------------------------------------------------------------
# The Wi-Fi tests. They bring the radio and the network stack with them, as
# the Mango Pi Wi-Fi tests and the Pi 4 B Ethernet tests do: the SDIO host,
# the CYW43436 driver, its firmware and the lwIP glue live under
# the platform's wifi/ (board support), lwIP under
# tests/third_party/lwip. This port builds a qemu image of every test; theirs
# only reports SKIP.
set (_wifi_tests wifi-net wifi-tcp-server)

# They use the SD card through µOS++ POSIX I/O and Chan FatFs (mounted at
# /sd/ by uos_test_storage_start()), on the SDHOST controller.
list (APPEND UOS_TESTS_STORAGE wifi-net wifi-tcp-server)
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
  if (_app STREQUAL "wifi-net" OR _app STREQUAL "wifi-tcp-server")
    set (${_out} micro-os-plus::devices-sdhost-posix PARENT_SCOPE)
  else ()
    set (${_out} "${UOS_BOARD_SD_POSIX}" PARENT_SCOPE)
  endif ()
endfunction ()

# What the tests associate with. No credentials in the repository: pass them
# at configure time, e.g.
#   cmake ... -D WIFI_TEST_SSID=myssid -D WIFI_TEST_PASS=mypassphrase
set (WIFI_TEST_SSID "" CACHE STRING "wifi tests: SSID to join")
set (WIFI_TEST_PASS "" CACHE STRING "wifi tests: WPA2 passphrase (empty: open network)")
set (WIFI_TEST_GATEWAY "" CACHE STRING "wifi tests: gateway to ping (default: from DHCP)")
set (WIFI_COUNTRY "XX" CACHE STRING "wifi tests: two-letter regulatory domain")
set (WIFI_TCP_WAIT_S "60" CACHE STRING "wifi-tcp-server: seconds to wait for a client")

set (_wifi_dir "${UOS_BOARD_SRC_DIR}/wifi/src")
set (_wifi_lwip_dir "${CMAKE_SOURCE_DIR}/third_party/lwip/src")

if (NOT TARGET aarch64-rpi-zero-2w-wifi)
  file (GLOB _wifi_sources CONFIGURE_DEPENDS
        "${_wifi_dir}/*.cpp"
        "${_wifi_lwip_dir}/core/*.c"
        "${_wifi_lwip_dir}/core/ipv4/*.c"
        "${_wifi_lwip_dir}/netif/*.c")
  add_library (aarch64-rpi-zero-2w-wifi INTERFACE)
  # The SD card is not here: the radio owns the Arasan controller, so these
  # tests reach the card through SDHOST, as the posix-io block device /dev/sd
  # (micro-os-plus::devices-sdhost-posix, board_test_sd_posix above).
  target_sources (aarch64-rpi-zero-2w-wifi INTERFACE
                  ${_wifi_sources} "${_wifi_dir}/fw.S")
  target_include_directories (aarch64-rpi-zero-2w-wifi INTERFACE
                              "${_wifi_dir}"
                              "${_wifi_lwip_dir}/include"
                              "${UOS_DEVICES_BCM283X_DRIVERS}/include")
  # fw.S pulls the firmware in with .incbin, relative to this directory.
  set_property (SOURCE "${_wifi_dir}/fw.S" APPEND PROPERTY
                COMPILE_OPTIONS "-Wa,-I${_wifi_dir}")
  target_compile_definitions (aarch64-rpi-zero-2w-wifi INTERFACE
                              "WIFI_TEST_SSID=\"${WIFI_TEST_SSID}\""
                              "WIFI_TEST_PASS=\"${WIFI_TEST_PASS}\""
                              "WIFI_TEST_GATEWAY=\"${WIFI_TEST_GATEWAY}\""
                              "WIFI_COUNTRY=\"${WIFI_COUNTRY}\""
                              "WIFI_TCP_WAIT_S=${WIFI_TCP_WAIT_S}")
endif ()

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

