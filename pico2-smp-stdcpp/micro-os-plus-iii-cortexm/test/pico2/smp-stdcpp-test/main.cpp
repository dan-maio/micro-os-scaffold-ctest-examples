/*
 * smp-stdcpp-test -- the StateOS stdc++ package adapted to µOS++ III, on the
 * Raspberry Pi Pico 2 (RP2350, 2x Cortex-M33).
 *
 * The libstdc++ parts a bare-metal target does not get from the toolchain
 * (gthr, std::thread, future/promise/async, chrono clocks, emulated TLS, and
 * C++20 barrier/latch/semaphore). On the board both cores join the scheduler;
 * under QEMU the pico2 image is the generic single-core Cortex-M machine, so
 * the same checks run single-core.
 *
 *   01 - std::thread and TLS   create/join/detach, jthread, ids, sleep, thread_local
 *   02 - promise and future    value, void, wait_for status, shared_future
 *   03 - packaged_task, async  deferred vs async, set_value_at_thread_exit
 *   04 - mutexes and call_once lock_guard, timed, recursive, once
 *   05 - sync primitives       condition_variable, semaphores, latch, barrier
 *   06 - chrono                the two clocks, sleeps, duration conversions
 *
 * Each suite lives in its own suite-*.cpp file and registers itself; there is
 * no list here to keep in step with them.
 */
#include <mtp_board.hpp>

#include <cstdio>

// One core under QEMU, two on the board; both are supported.
static_assert (OS_NCPU >= 1);

int
os_main (int argc, char* argv[])
{
  mtp_board::start ("smp-stdcpp-test");
  // On the board this launches core 1; on the single-core emulated image the
  // board support reports one core and returns. The "00 - Cores" suite is
  // registered only when there is another core to wait for.
  mtp_board::release_cores ();
  std::printf ("# stdc++: StateOS stdc++ adapted to µOS++ III, %u core%s\n",
               static_cast<unsigned> (OS_NCPU),
               static_cast<unsigned> (OS_NCPU) == 1 ? "" : "s");
  return mtp_board::run (argc, argv);
}

#include <mtp_startup.hpp>
