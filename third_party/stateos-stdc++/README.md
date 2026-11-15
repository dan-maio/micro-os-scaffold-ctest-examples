# stateos-stdc++ for µOS++ III (SMP)

The C++ standard-library pieces a bare-metal AArch64 target does not get from
the toolchain, adapted from the StateOS `stdc++` package:

| Part | What it provides |
| --- | --- |
| `include/bits/gthr-default.h`, `src/gthread.cc` | GCC gthread layer: `__gthread_*` over µOS++ mutexes, condition variables and threads |
| `src/thread.cc` | `std::thread`, the thread-specific key table, `this_thread::__sleep_for` (`std::jthread` comes from `<thread>`/`<stop_token>` on top of these) |
| `src/mutex.cc` | the `std::call_once` support objects |
| `src/condition_variable.cc` | `std::condition_variable`, `notify_all_at_thread_exit`, `__at_thread_exit` |
| `src/future.cc` | the `std::future` error category and at-thread-exit machinery |
| `src/chrono.cc` | `system_clock` and `steady_clock` over the kernel's system clock |
| `src/emutls.cc` | emulated `thread_local` storage |
| `include/{barrier,latch,semaphore}` | C++20 `std::barrier`, `std::latch`, `std::counting_semaphore` |

## How it fits together

`<mutex>` includes `<bits/gthr.h>`, which includes `<bits/gthr-default.h>`.
With this package's `include/` directory before the toolchain's C++ headers,
that is *this* `gthr-default.h`. It declares fixed-size, aligned opaque storage
for the kernel objects (`__gthread_mutex_t`, `__gthread_cond_t`) and the
`__gthread_*` functions; `src/gthread.cc` is the only file that sees both the
kernel headers and the gthread types.

That split is deliberate: `<bits/gthr.h>` is parsed from inside `<mutex>`, and
the µOS++ headers include `<mutex>` themselves. Keeping the kernel headers out
of `gthr-default.h` breaks the cycle.

GCC 15 does define the C++20 concurrency feature macros on this target: its
generic atomic-wait layer is itself a mutex and a condition variable, so the
toolchain's `<barrier>`, `<latch>` and `<semaphore>` would compile. This package
keeps the StateOS approach anyway: its include directory is found first, and the
three wrapper headers define the classes directly on the kernel's mutex,
condition variable and counting semaphore. A wait then blocks in the scheduler,
and libstdc++'s atomic-wait machinery (waiter pool, hash table) is not pulled
in.

## Differences from the StateOS original

- Kernel objects: `os::rtos::mutex`, `mutex_recursive`, `condition_variable`,
  `semaphore_counting`, `thread`, `clock_systick` instead of the StateOS
  `mtx_t`, `cnd_t`, `tsk_t`, `cnt_t` primitives.
- A `std::thread` is an `os::rtos::thread` allocated from the free store; the
  thread's identity is the address of that object, which is also what
  `this_thread::thread()` returns.
- Detached thread objects cannot be deleted from inside the thread and µOS++
  has no detached-thread reaper hook, so `src/gthread.cc` remembers up to eight
  of them and deletes each once the kernel reports it terminated.
- `_GLIBCXX_HAVE_TLS` is **not** defined. `std::call_once` then uses the
  global-functor path in `src/mutex.cc`, and `thread_local` goes through
  `src/emutls.cc`.
- `critical_section` uses `os::rtos::scheduler::critical_section`.
- `barrier`/`latch` use a µOS++ mutex and condition variable rather than the
  kernel's internal wait lists. The barrier completion function runs with the
  barrier's mutex held and must not call back into the barrier.

## Using it

```cmake
add_subdirectory (third_party/stateos-stdc++ stateos-stdc++-bin)
target_link_libraries (my-test PRIVATE stateos-stdc++)
```

The compile definition `_GLIBCXX_HAS_GTHREADS=1` and the include directory are
INTERFACE properties of the target.

## Full documentation

`rpi4b-smp/STDCXX.md` (`STDCXX.pdf`) is the detailed porting and usage guide:
the GCC gthread contract, the design of every ported file, the µTest++ test
walkthrough and the QEMU/hardware CTest cases.

## License

The GCC-derived files carry the GCC Runtime Library Exception; see
`src/COPYING3` and `src/COPYING.RUNTIME`. The StateOS adaptations are MIT, see
`LICENSE`.
