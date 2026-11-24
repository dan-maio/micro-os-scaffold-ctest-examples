# tests.local.cmake -- the CMake wiring of this platform's local tests
# (scripts/local-tests.txt). micro-os-scaffold never writes this file; a
# re-scaffold keeps it. Each board_test_<hook>_local (_app _out) answers
# first for the tests it sets ${_out} for.

function (board_test_libs_local _app _out)
  # spi-stdcpp-test: the StateOS stdc++ package on top of µTest++.
  if (_app STREQUAL "spi-stdcpp-test")
    set (${_out} stateos-stdc++ uos-micro-test-plus PARENT_SCOPE)
  endif ()
endfunction ()
