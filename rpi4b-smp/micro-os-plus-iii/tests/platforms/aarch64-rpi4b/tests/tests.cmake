# -----------------------------------------------------------------------------
# What this board's tests need.
#
# Which tests exist is the directory listing beside this file -- a board builds
# what it has, so there is no capability filter and no list of names to keep in
# step with the directories. This file carries only the knobs a test cannot
# state for itself.
# -----------------------------------------------------------------------------

# The board tests that use the SD card through µOS++ POSIX I/O and Chan FatFs
# (the card as /dev/sd, mounted at /sd/ by uos_test_storage_start()).
list (APPEND UOS_TESTS_STORAGE smp-shapes-test usb_big_test)

# The DWC2 USB device driver (micro-os-plus-iii-devices-bcm283x
# drivers/aarch64/src/usb_dwc2.cpp, its BCM2711 build:
# the core at USB_BASE, its interrupt INTID 105), for the one test that uses
# the USB-C socket's gadget. Not in the board's device set: no other test
# links it.
if (NOT TARGET aarch64-rpi4b-usb)
  add_library (aarch64-rpi4b-usb INTERFACE)
  target_sources (aarch64-rpi4b-usb INTERFACE
                  "${UOS_DEVICES_BCM283X_DRIVERS}/src/usb_dwc2.cpp")
endif ()

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
  if (_app STREQUAL "cmsis-os-validator")
    # The validator is Cortex-M code. For its image only, the board
    # (boards/rpi4b/src/) maps a "no cycle counter" page where it probes the
    # DWT (mmu.cpp), and routes SGI 1, its stand-in for NVIC IRQ 0, to its
    # handler (rtos/port_isr.cpp). Every other test's image is unchanged.
    list (APPEND _d UOS_CMSIS_OS_VALIDATOR)
  elseif (_app STREQUAL "smp_mem_test" OR _app STREQUAL "smp_mem_inter_test")
    # For its image only, the board maps the RAM above the first GB page by
    # page, every page invalid until the test maps a block it allocates
    # (boards/rpi4b/src/mmu.cpp, include/mmu_hi.hpp).
    list (APPEND _d UOS_SMP_MEM_TEST)
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
    if ("${_app}" MATCHES "^mtp-eth-" AND TARGET aarch64-rpi4b-eth)
      set (${_out} uos-micro-test-plus aarch64-rpi4b-eth PARENT_SCOPE)
      return ()
    endif ()
    set (${_out} uos-micro-test-plus PARENT_SCOPE)
    return ()
  endif ()
  if (_app STREQUAL "usb_big_test")
    set (${_out} aarch64-rpi4b-usb PARENT_SCOPE)
  elseif (_app STREQUAL "eth-net" OR _app STREQUAL "eth-tcp-server")
    set (${_out} aarch64-rpi4b-eth PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

# -----------------------------------------------------------------------------
# The Ethernet tests: eth-net (link, DHCP, ping, log on the SD card) and
# eth-tcp-server (TCP echo server, received stream saved on the SD card).
#
# They bring the network stack with them, as the Mango Pi Wi-Fi tests do: the
# GENET driver, the lwIP glue and the shared bring-up live under the
# platform's net/ (board support, not test code), and lwIP itself under
# tests/third_party/lwip. Both need the real controller and a cable; this
# port builds a qemu image of every test, and theirs only reports SKIP.
set (_eth_tests eth-net eth-tcp-server)
# The SD card through µOS++ POSIX I/O and Chan FatFs, as above.
list (APPEND UOS_TESTS_STORAGE ${_eth_tests})

# Nothing network-specific is in the repository: pass it at configure time,
#   cmake ... -D ETH_TEST_STATIC_IP=192.168.1.74/24
set (ETH_TEST_STATIC_IP "" CACHE STRING "eth tests: static a.b.c.d/len instead of DHCP")
set (ETH_TEST_GATEWAY "" CACHE STRING "eth tests: gateway to ping (default: from DHCP)")
set (ETH_TEST_PEER "" CACHE STRING "eth-net: optional second host to ping")
set (ETH_TCP_WAIT_S "60" CACHE STRING "eth-tcp-server: seconds to wait for a client")

set (_eth_net_dir "${UOS_BOARD_SRC_DIR}/net/src")
set (_eth_lwip_dir "${CMAKE_SOURCE_DIR}/third_party/lwip/src")

if (NOT TARGET aarch64-rpi4b-eth)
  file (GLOB _eth_sources CONFIGURE_DEPENDS
        "${_eth_net_dir}/*.cpp"
        "${_eth_lwip_dir}/core/*.c"
        "${_eth_lwip_dir}/core/ipv4/*.c"
        "${_eth_lwip_dir}/netif/*.c")
  add_library (aarch64-rpi4b-eth INTERFACE)
  target_sources (aarch64-rpi4b-eth INTERFACE ${_eth_sources})
  target_include_directories (aarch64-rpi4b-eth INTERFACE
                              "${_eth_net_dir}"
                              "${_eth_lwip_dir}/include")
  target_compile_definitions (aarch64-rpi4b-eth INTERFACE
                              "ETH_TEST_STATIC_IP=\"${ETH_TEST_STATIC_IP}\""
                              "ETH_TEST_GATEWAY=\"${ETH_TEST_GATEWAY}\""
                              "ETH_TEST_PEER=\"${ETH_TEST_PEER}\""
                              "ETH_TCP_WAIT_S=${ETH_TCP_WAIT_S}")
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
# mtp-eth-net: the Ethernet stack, and the SD card for its log suite.
list (APPEND UOS_TESTS_STORAGE mtp-eth-net)

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

