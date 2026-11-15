# -----------------------------------------------------------------------------
# stateos-stdc++ for µOS++ III (SMP).
#
# The libstdc++ pieces a bare-metal target does not get from the toolchain:
# the gthread layer (std::thread, std::mutex, std::condition_variable,
# std::call_once), std::future/promise/async/packaged_task, the chrono clocks,
# emulated TLS, and C++20 <barrier>/<latch>/<semaphore>.
#
# An INTERFACE library on purpose: its sources are compiled into each test
# that links it, with that test's defines and include path. The include
# directory must come BEFORE the toolchain's C++ headers, so that
#   <bits/gthr-default.h>  is this one, and
#   <barrier>, <latch>, <semaphore>
# are these implementations (the toolchain only provides them when atomic
# waiting or POSIX semaphores exist, which is not the case here).
# -----------------------------------------------------------------------------

if (NOT TARGET stateos-stdc++)
  add_library (stateos-stdc++ INTERFACE)

  target_include_directories (
    stateos-stdc++ BEFORE INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

  # libstdc++ enables the threading parts of <mutex>, <thread>, <future>, ...
  # only under this macro. The toolchain's c++config.h leaves it undefined.
  target_compile_definitions (stateos-stdc++ INTERFACE _GLIBCXX_HAS_GTHREADS=1)

  target_sources (
    stateos-stdc++ INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gthread.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/thread.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mutex.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/condition_variable.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/future.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/chrono.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/emutls.cc")

  # The sources need the kernel headers. Usually the consumer already links
  # the port, which brings micro-os-plus::iii; link it when it is there.
  if (TARGET micro-os-plus::iii)
    target_link_libraries (stateos-stdc++ INTERFACE micro-os-plus::iii)
  endif ()
endif ()
