# tests.local.cmake -- the CMake wiring of this platform's local tests
# (scripts/local-tests.txt). micro-os-scaffold never writes this file; a
# re-scaffold keeps it.

# usb-sd-stdcpp: usb_test's device, protocol and SD storage, with the
# application written on the standard C++ library (StateOS stdc++).
# The card through µOS++ POSIX I/O + Chan FatFs, mounted at /sd/.
list (APPEND UOS_TESTS_STORAGE usb-sd-stdcpp)

function (board_test_libs_local _app _out)
  if (_app STREQUAL "usb-sd-stdcpp")
    set (${_out} stateos-stdc++ micro-os-plus::devices-usb PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_defines_local _app _out)
  if (_app STREQUAL "usb-sd-stdcpp")
    # usb_test's LED pattern and its full-speed USB link.
    set (${_out} LED_BLINKS=3 LED_ON_MS=40 LED_OFF_MS=40 LED_GAP_MS=300
                 USB_FORCE_FS
                 # Every std::thread gets 16 KiB (the stdc++ default is 4 KiB
                 # on AArch32): the sd thread keeps a FatFs file and its sector
                 # on the stack, the usb thread runs the USB service loop.
                 STATEOS_STDCPP_THREAD_STACK_BYTES=16384 PARENT_SCOPE)
  endif ()
endfunction ()
