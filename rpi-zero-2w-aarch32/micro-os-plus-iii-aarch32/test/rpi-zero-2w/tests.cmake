# -----------------------------------------------------------------------------
# What this board's tests need.
#
# Which tests exist is the directory listing beside this file -- a board builds
# what it has, so there is no capability filter and no list of names to keep in
# step with the directories. This file carries only the knobs a test cannot
# state for itself.
# -----------------------------------------------------------------------------

# Tests that reach the SD card or the USB device controller, and so link
# UOS_BOARD_DEVICES (boards/<id>/board.cmake names which driver set that is).
set (BOARD_TEST_NEED_DEVICES
     sd_test smp-mat-sdcard-test smp-num-test smp-pipeline-test usb_test)

# Per-test -D flags, carried over from the per-test Makefiles.
function (board_test_defines _app _out)
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
# The harness suites.
if (NOT TARGET aarch32-rpi-zero-2w-harness-suite)
  add_library (aarch32-rpi-zero-2w-harness-suite INTERFACE)
  target_compile_definitions (aarch32-rpi-zero-2w-harness-suite INTERFACE
                              OS_USE_OS_APP_CONFIG_H UOS_HARNESS_SUITE
                              _POSIX_C_SOURCE=200809L _GNU_SOURCE)
  target_include_directories (aarch32-rpi-zero-2w-harness-suite INTERFACE
                              "${CMAKE_SOURCE_DIR}/platforms/${PLATFORM_NAME}/include")
  # The suite supplies no main() and no startup hooks; this is the harness
  # platform's, compiled into the application with the builder's variant
  # defines (QEMU_BUILD/HW_BUILD), so one copy serves both images.
  target_sources (aarch32-rpi-zero-2w-harness-suite INTERFACE
                  "${CMAKE_SOURCE_DIR}/platforms/${PLATFORM_NAME}/src/platform-support.cpp")
  target_link_libraries (aarch32-rpi-zero-2w-harness-suite INTERFACE
                         micro-os-plus::iii-semihosting
                         micro-os-plus::iii-newlib-reent
                         micro-os-plus::iii-posix-io)
  target_link_options (aarch32-rpi-zero-2w-harness-suite INTERFACE "-Wl,--wrap=os_main")
endif ()

