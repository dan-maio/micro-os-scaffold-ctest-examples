/*
 * smp_test4 -- the StateOS stdc++ package adapted to µOS++ III SMP.
 *
 * The libstdc++ parts a bare-metal target does not get from the toolchain
 * (gthr, std::thread, future/promise/async, chrono clocks, emulated TLS, and
 * C++20 barrier/latch/semaphore), tested on the Raspberry Pi 4 B with all four
 * cores in the scheduler.
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

#if !defined(MTP_HAS_SMP_BOOT) || !MTP_HAS_SMP_BOOT || OS_NCPU < 2
#error "smp_test4 needs a multi-core build with the board's SMP boot helpers"
#endif

int
os_main (int argc, char* argv[])
{
  mtp_board::start ("smp_test4");
  mtp_board::release_cores ();
  std::printf ("# stdc++: StateOS stdc++ adapted to µOS++ III, %u cores\n",
               static_cast<unsigned> (OS_NCPU));
  return mtp_board::run (argc, argv);
}

#include <mtp_startup.hpp>