function (board_test_libs _app _out)
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
  if (_app STREQUAL "rtos-apis")
    set (${_out} test::rtos-apis xpacks::chan-fatfs
         aarch32-rpi-zero-2w-harness-suite PARENT_SCOPE)
  elseif (_app STREQUAL "mutex-stress")
    set (${_out} test::mutex-stress aarch32-rpi-zero-2w-harness-suite PARENT_SCOPE)
  elseif (_app STREQUAL "cmsis-os-validator")
    set (${_out} test::cmsis-os-validator xpacks::arm-cmsis-os-validator
         aarch32-rpi-zero-2w-harness-suite PARENT_SCOPE)
  elseif (_app STREQUAL "wifi-net" OR _app STREQUAL "wifi-tcp-server")
    set (${_out} aarch32-rpi-zero-2w-wifi PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()


# -----------------------------------------------------------------------------
# The Wi-Fi tests. They bring the radio and the network stack with them, as
# the Mango Pi Wi-Fi tests and the Pi 4 B Ethernet tests do: the SDIO host,
# the CYW43436 driver, its firmware and the lwIP glue live under
# test/boards/rpi-zero-2w/wifi (board support), lwIP under
# test/third_party/lwip. This port builds a qemu image of every test; theirs
# only reports SKIP.
#
# What the tests associate with. No credentials in the repository: pass them
# at configure time, e.g.
#   cmake ... -D WIFI_TEST_SSID=myssid -D WIFI_TEST_PASS=mypassphrase
set (WIFI_TEST_SSID "" CACHE STRING "wifi tests: SSID to join")
set (WIFI_TEST_PASS "" CACHE STRING "wifi tests: WPA2 passphrase (empty: open network)")
set (WIFI_TEST_GATEWAY "" CACHE STRING "wifi tests: gateway to ping (default: from DHCP)")
set (WIFI_COUNTRY "XX" CACHE STRING "wifi tests: two-letter regulatory domain")
set (WIFI_TCP_WAIT_S "60" CACHE STRING "wifi-tcp-server: seconds to wait for a client")

set (_wifi_dir "${UOS_BOARD_SRC_DIR}/wifi/src")
set (_wifi_lwip_dir "${CMAKE_CURRENT_LIST_DIR}/../third_party/lwip/src")

if (NOT TARGET aarch32-rpi-zero-2w-wifi)
  file (GLOB _wifi_sources CONFIGURE_DEPENDS
        "${_wifi_dir}/*.cpp"
        "${_wifi_lwip_dir}/core/*.c"
        "${_wifi_lwip_dir}/core/ipv4/*.c"
        "${_wifi_lwip_dir}/netif/*.c")
  add_library (aarch32-rpi-zero-2w-wifi INTERFACE)
  # The SD card: the radio owns the Arasan controller, so these tests read
  # the card through SDHOST (SD_BACKEND_SDHOST). They compile the FatFs
  # stack with that back end themselves and do not link
  # micro-os-plus::devices, which carries the Arasan driver.
  set (_wifi_drivers "${CMAKE_CURRENT_LIST_DIR}/../../drivers")
  target_sources (aarch32-rpi-zero-2w-wifi INTERFACE
                  ${_wifi_sources} "${_wifi_dir}/fw.S"
                  "${_wifi_drivers}/src/sd_sdhost.cpp"
                  "${_wifi_drivers}/fatfs/ff.c"
                  "${_wifi_drivers}/fatfs/diskio_sd.cpp"
                  "${_wifi_drivers}/fatfs/fatfs_hw.cpp")
  target_include_directories (aarch32-rpi-zero-2w-wifi INTERFACE
                              "${_wifi_dir}"
                              "${_wifi_lwip_dir}/include"
                              "${_wifi_drivers}/include"
                              "${_wifi_drivers}/fatfs")
  # fw.S pulls the firmware in with .incbin, relative to this directory.
  set_property (SOURCE "${_wifi_dir}/fw.S" APPEND PROPERTY
                COMPILE_OPTIONS "-Wa,-I${_wifi_dir}")
  target_compile_definitions (aarch32-rpi-zero-2w-wifi INTERFACE
                              "WIFI_TEST_SSID=\"${WIFI_TEST_SSID}\""
                              "WIFI_TEST_PASS=\"${WIFI_TEST_PASS}\""
                              "WIFI_TEST_GATEWAY=\"${WIFI_TEST_GATEWAY}\""
                              "WIFI_COUNTRY=\"${WIFI_COUNTRY}\""
                              "WIFI_TCP_WAIT_S=${WIFI_TCP_WAIT_S}"
                              SD_BACKEND_SDHOST)
endif ()


# ---- µTest++ (micro-os-scaffold --micro-test-plus) -------------------------------------
# Every test directory named mtp-* gets µTest++ (the framework in the
# workspace's third_party/micro-test-plus) and the board support in
# ../mtp/ through the uos-micro-test-plus INTERFACE library, linked by the hook
# below. The tests are built and registered with CTest like any other.
set (_mtp_framework "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/micro-test-plus")
set (_mtp_support "${CMAKE_CURRENT_LIST_DIR}/../mtp")
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
                              MTP_CONSOLE=1
                              MTP_HAS_SMP_BOOT=1
                              MTP_CARRY_STARTUP=1
                              MTP_SEMIHOST_ARGS=2)
  # The report file: µTest++ opens --output-file with fopen(); the
  # wrapper in mtp_board.cpp turns it into a semihosting stream, so the
  # boards need no _open()/_write() file system calls of their own.
  target_link_options (uos-micro-test-plus INTERFACE "-Wl,--wrap=fopen")
endif ()

# ---- stateos-stdc++ (micro-os-scaffold --stdcpp) ------------------------------------
# The StateOS stdc++ package adapted to µOS++ III (the workspace's
# third_party/stateos-stdc++). Its include directory goes BEFORE the
# toolchain's C++ headers: it provides <bits/gthr-default.h> and the
# kernel-backed C++20 <barrier>/<latch>/<semaphore>. An INTERFACE library:
# a test that links it (board_test_libs) compiles its sources too.
set (_stateos_stdcpp "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/stateos-stdc++")
if (NOT IS_DIRECTORY "${_stateos_stdcpp}")
  message (FATAL_ERROR "stateos-stdc++ not found at ${_stateos_stdcpp}")
endif ()
if (NOT TARGET stateos-stdc++)
  add_subdirectory ("${_stateos_stdcpp}" "${CMAKE_BINARY_DIR}/stateos-stdc++-bin")
endif ()
