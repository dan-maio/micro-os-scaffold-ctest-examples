# StateOS stdc++ on µOS++ III

<p class="subtitle">A detailed guide to the StateOS stdc++ package, its port to µOS++ III SMP, and its use in a µTest++ test and a CTest case — from the GCC gthread contract to the Raspberry Pi 4 B hardware run</p>

**Library:** `third_party/stateos-stdc++/`<br>
**Test:** `rpi4b-smp/micro-os-plus-iii/tests/platforms/aarch64-rpi4b/tests/smp_test4/` (a local test; previously `micro-os-plus-iii-aarch64/test/rpi4b/smp_test4/`)<br>
**Workspace:** `rpi4b-smp/`<br>
**Target:** Raspberry Pi 4 B, BCM2711, 4 × Cortex-A72, AArch64, µOS++ III SMP<br>
**Toolchain:** `aarch64-none-elf-gcc` 15.2.1, C++20, `-fno-exceptions -fno-rtti`

This document answers three questions, in this order:

1. **What is StateOS/stdc++?** — the package, the GCC contract it implements, and what it assumes about its kernel.
2. **How was it ported to µOS++ III?** — the design, every file, and the problems that had to be solved on the way.
3. **How is it used in a test and in CTest?** — the CMake target, a µTest++ test, the QEMU and hardware CTest cases, what a passing run looks like, and how to start a new project from scratch with `micro-os-scaffold`.

It is written for someone who has to maintain this port, adapt it to another board, or write a new test with it. It is not a libstdc++ tutorial; it explains only the parts the port touches.

> **Read this first: the workspace layout changed on 2026-11-24.** This guide was
> written for the previous layout. The workspace was then re-scaffolded from the
> restructured source, and the tests, the startup code, the µTest++ support and
> the results are the same. The paths and the runners moved:
>
> | In this guide | In the re-scaffolded workspace |
> | --- | --- |
> | `tests/run.sh`, `cd tests` | `./run.sh` at the workspace root |
> | `tests/scripts/env.sh` | `scripts/env.sh` |
> | `tests/build/<configuration>/` | `micro-os-plus-iii/tests/build/<configuration>/` |
> | `.../.qemu-logs/<test>.tap`, `.../.hw-logs/<test>.tap` | `micro-os-plus-iii/tests/build/<configuration>/mtp-reports/<platform>-<test>-qemu.tap` (or `-hwd.tap`) |
> | `micro-os-plus-iii-smp/` (the kernel) | `micro-os-plus-iii/` |
> | `micro-os-plus-iii-<arch>/` (the port) | `<arch>/micro-os-plus-iii-<arch>/` |
> | `micro-os-plus-iii-<arch>/test/<board>/<test>/`, `.../tests.cmake` | `micro-os-plus-iii/tests/platforms/<platform>/tests/<test>/`, `.../tests/tests.cmake` (for example `micro-os-plus-iii/tests/platforms/aarch64-rpi4b/tests/smp_test4/`) |
> | `micro-os-plus-iii-<arch>/test/boards/<board>/` (board code, `linker.ld`, `hw.sh`) | `micro-os-plus-iii/tests/platforms/<platform>/` (`src/`, `include/`, `device/linker-scripts/linker.ld`, `openocd*.cfg`) |
> | `micro-os-plus-iii-<arch>/test/CMakeLists.txt` (the port's test builder) | `micro-os-plus-iii/tests/board-tests/CMakeLists.txt` (one builder, the same hooks) |
> | `micro-os-plus-iii-<arch>/test/mtp/` | `micro-os-plus-iii/tests/mtp/` |
> | `test_smpl/run-qemu.sh`, `run-hw.sh`, the board's `hw.sh` | CTest runs QEMU and OpenOCD itself: `micro-os-plus-iii/tests/cmake/uos-tests-run.cmake` and `uos-hwd.tcl`; the µTest++ command line is added there (marked `SCAFFOLD µTest++`) |
>
> The chapters on the runner scripts describe the old shell runners. The steps are
> the same (halt, enable semihosting, load, resume, read `RESULT:`), but CTest now
> issues them as OpenOCD commands.

---

# Part I — The package

# 1. What StateOS/stdc++ is

[StateOS](https://github.com/stateos/StateOS) is a small real-time operating system. Its C++ standard-library support lives in the `common` submodule, under `common/stateos/stdc++`:

```text
common/stateos/stdc++/
├── bits/gthr-default.h          the GCC gthread layer
├── inc/barrier.hh               C++20 std::barrier
├── inc/latch.hh                 C++20 std::latch
├── inc/semaphore.hh             C++20 std::counting_semaphore
├── inc/critical_section.hh      scheduler/core lock helper
├── inc/chrono.hh                a systick clock and duration→tick conversion
├── src/thread.cc                std::thread, this_thread::__sleep_for
├── src/mutex.cc                 std::call_once support
├── src/condition_variable.cc    std::condition_variable, notify_all_at_thread_exit
├── src/future.cc                std::future error category, at-thread-exit hook
├── src/chrono.cc                system_clock and steady_clock
├── src/emutls.cc                emulated thread_local storage
├── src/COPYING3                 GPLv3
└── src/COPYING.RUNTIME          GCC Runtime Library Exception
```

The `.cc` files are libstdc++ sources with a short StateOS modification block at the top; the GCC copyright and the Runtime Library Exception apply to them. The StateOS-specific files are MIT-licensed. Both licences are kept in the copy under `third_party/stateos-stdc++/`.

A StateOS application enables the package with four lines in its makefile:

```make
DEFS += _GLIBCXX_HAS_GTHREADS
DEFS += __STATEOS_STDCXX
INCS += $(COMMON)/stateos/stdc++
SRCS += $(COMMON)/stateos/stdc++/src/emutls.cc
SRCS += $(COMMON)/stateos/stdc++/src/chrono.cc
SRCS += $(COMMON)/stateos/stdc++/src/future.cc
SRCS += $(COMMON)/stateos/stdc++/src/mutex.cc
SRCS += $(COMMON)/stateos/stdc++/src/condition_variable.cc
SRCS += $(COMMON)/stateos/stdc++/src/thread.cc
```

`_GLIBCXX_HAS_GTHREADS` is the switch that makes libstdc++'s headers declare the threading classes at all; `__STATEOS_STDCXX` marks the sources as the StateOS build; the include directory must come **before** the toolchain's C++ headers.

## 1.1 Why libstdc++ needs a port at all

libstdc++ is written against a small "gthread" abstraction. On a hosted system the implementation is POSIX threads; on a bare-metal target it is whatever the RTOS provides. Without an implementation:

- `std::mutex`, `std::recursive_mutex`, `std::timed_mutex`, `std::condition_variable` are **not declared**;
- `std::thread` exists but cannot start a thread;
- `std::promise`, `std::future`, `std::async`, `std::packaged_task` are unavailable;
- `std::call_once` falls back to a single-threaded implementation.

That is what the toolchain ships for `aarch64-none-elf`: `c++config.h` leaves `_GLIBCXX_HAS_GTHREADS` and `_GLIBCXX_HAVE_TLS` undefined, and has no POSIX threads, no futexes, no `<pthread.h>`.

The StateOS package supplies:

| Layer | What it is |
| --- | --- |
| `bits/gthr-default.h` | the gthread contract, implemented with the RTOS |
| six libstdc++ `.cc` files | the non-inline parts of `<thread>`, `<mutex>`, `<condition_variable>`, `<future>`, `<chrono>` |
| `inc/*.hh` | C++20 `barrier`, `latch`, `semaphore`, and helpers |
| `src/emutls.cc` | `thread_local` support for a target without native TLS |

# 2. The GCC gthread contract

`<mutex>` includes `<bits/gthr.h>`, which includes `<bits/gthr-default.h>`. The contract is small but exact.

## 2.1 Types

| Type | Meaning in libstdc++ |
| --- | --- |
| `__gthread_t` | a thread handle; stored in `std::thread::id` |
| `__gthread_key_t` | a thread-specific-data key |
| `__gthread_once_t` | the state of a `std::once_flag` |
| `__gthread_mutex_t` | a non-recursive mutex, embedded by value in `std::mutex` |
| `__gthread_recursive_mutex_t` | the same for `std::recursive_mutex` |
| `__gthread_cond_t` | a condition variable, embedded in `std::condition_variable` |
| `__gthread_time_t` | an absolute time, with `.tv_sec` and `.tv_nsec` |

`__gthread_mutex_t` and `__gthread_cond_t` are **members** of `std::mutex` and `std::condition_variable`, so they must be complete types with a known size and alignment. They cannot be forward declarations or pointers.

## 2.2 Macros

| Macro | Effect |
| --- | --- |
| `__GTHREADS` / `__GTHREADS_CXX0X` | the threading classes exist |
| `__GTHREAD_ONCE_INIT` | constant initialiser for `__gthread_once_t` |
| `__GTHREAD_MUTEX_INIT` | if defined, `std::mutex` is constexpr and statically initialised |
| `__GTHREAD_MUTEX_INIT_FUNCTION(m)` | otherwise, called from the `std::mutex` constructor |
| `__GTHREAD_RECURSIVE_MUTEX_INIT` / `..._INIT_FUNCTION(m)` | the same for recursive mutexes |
| `__GTHREAD_COND_INIT` / `__GTHREAD_COND_INIT_FUNCTION(c)` | the same for condition variables |

If a mutex needs a run-time constructor (as ours does), do **not** define `__GTHREAD_MUTEX_INIT`; define the `_INIT_FUNCTION` form instead. libstdc++ then gives `std::mutex` a non-constexpr constructor and a destructor that calls `__gthread_mutex_destroy()`.

## 2.3 Functions

```c++
int  __gthread_active_p (void);

int  __gthread_mutex_destroy (__gthread_mutex_t*);
int  __gthread_mutex_lock (__gthread_mutex_t*);
int  __gthread_mutex_trylock (__gthread_mutex_t*);
int  __gthread_mutex_timedlock (__gthread_mutex_t*, const __gthread_time_t*);
int  __gthread_mutex_unlock (__gthread_mutex_t*);
     /* recursive variants, same names with recursive_mutex */

int  __gthread_once (__gthread_once_t*, void (*) (void));

int  __gthread_cond_destroy (__gthread_cond_t*);
int  __gthread_cond_signal (__gthread_cond_t*);
int  __gthread_cond_broadcast (__gthread_cond_t*);
int  __gthread_cond_wait (__gthread_cond_t*, __gthread_mutex_t*);
int  __gthread_cond_wait_recursive (__gthread_cond_t*,
                                    __gthread_recursive_mutex_t*);
int  __gthread_cond_timedwait (__gthread_cond_t*, __gthread_mutex_t*,
                               const __gthread_time_t*);

int  __gthread_create (__gthread_t*, void (*) (void*), void*);
int  __gthread_join (__gthread_t, void**);
int  __gthread_detach (__gthread_t);
int  __gthread_equal (__gthread_t, __gthread_t);
__gthread_t __gthread_self (void);
int  __gthread_yield (void);

int  __gthread_key_create (__gthread_key_t*, void (*) (void*));
int  __gthread_key_delete (__gthread_key_t);
void* __gthread_getspecific (__gthread_key_t);
int  __gthread_setspecific (__gthread_key_t, const void*);
```

The return value convention is POSIX-like: `0` on success, an `errno` value otherwise. libstdc++ turns a non-zero value from `lock()` into `std::system_error`, which on this target aborts (no exceptions).

## 2.4 Two details that matter

**The `__gthread_create` signature.** GCC's documentation shows `void*(*)(void*)`, but GCC 15's `std::thread` passes `execute_native_thread_routine`, which is `void (*)(void*)`. The port declares `__gthread_create` with `void (*)(void*)` to match the actual call. This is a one-line difference from a POSIX gthread layer, and it is worth checking again when the toolchain changes.

**Time.** `__gthread_cond_timedwait` and `__gthread_mutex_timedlock` receive an **absolute** time. libstdc++ builds it from `std::chrono::steady_clock` (for `wait_until`/`try_lock_until`) or `std::chrono::system_clock`, casts it to seconds and nanoseconds, and expects the gthread layer to know the epoch. The port makes both clocks tick from system start-up, then converts the absolute time back to a relative tick count at the call.

# 3. The StateOS original

StateOS maps the contract onto its own kernel:

| gthread concept | StateOS |
| --- | --- |
| `__gthread_mutex_t` | `mtx_t`, initialised by `_MTX_INIT (mtxPrioInherit|mtxErrorCheck, 0)` |
| `__gthread_recursive_mutex_t` | `mtx_t` with `mtxRecursive` |
| `__gthread_cond_t` | `cnd_t`, `_CND_INIT ()` |
| `__gthread_once_t` | `one_t`, `_ONE_INIT ()` |
| `__gthread_t` | `tsk_t*` |
| `__gthread_key_t` | `oskey_t*`, a per-key `std::unordered_map<__gthread_t, void*>` |
| `__gthread_time_t` | `ostime_t : timespec`, converted with `systick::until()` |

Thread creation is `tsk_setup (OS_MAIN_PRIO, func, args, OS_STACK_SIZE)`; join and detach are `tsk_join`/`tsk_detach`; the current thread is `tsk_this`; yielding is `tsk_yield`; sleeping is `tsk_sleepFor`.

The StateOS `inc/*.hh` files do not use the public API for `barrier`/`latch`/`semaphore`; they call internal wait-list primitives (`core_tsk_waitFor`, `core_all_wakeup`, `core_one_wakeup`) under a `critical_section`. That is why the C++20 headers had to be adapted rather than used unchanged.

# 4. What a bare-metal libstdc++ provides

The toolchain's `aarch64-none-elf/bits/c++config.h` says, in comments that are the preprocessor's way of saying "not defined":

```c
/* #undef _GLIBCXX_HAS_GTHREADS */
/* #undef _GLIBCXX_HAVE_TLS */
/* #undef _GLIBCXX_HAVE_LINUX_FUTEX */
/* #undef _GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT */
/* #undef _GLIBCXX_USE_PTHREAD_MUTEX_CLOCKLOCK */
/* #undef _GLIBCXX_USE_PTHREAD_RWLOCK_T */
```

`_GLIBCXX_HOSTED` is `__STDC_HOSTED__`, which is `1` for newlib.

The consequences, verified against this toolchain:

| Header | Without the port | With the port |
| --- | --- | --- |
| `<mutex>` | no `std::mutex` at all; `std::once_flag` is the single-threaded variant | full mutex family and `std::call_once` |
| `<thread>` | `std::thread` exists but cannot create a thread | `std::thread` and `std::jthread` |
| `<condition_variable>` | no `std::condition_variable` | the full class |
| `<future>` | `std::promise`, `std::future`, `std::async` are unavailable | the full future library |
| `<chrono>` | clocks exist but need `now()` definitions from the library | `system_clock`/`steady_clock` over the kernel clock |
| `<barrier>`, `<latch>`, `<semaphore>` | GCC 15 does enable these (its generic atomic-wait layer is a mutex and a condition variable), so they would compile | the package's kernel-backed versions, found first |
| `thread_local` | GCC uses emulated TLS on this target; the entry points are missing | `src/emutls.cc` provides them |

The C++20 case deserves a note. `bits/atomic_wait.h` falls back to a `mutex` + `__condvar` when there is no platform wait; `bits/version.h` therefore defines `__cpp_lib_atomic_wait`, and `<semaphore>`/`<barrier>`/`<latch>` become available. They would work on top of the gthread layer. The package still shadows them, as StateOS does, so that a waiting thread blocks in the scheduler's semaphore/mutex/condition variable rather than in libstdc++'s waiter pool. The feature macros are still defined (the headers set them if absent), so code that tests `__cpp_lib_semaphore` sees the expected value.

---

# Part II — The port

# 5. Design constraints

Five properties of this target shaped the port.

**1. Kernel headers and `<mutex>` include each other.** `<bits/gthr.h>` is parsed from inside `<mutex>`. The µOS++ header `cmsis-plus/rtos/os-memory.h` includes `<mutex>`. If `gthr-default.h` included `os.h`, then `<mutex>` → gthr → `os.h` → `os-memory.h` → `<mutex>` would reach a `std::mutex` that is not yet defined. The port therefore keeps **all kernel headers out of `gthr-default.h`**: the header declares fixed-size opaque storage and `extern "C"` functions; `src/gthread.cc` is the only file that sees both sides.

**2. No exceptions, no RTTI.** The platform compiles with `-fno-exceptions -fno-rtti`. libstdc++'s `__throw_*` helpers call `abort()` instead. The port must not rely on catching anything, and a failed lock/join is a visible abort rather than a catchable error.

**3. Static initialisation happens before the scheduler.** Static constructors run from `startup.S` before `main()`, and the kernel's `os::rtos::mutex` constructor is safe there (it only asserts that it is not in handler mode). Namespace-scope `std::mutex` objects are therefore possible — but function-local statics are avoided, because the toolchain's `__cxa_guard_*` was built without gthreads.

**4. SMP.** The 4 cores run the same code; mutexes, condition variables and semaphores are the kernel's, so cross-core wake-ups go through the scheduler. The port adds no locks of its own except two small ones for the key table and the detached-thread table.

**5. A flat address space with a real free store.** Threads are heap-allocated `os::rtos::thread` objects; `malloc`/`new` are backed by the µOS++ first-fit heap (64 MiB in this image). There is no MMU protection for thread stacks beyond what the port provides.

# 6. Layers

```text
     application / test
            │  #include <thread>, <future>, <mutex>, <semaphore> ...
            ▼
     toolchain C++ headers
       <mutex>, <thread>, <future>, <condition_variable>, <chrono>, <atomic_wait>
            │  #include <bits/gthr.h>
            ▼
     third_party/stateos-stdc++/include/bits/gthr-default.h
       opaque storage + extern "C" __gthread_* declarations
            │
            ▼
     third_party/stateos-stdc++/src/gthread.cc      (the only kernel-aware file)
            │
            ▼
     micro-os-plus-iii-smp/include/cmsis-plus/rtos/os.h
       mutex, mutex_recursive, condition_variable, semaphore_counting,
       thread, clock_systick, sysclock
            │
            ▼
     micro-os-plus-iii-aarch64  (AArch64 port, GIC, generic timer, SMP scheduler)
```

The other `.cc` files sit beside `gthread.cc` and implement the libstdc++ classes themselves; they call only the gthread functions and standard C++.

# 7. Opaque storage

`gthr-default.h` defines the three embedded types as one aligned byte block:

```c++
#define __GTHREAD_STORAGE_ALIGNMENT 16
#define __GTHREAD_STORAGE_BYTES 256

struct __gthread_object_storage
{
  alignas (__GTHREAD_STORAGE_ALIGNMENT) unsigned char
      __data[__GTHREAD_STORAGE_BYTES];
};

typedef __gthread_object_storage __gthread_mutex_t;
typedef __gthread_object_storage __gthread_recursive_mutex_t;
typedef __gthread_object_storage __gthread_cond_t;
```

`gthread.cc` constructs the kernel object in place:

```c++
new (mutex->__data) mutex_t ();
```

and static-asserts that the sizes and alignments still fit:

```c++
static_assert (sizeof (os::rtos::mutex) <= __GTHREAD_STORAGE_BYTES, ...);
static_assert (sizeof (os::rtos::mutex_recursive) <= __GTHREAD_STORAGE_BYTES, ...);
static_assert (sizeof (os::rtos::condition_variable) <= __GTHREAD_STORAGE_BYTES, ...);
static_assert (alignof (os::rtos::mutex) <= __GTHREAD_STORAGE_ALIGNMENT, ...);
```

**Cost.** `sizeof (std::mutex)` and `sizeof (std::condition_variable)` become at least 256 bytes, not the ~40 bytes of a hosted build. That is the price of not including the kernel headers from inside `<mutex>`. The alternatives — a `void*` indirection (another allocation per mutex) or a generated header with the exact sizes — were rejected as heavier or more brittle. The assertion makes a future size change a compile error rather than memory corruption.

**Everything is out of line.** `__gthread_mutex_lock()` and friends are real function calls into `gthread.cc`, not inline code. A mutex operation therefore costs one call plus the kernel's lock. On this target that is not the dominant cost: the kernel's own mutex path, the scheduler and the cache effects are larger.

# 8. Mutexes

```c++
extern "C" void __gthread_mutex_init (__gthread_mutex_t* m)
{ new (m->__data) mutex_t (); }

extern "C" int __gthread_mutex_lock (__gthread_mutex_t* m)
{ return as_mutex (m)->lock (); }

extern "C" int __gthread_mutex_trylock (__gthread_mutex_t* m)
{ return as_mutex (m)->try_lock () == os::rtos::result::ok ? 0 : EBUSY; }

extern "C" int __gthread_mutex_timedlock (__gthread_mutex_t* m,
                                          const __gthread_time_t* abs)
{ return as_mutex (m)->timed_lock (timeout_ticks (abs)); }

extern "C" int __gthread_mutex_unlock (__gthread_mutex_t* m)
{ return as_mutex (m)->unlock (); }
```

The mapping is direct because µOS++ returns POSIX-style `result_t` values and uses `0` for success (`os::rtos::result::ok`).

- **Non-recursive mutex** → `os::rtos::mutex`, default attributes.
- **Recursive mutex** → `os::rtos::mutex_recursive`, which counts depth and releases only at zero.
- **`try_lock`** returns `EWOULDBLOCK` from the kernel; the gthread layer reports `EBUSY`, because that is what libstdc++'s `try_lock()` expects to turn into `false`.
- **Timed lock** converts the absolute time to a relative tick count (chapter 10) and returns `ETIMEDOUT` unchanged.
- **Destruction** calls the destructor; the kernel asserts that no thread owns the mutex and no one is waiting.

`std::mutex`'s constructor and destructor are supplied by libstdc++ through `__GTHREAD_MUTEX_INIT_FUNCTION` and `__gthread_mutex_destroy`; the port does not define their bodies.

# 9. Condition variables

```c++
extern "C" void __gthread_cond_init (__gthread_cond_t* c)
{ new (c->__data) cond_t (); }

extern "C" int __gthread_cond_wait (__gthread_cond_t* c,
                                    __gthread_mutex_t* m)
{ return as_cond (c)->wait (*as_mutex (m)); }

extern "C" int __gthread_cond_timedwait (__gthread_cond_t* c,
                                         __gthread_mutex_t* m,
                                         const __gthread_time_t* abs)
{ return as_cond (c)->timed_wait (*as_mutex (m), timeout_ticks (abs)); }
```

`signal()` maps to `notify_one()`, `broadcast()` to `notify_all()`. The kernel's `wait(mutex&)` releases the mutex and reacquires it before returning; `timed_wait` adds the timeout and returns `ETIMEDOUT`.

libstdc++'s `condition_variable::wait(unique_lock&, predicate)` is a header-side loop around these calls; no extra porting is needed. The `std::condition_variable` constructor is `= default` in the port's `condition_variable.cc`, so the `__condvar` base constructor calls `__GTHREAD_COND_INIT_FUNCTION` — which is why that macro must be defined.

# 10. Time: absolute timespec to relative ticks

libstdc++ builds an absolute `__gthread_time_t` from a chrono time point:

```c++
auto s  = chrono::time_point_cast<chrono::seconds>(abs_time);
auto ns = chrono::duration_cast<chrono::nanoseconds>(abs_time - s);
__gthread_time_t ts = { (std::time_t) s.time_since_epoch().count(),
                        (long) ns.count() };
```

The port defines the type as a plain two-field aggregate:

```c++
struct __gthread_time_t
{
  long long tv_sec;
  long tv_nsec;
};
```

`timeout_ticks()` converts it to what µOS++ expects:

```c++
constexpr std::int64_t billion = 1000000000LL;
constexpr std::int64_t freq = os::rtos::clock_systick::frequency_hz; // 1000

std::int64_t ns = abs->tv_sec * billion + abs->tv_nsec;
std::int64_t abs_ticks = (ns * freq + billion - 1) / billion;  // round up
std::int64_t now_ticks = (std::int64_t) os::rtos::sysclock.steady_now ();
std::int64_t rel = abs_ticks - now_ticks;
return rel <= 0 ? 0 : rel >= UINT32_MAX ? UINT32_MAX - 1 : (duration_t) rel;
```

Four decisions are visible here:

1. **The epoch is start-up.** `chrono.cc` makes `steady_clock::now()` and `system_clock::now()` return `sysclock.steady_now()`, so libstdc++'s absolute time and the kernel's timestamp share an epoch. The conversion is subtraction, not a calendar conversion.
2. **Round up.** A delay is never shorter than requested. A 1 ns request becomes one tick.
3. **Zero means "poll".** A time already in the past becomes 0 ticks; `timed_lock(0)`/`timed_wait(0)` return `ETIMEDOUT` immediately, which is the correct `try_lock_until` result.
4. **Saturate, do not wrap.** `duration_t` is 32-bit; a far-future time saturates to `UINT32_MAX - 1`, which is the kernel's "practically infinite" duration.

`inc/chrono.hh` exposes the same arithmetic as `std::chrono::systick::count(duration)` and `until(time_point)` for the C++20 headers, which need it in templates.

# 11. Threads

A `std::thread` is a heap-allocated `os::rtos::thread`; the `__gthread_t` handle is the address of that object.

## 11.1 Creation

```c++
struct thread_start
{
  void (*func) (void*);
  void* arg;
};

extern "C" int
__gthread_create (__gthread_t* thread, void (*func) (void*), void* args)
{
  thread_start* start = new thread_start{ func, args };

  os::rtos::thread::attributes attr;
  attr.th_stack_size_bytes = thread_stack_bytes;   // 16 KiB on AArch64
  attr.th_priority = os::rtos::thread::priority::normal;

  os::rtos::thread* th =
      new os::rtos::thread{ "stdc++", &thread_trampoline, start, attr };

  *thread = static_cast<__gthread_t> (static_cast<void*> (th));
  return 0;
}
```

µOS++ threads start **when constructed**; there is no separate `start()`. The default allocator takes the 16 KiB stack from the free store.

The trampoline calls the libstdc++ routine and then runs the per-thread destructors:

```c++
void* thread_trampoline (void* p)
{
  thread_start* start = static_cast<thread_start*> (p);
  void (*func) (void*) = start->func;
  void* arg = start->arg;
  delete start;

  func (arg);
  __gthread_key_atexit ();
  return nullptr;
}
```

`execute_native_thread_routine` (in `thread.cc`) also calls `__gthread_key_atexit()` after `_M_run()`. The second call finds an empty table and is harmless; keeping it in the trampoline means the cleanup does not depend on which libstdc++ routine was passed to `__gthread_create`.

## 11.2 Identity

`__gthread_self()` returns `&os::rtos::this_thread::thread()`. libstdc++ stores that value in `std::thread::id` and compares it with `==`/`<=>`, so the address of the kernel thread object is a valid identity. It also works for the main thread, which was not created by `std::thread`.

`__gthread_equal` is a pointer comparison. `__gthread_yield` calls `os::rtos::this_thread::yield()`.

## 11.3 Join and detach

```c++
extern "C" int __gthread_join (__gthread_t thread, void** value_ptr)
{
  auto* th = static_cast<os::rtos::thread*> (thread);
  const int result = th->join (value_ptr);
  delete th;
  return result;
}
```

After `join()` the kernel has terminated the thread, and the object can be deleted (the project's other tests use the same `join(); delete` sequence).

A detached thread cannot delete its own `thread` object — the destructor would call `kill()` — so `__gthread_detach` only calls `detach()` and remembers the pointer:

```c++
constexpr unsigned detached_slots = 8;
os::rtos::thread* detached_threads[detached_slots] = {};
os::rtos::mutex detached_mutex{ "stdc++-detached" };
```

On each detach, terminated entries are deleted, then the new one is stored if there is room (`thread::state() == thread::state::terminated` says the kernel is done with it). Eight live detached threads are tracked; beyond that the object is left to the application. This is a bounded, deliberate leak, not a general-purpose reaper.

## 11.4 `std::thread`'s own part

`thread.cc` keeps the libstdc++ `std::thread` implementation almost unchanged:

- `thread::join()`, `thread::detach()`, `thread::_M_start_thread(_State_ptr, void (*)())`;
- the ABI-compatible `_M_start_thread(__shared_base_type, ...)`;
- `thread::hardware_concurrency()` returns `OS_NCPU`;
- `this_thread::__sleep_for(seconds, nanoseconds)` converts both parts to ticks, saturates, and calls `sysclock.sleep_for()`.

`_GLIBCXX_THREAD_ABI_COMPAT` paths are kept, so code built against the older ABI still works.

# 12. `std::call_once`

`once_flag` is a 4-byte `__gthread_once_t` (`int`), initialised to `0`. The implementation is a three-state flag:

```c++
int __gthread_once (__gthread_once_t* once, void (*func) (void))
{
  int expected = 0;
  if (__atomic_compare_exchange_n (once, &expected, 1, false,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      func ();
      __atomic_store_n (once, 2, __ATOMIC_RELEASE);
      return 0;
    }
  while (__atomic_load_n (once, __ATOMIC_ACQUIRE) != 2)
    {
      os::rtos::this_thread::yield ();
    }
  return 0;
}
```

`0` = not run, `1` = running, `2` = done. A second caller yields until the first finishes. The function is not re-entrant for the same flag, which matches the C++ rule.

Because `_GLIBCXX_HAVE_TLS` is undefined, `<mutex>` selects the global-functor path for `std::call_once`: the callable is stored in a global `std::function`, and a global `std::mutex` serialises callers before `__gthread_once`. `mutex.cc` provides `__once_functor`, `__get_once_mutex()`, `__set_once_functor_lock_ptr()` and `__once_proxy()`. StateOS used a function-local static for that mutex; this port uses namespace-scope objects to avoid the toolchain's static-initialisation guard, which was built without gthreads.

# 13. Thread-specific data and TLS

## 13.1 The key table

`thread.cc` implements `__gthread_key_create/delete/getspecific/setspecific`. A key is an `oskey_t`, an `std::unordered_map<__gthread_t, void*>`; a global map holds the destructor for each key:

```c++
static os::rtos::mutex key_mutex{ "stdc++-keys" };
struct oskey_t : public std::unordered_map<__gthread_t, void*>
{ using unordered_map<__gthread_t, void*>::unordered_map; };
static std::unordered_map<__gthread_key_t, void (*) (void*)> key_map{};
```

The key type is `void*`, so the implementation casts it back to `oskey_t*` at each entry. `__gthread_getspecific` looks up the current thread's value; `__gthread_setspecific` inserts or assigns it.

## 13.2 Running the destructors

`__gthread_key_atexit()` walks `key_map` for the current thread, calls each destructor with its value, and erases the entry. It is called at thread exit (section 11.1) and is what makes `notify_all_at_thread_exit` and `std::future`'s `_Make_ready` work.

`condition_variable.cc` registers a key whose destructor (`run`) walks the `__at_thread_exit_elt` list. `__at_thread_exit` chains an element onto the current thread's list under a `__gthread_once`-protected key; at thread exit, the list runs.

## 13.3 Emulated TLS

`_GLIBCXX_HAVE_TLS` is deliberately **not** defined: the target has no native TLS block, and libstdc++ would use `__thread` variables that no thread sets up. GCC instead compiles `thread_local` in user code to calls to `__emutls_get_address()`. `emutls.cc` implements the GCC emulated-TLS interface:

```c++
struct __emutls_object
{
  size_t size;    // object size
  size_t align;   // object alignment
  void*  key;     // __gthread_key_t
  void*  init;    // initial value
};
```

The first access for a given `thread_local` creates a key; the first access in a thread allocates one block and stores it under that key; the thread's destructor frees it.

**A real bug this exposed.** The StateOS original used `memalign()`. In this workspace `_memalign_r()` is a deliberate stub that calls `abort()` (µOS++'s libc only implements `malloc`/`free`/`calloc`/`realloc`), so the first `thread_local` access aborted. The port allocates manually:

```c++
size_t align = obj->align < sizeof (void*) ? sizeof (void*) : obj->align;
unsigned char* raw = (unsigned char*) ::malloc (obj->size + align + sizeof (void*));
uintptr_t base = (uintptr_t) raw + sizeof (void*);
uintptr_t aligned = (base + (align - 1)) & ~(uintptr_t) (align - 1);
void* ptr = (void*) aligned;
static_cast<void**> (ptr)[-1] = raw;   // for the destructor
```

and the key's destructor is `emutls_free`, which reads the raw pointer back and calls `::free`. This was found on the first hardware/QEMU run, not by reading the code — see chapter 32.

# 14. `future.cc` and the at-thread-exit chain

`future.cc` is the libstdc++ source for the future error category and for `__future_base::_State_baseV2::_Make_ready`. The latter registers a callback with `__at_thread_exit(this)`; its `_S_run` sets the shared state to ready and notifies the waiters with release semantics.

`std::promise`, `std::future`, `std::shared_future`, `std::async` and `std::packaged_task` are otherwise header templates in `<future>`. They use:

- `std::mutex` and `std::condition_variable` through the gthread layer;
- `__atomic_futex_unsigned` (`bits/atomic_futex.h`). Without Linux futexes it falls back to a mutex and a condition variable, so `future::wait()` blocks in the kernel;
- `__at_thread_exit` and the key table for `set_value_at_thread_exit`/`make_ready_at_thread_exit`.

No additional `.cc` file is needed for the future library itself.

# 15. `chrono.cc` and `inc/chrono.hh`

Both clocks count system ticks from start-up:

```c++
constexpr bool system_clock::is_steady;
system_clock::time_point system_clock::now () noexcept
{
  return time_point (systick::duration (
      static_cast<systick::rep> (os::rtos::sysclock.steady_now ())));
}
```

`systick` (in `inc/chrono.hh`) is:

```c++
using rep        = std::int64_t;
using period     = ratio<1, OS_INTEGER_SYSTICK_FREQUENCY_HZ>;   // 1/1000
using duration   = chrono::duration<rep, period>;
using time_point = chrono::time_point<systick, duration>;
```

with `count(duration)` returning rounded-up `os::rtos::clock::duration_t` ticks and `until(time_point)` returning `count(tp - Clock::now())`. `this_thread::sleep_for` uses the same conversion, so a `sleep_for (1ms)` is one tick.

The kernel tick frequency is 1000 Hz (`OS_INTEGER_SYSTICK_FREQUENCY_HZ`), so one tick is one millisecond. The conversion is exact for millisecond and second durations and rounds up for anything finer.

# 16. C++20 `barrier`, `latch`, `semaphore`

The three headers are found before the toolchain's, through the same include guards (`_GLIBCXX_BARRIER`, `_GLIBCXX_LATCH`, `_GLIBCXX_SEMAPHORE`), so a later `#include <barrier>` from libstdc++ internals also reaches these definitions.

## 16.1 `latch`

```c++
class latch
{
  os::rtos::mutex              _M_mutex;
  os::rtos::condition_variable _M_cond;
  ptrdiff_t                    _M_latch;
};
```

`count_down(n)` locks, subtracts, and broadcasts when the count reaches zero; `wait()` waits while the count is non-zero; `try_wait()` locks and reads; `arrive_and_wait(n)` is `count_down(n); wait()`.

## 16.2 `barrier`

`arrive(n)` locks, subtracts from `_M_pending`, and when it reaches zero runs the completion function, increments `_M_phase`, resets `_M_pending` to `_M_expected`, and broadcasts. `wait(token)` waits while `token == _M_phase`. `arrive_and_drop()` decrements both `_M_expected` and `_M_pending`.

> **Contract of this implementation:** the completion function runs with the barrier's mutex held. It must not call back into the same barrier. This is stated in the header and is the price of keeping the phase transition atomic with the state update.

## 16.3 `semaphore`

```c++
template<ptrdiff_t __least_max_value = __PTRDIFF_MAX__>
class counting_semaphore
{
  os::rtos::semaphore_counting _M_sem;
};
```

- `release(n)` posts `n` times;
- `acquire()` waits;
- `try_acquire()` maps `EWOULDBLOCK` to `false`;
- `try_acquire_for/until` use `systick::count/until` and `timed_wait`.

The kernel's counter is 16 bits (`semaphore::max_count_value == 0x7FFF`). The template's `max()` returns `__least_max_value`, but the kernel is created with `min(__least_max_value, 0x7FFF)`; releasing more than 32767 tokens would be refused by the kernel (`EAGAIN`, ignored here). Use a small `__least_max_value` (as `binary_semaphore` and the tests do) or stay under the kernel limit.

`binary_semaphore` is `counting_semaphore<1>`.

# 17. File-by-file reference

| File | Responsibility | Key symbols |
| --- | --- | --- |
| `include/bits/gthr-default.h` | the contract: types, macros, prototypes | `__gthread_*`, `__GTHREAD_*` |
| `src/gthread.cc` | the implementation over the kernel | mutex/cond/once/thread functions, `timeout_ticks`, `thread_trampoline` |
| `src/thread.cc` | libstdc++ `std::thread`; key table; sleep | `thread::join/detach/_M_start_thread`, `__gthread_key_*`, `__gthread_key_atexit` |
| `src/mutex.cc` | `std::call_once` support | `__once_functor`, `__once_proxy`, `__get_once_mutex` |
| `src/condition_variable.cc` | `std::condition_variable`; at-thread-exit | `wait`, `notify_one/all`, `__at_thread_exit`, `notify_all_at_thread_exit` |
| `src/future.cc` | future error category; `_Make_ready` | `future_category`, `_S_run` |
| `src/chrono.cc` | the two clocks | `system_clock::now`, `steady_clock::now` |
| `src/emutls.cc` | emulated TLS | `__emutls_get_address`, `__emutls_register_common` |
| `include/inc/chrono.hh` | tick conversion for the C++20 headers | `chrono::systick` |
| `include/inc/barrier.hh` | `std::barrier` | `barrier`, `__empty_completion` |
| `include/inc/latch.hh` | `std::latch` | `latch` |
| `include/inc/semaphore.hh` | `std::counting_semaphore` | `counting_semaphore`, `binary_semaphore` |
| `include/inc/critical_section.hh` | scheduler critical section helper | `critical_section` |
| `include/{barrier,latch,semaphore}` | wrappers found before the toolchain's headers | — |
| `CMakeLists.txt` | the `stateos-stdc++` INTERFACE target | `_GLIBCXX_HAS_GTHREADS`, BEFORE include |
| `README.md` | concise library notes | — |

# 18. Differences from the StateOS original

| Aspect | StateOS | This port |
| --- | --- | --- |
| Kernel | StateOS (`mtx_t`, `cnd_t`, `tsk_t`, `one_t`) | µOS++ (`os::rtos::mutex`, `condition_variable`, `thread`) |
| Thread handle | `tsk_t*` | `os::rtos::thread*` |
| Thread creation | `tsk_setup (OS_MAIN_PRIO, ..., OS_STACK_SIZE)` | `new os::rtos::thread{..., 16 KiB stack}` |
| Mutex storage | `_MTX_INIT (...)` POD initialiser | opaque 256-byte storage + placement new |
| Time | `ostime_t::to_ticks()` absolute | absolute timespec → relative ticks |
| TLS | `_GLIBCXX_HAVE_TLS 1` with a fake `__aeabi_read_tp` | `_GLIBCXX_HAVE_TLS` undefined; emulated TLS |
| `std::call_once` | global functor, function-local static mutex | global functor, namespace-scope mutex |
| `barrier`/`latch`/`semaphore` | internal wait lists + `critical_section` | kernel mutex/condition variable/semaphore |
| Detached threads | kernel handles them | 8-slot reaper table |
| `critical_section` | `core_set_lock` / `core_put_lock` | `os::rtos::scheduler::critical_section` |
| Alignment allocation | `memalign()` | `malloc()` + manual alignment (µOS++ has no `memalign`) |

---

# Part III — Building a test with it

# 19. The CMake target

`third_party/stateos-stdc++/CMakeLists.txt` is an INTERFACE library, not a standalone project:

```cmake
if (NOT TARGET stateos-stdc++)
  add_library (stateos-stdc++ INTERFACE)

  target_include_directories (
    stateos-stdc++ BEFORE INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

  target_compile_definitions (stateos-stdc++ INTERFACE _GLIBCXX_HAS_GTHREADS=1)

  target_sources (
    stateos-stdc++ INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gthread.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/thread.cc"
    ... )

  if (TARGET micro-os-plus::iii)
    target_link_libraries (stateos-stdc++ INTERFACE micro-os-plus::iii)
  endif ()
endif ()
```

Three properties are load bearing:

1. **`BEFORE`.** The include directory must precede the toolchain's C++ headers. If it does not, `<bits/gthr.h>` finds the toolchain's own `gthr-default.h`, which on this target is the **single-threaded no-op** implementation (`gthr-single.h`): with `_GLIBCXX_HAS_GTHREADS` defined the classes would compile, but every lock would silently do nothing. `<barrier>`/`<latch>`/`<semaphore>` would also find the toolchain's bodies instead of the kernel-backed ones.
2. **`_GLIBCXX_HAS_GTHREADS=1` as an INTERFACE define.** The toolchain's `c++config.h` leaves it undefined. Every translation unit of the test — including the library's own sources — gets the define.
3. **INTERFACE sources.** The sources are compiled once per consuming target (once for `smp_test4-qemu`, once for `smp_test4-hwd`), with that target's defines and include path. No separate object library is needed, and a change to the library rebuilds exactly the tests that use it.

`micro-os-plus::iii` is linked only if it already exists. In this workspace the test links `micro-os-plus::aarch64`, which brings the kernel, so the kernel include directories are present either way; the conditional link keeps the target usable from a context that adds the kernel itself.

# 20. Hooking the library into a board test

The port's `test/rpi4b/tests.cmake` creates the target once and names it from the library hook:

```cmake
# ---- stateos-stdc++ ---------------------------------------------------------
set (_stateos_stdcpp
     "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/stateos-stdc++")
if (NOT IS_DIRECTORY "${_stateos_stdcpp}")
  # The library is kept once, in the examples repository's third_party/;
  # a scaffolded workspace is one directory deeper.
  set (_stateos_stdcpp
       "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/stateos-stdc++")
endif ()
if (NOT TARGET stateos-stdc++)
  add_subdirectory ("${_stateos_stdcpp}" "${CMAKE_BINARY_DIR}/stateos-stdc++-bin")
endif ()
```

```cmake
function (board_test_libs _app _out)
  if (_app STREQUAL "smp_test4")
    set (${_out} stateos-stdc++ uos-micro-test-plus PARENT_SCOPE)
    return ()
  endif ()
  ...
endfunction ()
```

`smp_test4` is not an `mtp-*` name, so the generic µTest++ branch does not catch it; it gets both `stateos-stdc++` and `uos-micro-test-plus` explicitly.

The library is kept **once**, in the repository's `third_party/stateos-stdc++/`. The workspace has a relative symlink:

```text
rpi4b-smp/third_party/stateos-stdc++ -> ../../third_party/stateos-stdc++
```

so both path forms in the CMake snippet resolve, and the workspace remains a single copy of the sources.

# 21. The test directory

```text
micro-os-plus-iii-aarch64/test/rpi4b/smp_test4/
├── main.cpp              start, release the cores, run the suites
├── suite-thread.cpp      std::thread, std::jthread, ids, sleep, TLS
├── suite-promise.cpp     promise/future, wait_for, shared_future
├── suite-future.cpp      packaged_task, async, set_value_at_thread_exit
├── suite-mutex.cpp       mutex, timed/recursive mutex, call_once, unique_lock
├── suite-sync.cpp        condition_variable, semaphores, latch, barrier, shared_mutex
└── suite-chrono.cpp      clocks, sleeps, duration conversions
```

There is no suite list anywhere: each file defines a `mt::static_suite` at namespace scope, and the runner discovers it through the static constructor.

## 21.1 `main.cpp`

```c++
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
```

`mtp_board::start()` installs the console and prints the banner; `release_cores()` pins the runner to core 0 and releases cores 1–3 into the SMP scheduler; `run()` parses the command line, runs the suites and prints the RESULT line. `mtp_startup.hpp` supplies the `main()` trampoline, the startup hooks and the `--wrap=os_main` verdict path.

## 21.2 A suite

```c++
#include <mtp_board.hpp>
#include <atomic>
#include <chrono>
#include <thread>

namespace { std::atomic<int> g_counter{ 0 }; }

mt::static_suite suite_thread{
  "01 - std::thread and TLS", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("join: a thread runs, its writes are visible after join ()",
             [] (auto& t) {
               g_counter = 0;
               std::thread th ([] {
                 for (int i = 0; i < 4000; ++i)
                   g_counter.fetch_add (1, std::memory_order_relaxed);
               });
               t.expect (th.joinable ()) << "joinable before join ()";
               th.join ();
               t.expect (mt::eq (g_counter.load (), 4000))
                   << "every increment visible after join ()";
             });
  }
};
```

A check is `t.expect (matcher) << "message"`; `mt::eq (a, b)` is the equality matcher. The runner prints TAP and returns the exit code.

## 21.3 The promise/future test, annotated

```c++
ts.test ("promise<int>: the value set by a thread reaches get ()",
         [] (auto& t) {
           std::promise<int> promise;
           std::future<int> future = promise.get_future ();
           t.expect (future.valid ()) << "a future is attached";

           std::thread th ([p = std::move (promise)] () mutable {
             std::this_thread::sleep_for (5ms);
             p.set_value (41 + 1);
           });

           t.expect (mt::eq (future.get (), 42)) << "get () waited for the value";
           th.join ();
           t.expect (!future.valid ()) << "get () releases the state";
         });
```

What it exercises end to end:

- `std::promise` moves into the lambda (a move-only move capture);
- `std::thread` starts a kernel thread from the free store;
- `sleep_for` converts 5 ms to 5 ticks and calls `sysclock.sleep_for`;
- `set_value` locks the shared state, stores the value, sets the status and wakes the waiter through the kernel condition variable;
- `get()` blocks in `future::wait()` on that condition variable until the value is ready;
- `future.get()` invalidates the state.

The suite also covers `promise<void>`, `wait_for` status, `shared_future` read from two threads, and a promise moved between threads.

## 21.4 The synchronization test, annotated

```c++
ts.test ("barrier: one completion per phase, all threads pass",
         [] (auto& t) {
           std::atomic<int> completions{ 0 };
           std::atomic<int> passed{ 0 };
           std::barrier sync (3, [&] {
             completions.fetch_add (1, std::memory_order_relaxed);
           });

           auto worker = [&] {
             sync.arrive_and_wait ();
             passed.fetch_add (1, std::memory_order_relaxed);
           };

           std::thread a (worker);
           std::thread b (worker);
           sync.arrive_and_wait ();

           a.join ();
           b.join ();

           t.expect (mt::eq (completions.load (), 1)) << "the completion ran once";
           t.expect (mt::eq (passed.load (), 2)) << "both workers passed";
         });
```

The barrier has three participants: the runner and two threads. The completion runs in whichever thread arrives last, once per phase; all three return after the phase changes.

## 21.5 What the test deliberately does not use

- **Exceptions.** The build is `-fno-exceptions`; `set_exception`, `future_error` handling and catch blocks are not exercised. A failed kernel call aborts with `abort(), exiting...` on the trace console.
- **Native TLS.** `thread_local` is tested, but through emulated TLS; there is no `_GLIBCXX_HAVE_TLS`.
- **Unbounded detached threads.** One detach test runs; the reaper table is eight slots by design.
- **Huge semaphore counts.** `binary_semaphore` and `counting_semaphore<4>` stay under the kernel's 16-bit counter.

# 22. Runtime behaviour and limits

| Topic | Behaviour | Consequence |
| --- | --- | --- |
| Mutex/cond storage | 256 bytes each | A test with many mutexes uses more RAM than a hosted build |
| Thread stack | 16 KiB from the free store | 50 threads ≈ 800 KiB plus objects |
| `thread_local` | one heap block per variable per thread | Freed at thread exit by the key destructor |
| Detached thread | object tracked in 8 slots | Bounded leak by design |
| `call_once` | three-state atomic + yield | Not re-entrant on the same flag |
| Barrier completion | runs with the barrier mutex held | Must not call the barrier |
| Semaphore count | 16-bit kernel counter | `max()` may be larger than the kernel allows |
| Exceptions | abort | No `std::future_error` recovery |
| Static init | namespace-scope mutexes are constructed before the scheduler | Function-local statics are avoided in the port |

---

# Part IV — CTest

# 23. From a directory to a CTest case

The port's `micro-os-plus-iii-aarch64/test/CMakeLists.txt` globs the directories under `test/<board>/`:

```cmake
file (GLOB _test_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${_test_dir}/*")
foreach (_dir IN LISTS _test_dirs)
  get_filename_component (_app "${_dir}" NAME)
  file (GLOB _srcs CONFIGURE_DEPENDS "${_dir}/*.cpp")
  if (NOT _srcs)
    continue ()
  endif ()
  ...
  foreach (_variant IN LISTS _variants)      # qemu and hwd
    uos_add_app ("${_app}-${_variant}" ...)
  endforeach ()
endforeach ()
```

`test/rpi4b/smp_test4/` therefore produces two targets and two CTest cases without any list to edit:

```text
smp_test4-qemu        →  aarch64-rpi4b-smp_test4-qemu
smp_test4-hwd         →  aarch64-rpi4b-smp_test4-hwd
```

The platform's `tests/platforms/aarch64-rpi4b/CMakeLists.txt` registers both with `add_test()`:

```cmake
add_test (NAME "${PLATFORM_NAME}-${_app}-qemu"
          COMMAND bash "${_runner}/run-qemu.sh" "${_port_bin}" "${_qemu}"
                  -M raspi4b -smp 4)
set_tests_properties (... PROPERTIES
  ENVIRONMENT "UOS_QEMU_ONLY=${_app};UOS_TEST_SRC_DIR=${_port_test_dir}"
  LABELS "qemu" TIMEOUT 2400)

add_test (NAME "${PLATFORM_NAME}-${_app}-hwd"
          COMMAND bash "${_board_hw}" "${_app}" 1800)
set_tests_properties (... PROPERTIES
  ENVIRONMENT "BUILD=${CMAKE_CURRENT_BINARY_DIR}/port-tests"
  LABELS "hwd" TIMEOUT 2400)
```

The test directory needs no per-test settings. `smp_test4` is built exactly like `mtp-smp`; only `board_test_libs()` differs, adding `stateos-stdc++`.

# 24. The one entry point: `tests/run.sh`

```bash
cd rpi4b-smp/tests

./run.sh build                 # configure and build every target
./run.sh list                  # every CTest case, * = run by default
./run.sh smp_test4             # build, then the QEMU case
./run.sh --no-build smp_test4  # skip the build
./run.sh smp_test4 --hwd       # the hardware case
./run.sh --hwd-only smp_test4  # only the hardware image
./run.sh --no-build -v smp_test4
```

`run.sh` resolves a name in this order:

1. an exact CTest name (`aarch64-rpi4b-smp_test4-qemu`);
2. an application directory (`smp_test4`), which matches `*-smp_test4-qemu` and `*-smp_test4-hwd`;
3. a selection id from the configuration (none for this test).

The default selection in `run.sh` includes both `smp_test4` cases, so a plain `./run.sh` runs it with the rest of the workspace:

```bash
SELECTED=(... 'aarch64-rpi4b-smp_test4-qemu' 'aarch64-rpi4b-smp_test4-hwd')
```

Hardware cases are filtered out unless `--hwd`, `--hwd-only` or `all` is given.

Results are files:

```text
tests/build/aarch64-rpi4b-cmake-gcc-debug/ctest.xml      JUnit summary
tests/build/aarch64-rpi4b-cmake-gcc-debug/ctest.log      full CTest log
.../port-tests/test/.qemu-logs/smp_test4.tap             complete TAP (QEMU)
.../port-tests/test/.hw-logs/smp_test4.tap               complete TAP (hardware)
```

# 25. The QEMU case

The runner is `micro-os-plus-iii-smp/test_smpl/run-qemu.sh`; it is given the build directory, the QEMU binary and the machine arguments `-M raspi4b -smp 4`. For a µTest++ image it builds:

```text
-semihosting-config enable=on,target=native,
  arg=smp_test4,arg=--output-file,arg=<log>/smp_test4.tap
```

The image is handed to `-kernel` as the raw `.bin`. CTest sees only the exit status and the console; `run-qemu.sh` reads the `RESULT:` line from the log to decide pass/fail.

`UOS_QEMU_ONLY=smp_test4` makes the runner iterate only that image (`run-qemu.sh` loops over all `*-qemu.bin` otherwise).

# 26. The hardware case

The board runner is `micro-os-plus-iii-aarch64/test/boards/rpi4b/hw.sh`, which execs `micro-os-plus-iii-smp/test_smpl/run-hw.sh` with the BCM2711 facts:

```bash
UOS_HW_CFG=openocd-cmsis-dap-rpi4b.cfg
UOS_HW_CFG_INIT=1
UOS_HW_ENTRY=0x80000
UOS_HW_RESUME=pc-first
UOS_HW_NCPU=4
UOS_HW_CORES=0
UOS_HW_SEMIHOST_CORES="0 1 2 3"
UOS_HW_TARGET_FMT="bcm2711.cpu%d"
UOS_HW_RESET_WAIT=10
UOS_HW_PRELOAD="catch {mww 0xfe100024 0x5a000001}; catch {mww 0xfe10001c 0x5a000020}"
```

What happens on each run:

1. **Connect.** OpenOCD loads `interface/cmsis-dap.cfg`, selects JTAG, loads `board/rpi4b.cfg`, and examines `bcm2711.cpu0..3` (all four, because any core may print the verdict through semihosting).
2. **Reset.** The PM watchdog registers (`0xfe100024`, `0xfe10001c`) are written through core 0, resetting the board; the runner waits 10 s for the firmware to boot and re-enable JTAG.
3. **Unwedge.** The config's `rpi4b_unwedge` clears a sticky JTAG-DP error left by a killed session.
4. **Load.** The `smp_test4-hwd` image is loaded over JTAG (about 250 KB) and core 0 is started at `0x80000`; cores 1–3 stay in the armstub spin-table until the kernel releases them.
5. **Run.** Semihosting is enabled on all four cores. The full console is on the UART (GPIO14/15, 115200 8N1); the RESULT/FAIL/FATAL lines are also mirrored through semihosting, and the TAP report file is written through semihosting to `.hw-logs/smp_test4.tap`.
6. **Verdict.** `run-hw.sh` prints `smp_test4 PASS`, CTest records the case.

The UART is **not** opened by the runner. A terminal on the board's USB-serial adapter is unaffected, and the probe's own CDC port is not used either.

> After a passing run the app ends with semihosting `SYS_EXIT`, which leaves core 0 halted. The runner prints “power-cycle the board before the next test”; the watchdog reset normally recovers it, but a power cycle is the safe start for a back-to-back run.

# 27. What a passing run looks like

**QEMU:**

```text
11: # smp_test4, under QEMU
11: # cores: c1=3 c2=3 c3=3, 0 ms
11: # stdc++: StateOS stdc++ adapted to µOS++ III, 4 cores
11: # Running: smp_test4
11: # Built with GCC 15.2.1 20251203, with FP, no exceptions
11: TAP version 14
...
11: ok 7 - 06 - chrono # { passed, 9 checks in 5 test cases }
11: 1..7
11: # { total: 75 checks passed, 0 failed, in 37 test cases, 7 test suites }
11: RESULT: PASS (smp_test4: all µTest++ checks passed, 106 ms)
1/1 Test #11: aarch64-rpi4b-smp_test4-qemu .....   Passed    0.21 sec
```

**Hardware:**

```text
12: # cores: c1=3 c2=3 c3=3, 0 ms
12: # stdc++: StateOS stdc++ adapted to µOS++ III, 4 cores
12: # Built with GCC 15.2.1 20251203, with FP, no exceptions
...
12: # { total: 75 checks passed, 0 failed, in 37 test cases, 7 test suites }
12: RESULT: PASS (smp_test4: all µTest++ checks passed, 24572 ms)
12: [hw] smp_test4 PASSED — power-cycle the board before the next test.
1/1 Test #12: aarch64-rpi4b-smp_test4-hwd ......   Passed   65.63 sec
```

| Suite | Checks | Cases |
| --- | --- | --- |
| `00 - Cores` | 3 | 1 |
| `01 - std::thread and TLS` | 15 | 7 |
| `02 - promise and future` | 10 | 5 |
| `03 - packaged_task and async` | 11 | 6 |
| `04 - mutexes and call_once` | 13 | 5 |
| `05 - condition variable and C++20 sync` | 14 | 8 |
| `06 - chrono` | 9 | 5 |
| **Total** | **75** | **37** |

# 28. Adding another test that uses the library

1. Create `micro-os-plus-iii-aarch64/test/rpi4b/<name>/` with one or more `.cpp` files.
2. Write `main.cpp` like `smp_test4`'s, or one `int main(int, char*[])` if it is a plain port test.
3. In `test/rpi4b/tests.cmake`, add a branch to `board_test_libs()`:

   ```cmake
   if (_app STREQUAL "<name>")
     set (${_out} stateos-stdc++ uos-micro-test-plus PARENT_SCOPE)
     return ()
   endif ()
   ```

4. Add the two CTest names to the configuration's `SELECTED` list in `tests/run.sh` if it should run by default, and to `scaffold.json` (`microTestPlus.boards[].ctest`) so a re-scaffold keeps them.
5. Build and run:

   ```bash
   ./tests/run.sh <name>
   ./tests/run.sh <name> --hwd
   ```

If the test uses its own `main()` and the `mtp_board` support, no further change is needed: the directory listing is the registration.

# 29. Starting a new project from scratch

This chapter builds a standalone project, `<project>/`, from an empty folder, using
`micro-os-scaffold`, and ends with the same `smp_test4` CTest cases this repository
runs. The result is not a copy of `rpi4b-smp`; it is the sequence that produces it.

The example target is the Raspberry Pi 4 B (`aarch64-rpi4b`, four Cortex-A72
cores). The same steps work for any board the scaffold discovers; only the
selection token and the board's runner change.

## 29.1 Prerequisites

| Piece | What is needed |
| --- | --- |
| Node.js | 18 or newer; the scaffolder uses only the standard library |
| Source workspace | a folder holding the sibling `micro-os-plus-iii-smp` and `micro-os-plus-iii-aarch64` repositories (here `/home/dan/Desktop/micro-os-plu-iii-smp-pr-github`) |
| Cross toolchain | `aarch64-none-elf-gcc` 15.2 (an xPack), CMake, Ninja |
| µTest++ | a folder or a git URL for `micro-test-plus` |
| QEMU (optional) | `qemu-system-aarch64` 9.x for the emulated cases |
| OpenOCD (optional) | 0.12 for the hardware cases, plus a CMSIS-DAP probe |

The scaffolder finds the tools and records their absolute paths in
`scaffold.json`. The generated `tests/scripts/env.sh` puts them on `PATH` for
`run.sh`; nothing is hard-coded in the CMake files.

## 29.2 Find the board and the test to start from

`discover` is read-only and prints exactly the tokens `--select` accepts:

```bash
cd /home/dan/Work/micro-os-scaffold

node bin/scaffold.js discover \
  --source /home/dan/Desktop/micro-os-plu-iii-smp-pr-github
```

The part that matters here:

```text
  3. micro-os-plus-iii-aarch64 v0.1.0  [port, aarch64; 3 boards, 56 tests]
      3.1    aarch64-rpi-zero-2w  (board rpi-zero-2w, 18 tests)
      3.2    aarch64-rpi3b  (board rpi3b, 16 tests)
      3.3    aarch64-rpi4b  (board rpi4b, 22 tests)
             3.3.1    clock-test
             3.3.2    cmsis-os-validator
             ...
             3.3.10   smp_test0
             3.3.11   smp_test1
             3.3.12   smp_test2
             3.3.13   smp_test3
             3.3.14   smp_test4
             ...
```

Three selection shapes are useful:

| Token | Meaning |
| --- | --- |
| `3.3` | the whole board: all 22 Raspberry Pi 4 B tests (heavy) |
| `3.3.1` | one test, `clock-test`; the smallest workspace that still builds the board (used below) |
| `3.3.10` | one SMP test (`smp_test0`) if the starting point should already be multi-core |

> The source workspace has its own `smp_test4`, a load-balancing demo. It is
> **not** the test in this document. Selecting `3.3.1` keeps the workspace
> small; the stdc++ test is added below as a new directory with the same name.

## 29.3 Scaffold the workspace

```bash
node bin/scaffold.js /home/dan/Work/rpi4b-stdcpp \
  --source /home/dan/Desktop/micro-os-plu-iii-smp-pr-github \
  --select 3.3.1 \
  --micro-test-plus https://github.com/micro-os-plus/micro-test-plus-xpack.git \
  --micro-test-plus-ref v6.0.0 \
  --yes
```

- `--select 3.3.1` is the lean default: the complete kernel, the selected port,
  the selected test and the harness files it needs.
- `--micro-test-plus` copies the framework into `third_party/micro-test-plus/`,
  adds the board support under `micro-os-plus-iii-aarch64/test/mtp/`, creates the
  `uos-micro-test-plus` INTERFACE target, adds the `mtp-*` starters
  (`mtp-example`, `mtp-args`, `mtp-smp`, `mtp-eth-net`) and patches the runners
  so a µTest++ image receives `--output-file`.
- `--yes` accepts the detected tools non-interactively.
- Add `--no-build` to skip the verification build. Without it the tool runs the
  full CMake/Ninja build once, records the result in `scaffold.json`, then
  removes the build tree (the log stays in `build-logs/`).

On the first run the tool writes `scaffold.json`; later runs read it as the
source of truth. `scaffold.json` records the source root, the selection, the
tool paths, the µTest++ integration, the runner patches and the expected CTest
names.

## 29.4 What the scaffold created

```text
rpi4b-stdcpp/
├── micro-os-plus-iii-smp/                  the kernel (complete)
├── micro-os-plus-iii-aarch64/              the AArch64 port (lean)
│   ├── test/boards/rpi4b/                  board sources, linker, hw.sh, OpenOCD cfg
│   └── test/rpi4b/
│       ├── clock-test/                     the selected test
│       ├── mtp-example/ mtp-args/          µTest++ starters
│       ├── mtp-smp/ mtp-eth-net/
│       ├── include/ src/                   board test support
│       └── tests.cmake                     the per-test hooks
├── tests/
│   ├── CMakeLists.txt cmake/ platforms/aarch64-rpi4b/
│   ├── run.sh                              the one entry point
│   └── scripts/env.sh                      generated tool paths
├── third_party/micro-test-plus/            µTest++ 6.0.0
├── scaffold.json                           the source of truth
└── README.md HOW-TO-RUN.md build-logs/
```

The workspace builds as it is:

```bash
cd /home/dan/Work/rpi4b-stdcpp/tests
./run.sh build
./run.sh clock-test        # the QEMU case
./run.sh list
```

## 29.5 Put the stdc++ library in `third_party`

A standalone project has no repository-level `third_party/`, so copy the library
in (or make it a submodule of the project):

```bash
mkdir -p /home/dan/Work/rpi4b-stdcpp/third_party
cp -a /home/dan/Work/micro-os-scaffold-ctest-examples/third_party/stateos-stdc++ \
      /home/dan/Work/rpi4b-stdcpp/third_party/
```

The result is:

```text
rpi4b-stdcpp/third_party/stateos-stdc++
├── CMakeLists.txt
├── include/{barrier,latch,semaphore,bits/gthr-default.h,inc/*.hh}
├── src/{gthread,thread,mutex,condition_variable,future,chrono,emutls}.cc
└── README.md LICENSE src/COPYING3 src/COPYING.RUNTIME
```

This repository keeps one canonical copy at its root and a relative symlink in
`rpi4b-smp/third_party/`; a standalone project needs neither, because the first
path the CMake hook tries is `<project>/third_party/stateos-stdc++`.

## 29.6 Add the `smp_test4` directory

```bash
mkdir -p /home/dan/Work/rpi4b-stdcpp/micro-os-plus-iii-aarch64/test/rpi4b/smp_test4
```

Four things are needed for a µTest++ test: `main.cpp`, one or more suite files,
the `stateos-stdc++` target and the `uos-micro-test-plus` library. The suites are
listed in the table after the code; the complete set is in this repository's
`smp_test4/`.

### `main.cpp`

```c++
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
```

### `suite-thread.cpp`

```c++
// 01 - std::thread and TLS: join, detach, jthread, ids, yield, sleep, thread_local,
// on the StateOS stdc++ gthread layer.
#include <mtp_board.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
  using namespace std::chrono_literals;

  std::atomic<int> g_counter{ 0 };
  thread_local int g_tls = 0;
  std::atomic<int> g_tls_ok{ 0 };

  // Bounded wait that does not need the thread to be joined first: sleep in
  // one-tick steps, so the scheduler on the other cores keeps running.
  bool
  wait_flag (const std::atomic<bool>& flag, unsigned ms = 2000)
  {
    for (unsigned i = 0; i < ms; ++i)
      {
        if (flag.load (std::memory_order_acquire))
          {
            return true;
          }
        std::this_thread::sleep_for (1ms);
      }
    return flag.load (std::memory_order_acquire);
  }
} // namespace

mt::static_suite suite_thread{
  "01 - std::thread and TLS", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("join: a thread runs, its writes are visible after join ()",
             [] (auto& t) {
               constexpr int increments = 4000;
               g_counter = 0;
               std::thread th ([]
                               {
                                 for (int i = 0; i < increments; ++i)
                                   {
                                     g_counter.fetch_add (
                                         1, std::memory_order_relaxed);
                                   }
                               });
               t.expect (th.joinable ()) << "joinable before join ()";
               th.join ();
               t.expect (mt::eq (g_counter.load (), increments))
                   << "every increment visible after join ()";
               t.expect (!th.joinable ()) << "not joinable after join ()";
             });

    ts.test ("detach: the thread runs on without join ()", [] (auto& t) {
      std::atomic<bool> ran{ false };
      std::thread th ([&ran] { ran.store (true, std::memory_order_release); });
      t.expect (th.joinable ()) << "joinable before detach ()";
      th.detach ();
      t.expect (!th.joinable ()) << "not joinable after detach ()";
      t.expect (wait_flag (ran)) << "the detached thread ran";
    });

    ts.test ("get_id (): another thread has a distinct, stable id",
             [] (auto& t) {
               const std::thread::id main_id = std::this_thread::get_id ();
               std::thread::id other_id;
               std::atomic<bool> stable{ false };

               std::thread th ([&]
                               {
                                 other_id = std::this_thread::get_id ();
                                 std::this_thread::yield ();
                                 stable.store (std::this_thread::get_id ()
                                                   == other_id,
                                               std::memory_order_release);
                               });
               th.join ();

               t.expect (!(main_id == other_id))
                   << "the worker's id differs from the runner's";
               t.expect (stable.load ()) << "the worker's id is stable";
               t.expect (main_id == std::this_thread::get_id ())
                   << "the runner's id is stable";
             });

    ts.test ("hardware_concurrency () reports the board's cores",
             [] (auto& t) {
               t.expect (mt::eq (static_cast<unsigned> (
                                     std::thread::hardware_concurrency ()),
                                 static_cast<unsigned> (OS_NCPU)))
                   << "one thread per core";
             });

    ts.test ("sleep_for () lasts at least the requested time", [] (auto& t) {
      const auto before = std::chrono::steady_clock::now ();
      std::this_thread::sleep_for (20ms);
      const auto after = std::chrono::steady_clock::now ();
      const auto elapsed
          = std::chrono::duration_cast<std::chrono::milliseconds> (after
                                                                  - before)
                .count ();
      t.expect (elapsed >= 15) << "slept at least 15 ms (the tick is 1 ms)";
    });

    ts.test ("thread_local: one instance per thread (emulated TLS)",
             [] (auto& t) {
               g_tls = 1; // the runner's own copy
               g_tls_ok = 0;

               auto worker = [] (int value) {
                 g_tls = value;
                 std::this_thread::sleep_for (5ms);
                 if (g_tls == value)
                   {
                     g_tls_ok.fetch_add (1, std::memory_order_relaxed);
                   }
               };

               std::thread a (worker, 10);
               std::thread b (worker, 20);
               a.join ();
               b.join ();

               t.expect (mt::eq (g_tls_ok.load (), 2))
                   << "each thread saw its own value";
               t.expect (mt::eq (g_tls, 1))
                   << "the runner's value was untouched";
               g_tls = 0;
             });

    ts.test ("jthread: joins at scope exit, the stop token reaches the worker",
             [] (auto& t) {
               std::atomic<bool> started{ false };
               std::atomic<bool> stopped{ false };
               {
                 std::jthread worker ([&] (std::stop_token token) {
                   started.store (true, std::memory_order_release);
                   while (!token.stop_requested ())
                     {
                       std::this_thread::sleep_for (1ms);
                     }
                   stopped.store (true, std::memory_order_release);
                 });

                 t.expect (wait_flag (started)) << "the jthread started";
                 worker.request_stop ();
               } // the destructor joins here
               t.expect (stopped.load ())
                   << "the worker saw the stop request, and the join completed";
             });
  }
};
```

### `suite-promise.cpp`

```c++
// 02 - promise and future: the core of the C++11 future library on top of
// the gthread layer.
#include <mtp_board.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <utility>

namespace
{
  using namespace std::chrono_literals;

  bool
  wait_flag (const std::atomic<bool>& flag, unsigned ms = 2000)
  {
    for (unsigned i = 0; i < ms; ++i)
      {
        if (flag.load (std::memory_order_acquire))
          {
            return true;
          }
        std::this_thread::sleep_for (1ms);
      }
    return flag.load (std::memory_order_acquire);
  }
} // namespace

mt::static_suite suite_promise{
  "02 - promise and future", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("promise<int>: the value set by a thread reaches get ()",
             [] (auto& t) {
               std::promise<int> promise;
               std::future<int> future = promise.get_future ();
               t.expect (future.valid ()) << "a future is attached";

               std::thread th ([p = std::move (promise)] () mutable {
                 std::this_thread::sleep_for (5ms);
                 p.set_value (41 + 1);
               });

               t.expect (mt::eq (future.get (), 42))
                   << "get () waited for the value";
               th.join ();
               t.expect (!future.valid ()) << "get () releases the state";
             });

    ts.test ("promise<void>: a value-less handshake", [] (auto& t) {
      std::promise<void> promise;
      std::future<void> future = promise.get_future ();
      std::atomic<bool> started{ false };

      std::thread th ([&]
                      {
                        started.store (true, std::memory_order_release);
                        std::this_thread::sleep_for (5ms);
                        promise.set_value ();
                      });

      t.expect (wait_flag (started)) << "the setter is running";
      future.wait ();
      t.expect (future.valid ()) << "wait () does not consume the state";
      th.join ();
    });

    ts.test ("future_status: timeout before the value, ready after",
             [] (auto& t) {
               std::promise<int> promise;
               std::future<int> future = promise.get_future ();

               t.expect (future.wait_for (1ms) == std::future_status::timeout)
                   << "nothing is ready yet";

               promise.set_value (7);

               t.expect (future.wait_for (1ms) == std::future_status::ready)
                   << "the value is ready";
               t.expect (mt::eq (future.get (), 7)) << "and readable";
             });

    ts.test ("shared_future: two threads read the same value",
             [] (auto& t) {
               std::promise<int> promise;
               std::future<int> future = promise.get_future ();
               std::shared_future<int> shared = future.share ();
               std::atomic<int> sum{ 0 };

               std::thread a ([shared, &sum] {
                 sum.fetch_add (shared.get (), std::memory_order_relaxed);
               });
               std::thread b ([shared, &sum] {
                 sum.fetch_add (shared.get (), std::memory_order_relaxed);
               });

               promise.set_value (21);
               a.join ();
               b.join ();

               t.expect (mt::eq (sum.load (), 42))
                   << "both readers saw 21";
             });

    ts.test ("a promise moved between threads keeps its future",
             [] (auto& t) {
               std::promise<int> promise;
               std::future<int> future = promise.get_future ();

               // Move the promise into a lambda, then move it again between
               // the two threads through a shared slot.
               std::thread producer ([p = std::move (promise)] () mutable {
                 std::this_thread::sleep_for (2ms);
                 p.set_value (99);
               });

               t.expect (mt::eq (future.get (), 99)) << "the moved promise set";
               producer.join ();
             });
  }
};
```

The remaining suites are the same shape:

| File | Suite title | What it covers |
| --- | --- | --- |
| `suite-future.cpp` | `03 - packaged_task and async` | `packaged_task`, `launch::async` vs `deferred`, `set_value_at_thread_exit` |
| `suite-mutex.cpp` | `04 - mutexes and call_once` | `lock_guard`, `timed_mutex`, `recursive_mutex`, `call_once`, `unique_lock` |
| `suite-sync.cpp` | `05 - condition variable and C++20 sync` | `condition_variable`, `notify_all_at_thread_exit`, semaphores, `latch`, `barrier`, `shared_mutex` |
| `suite-chrono.cpp` | `06 - chrono` | both clocks, sleeps, duration conversions |

Copy them from this repository if the whole set is wanted:

```bash
cp /home/dan/Work/micro-os-scaffold-ctest-examples/rpi4b-smp/micro-os-plus-iii-aarch64/test/rpi4b/smp_test4/*.cpp \
   /home/dan/Work/rpi4b-stdcpp/micro-os-plus-iii-aarch64/test/rpi4b/smp_test4/
```

## 29.7 Wire it into CMake and the runner

### 29.7.1 The `stateos-stdc++` INTERFACE target

Add this block to `micro-os-plus-iii-aarch64/test/rpi4b/tests.cmake`, before the
µTest++ block:

```cmake
# ---- stateos-stdc++ ---------------------------------------------------------
# The StateOS stdc++ package adapted to µOS++ III. Its include directory goes
# BEFORE the toolchain's C++ headers: it provides <bits/gthr-default.h> and the
# C++20 <barrier>/<latch>/<semaphore>.
set (_stateos_stdcpp
     "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/stateos-stdc++")
if (NOT IS_DIRECTORY "${_stateos_stdcpp}")
  message (FATAL_ERROR "stateos-stdc++ not found at ${_stateos_stdcpp}")
endif ()
if (NOT TARGET stateos-stdc++)
  add_subdirectory ("${_stateos_stdcpp}"
                    "${CMAKE_BINARY_DIR}/stateos-stdc++-bin")
endif ()
```

### 29.7.2 The library hook

`tests.cmake` already has a `board_test_libs()` function. Add the branch at its
top:

```cmake
function (board_test_libs _app _out)
  if (_app STREQUAL "smp_test4")
    set (${_out} stateos-stdc++ uos-micro-test-plus PARENT_SCOPE)
    return ()
  endif ()
  ...
endfunction ()
```

`uos-micro-test-plus` is defined later in the same file by the
`--micro-test-plus` block, so the hook is called after the whole file has been read and the
target exists.

### 29.7.3 The default CTest selection

`tests/run.sh` lists the cases a plain `./run.sh` runs. Add the two new names to
the configuration's `SELECTED` array:

```bash
SELECTED=('aarch64-rpi4b-clock-test-qemu' 'aarch64-rpi4b-clock-test-hwd' \
          ... \
          'aarch64-rpi4b-smp_test4-qemu' 'aarch64-rpi4b-smp_test4-hwd')
```

The CTest cases exist either way — the directory listing registers them. The
array only decides what runs by default.

### 29.7.4 A TAP report for a non-`mtp-*` name

The runners write a complete TAP file only for names matching `mtp-*`. Extend
the condition in the three generated runners:

```bash
# micro-os-plus-iii-smp/test_smpl/run-qemu.sh and run-host.sh
if [[ "$app" == mtp-* || "$app" == smp_test4 ]]; then
  ...
fi
```

```bash
# micro-os-plus-iii-aarch64/test/boards/rpi4b/hw.sh
if [[ "$1" == mtp-* || "$1" == smp_test4 ]]; then
  ...
fi
```

Without this the test still runs and prints its TAP to the console; it just does
not write `.qemu-logs/smp_test4.tap` or `.hw-logs/smp_test4.tap`.

### 29.7.5 Recording the additions

`scaffold.json` is the source of truth. If `smp_test4` should be part of the
default set after a re-scaffold, add the two CTest names to
`microTestPlus.boards[0].ctest` and note the runner patch in `patches` (this
repository's `rpi4b-smp/scaffold.json` is the example). The test directory and
the CMake hook are not generated files; keep them in version control.

## 29.8 Build and run under QEMU

```bash
cd /home/dan/Work/rpi4b-stdcpp/tests

./run.sh build             # configure and build every target
./run.sh list              # smp_test4-qemu / -hwd are registered
./run.sh smp_test4         # build, then the QEMU case
```

Expected (the CTest number depends on the directory order, the rest does not):

```text
11: # smp_test4, under QEMU
11: # cores: c1=3 c2=3 c3=3, 0 ms
11: # stdc++: StateOS stdc++ adapted to µOS++ III, 4 cores
11: # Built with GCC 15.2.1 20251203, with FP, no exceptions
11: TAP version 14
...
11: # { total: 75 checks passed, 0 failed, in 37 test cases, 7 test suites }
11: RESULT: PASS (smp_test4: all µTest++ checks passed, 106 ms)
1/1 Test #11: aarch64-rpi4b-smp_test4-qemu .....   Passed    0.21 sec
```

The complete report is

```text
tests/build/aarch64-rpi4b-cmake-gcc-debug/platform-bin/port-tests/test/.qemu-logs/smp_test4.tap
```

If the first build fails at the `static_assert`s in `src/gthread.cc`, the kernel
objects outgrew the 256-byte storage; raise `__GTHREAD_STORAGE_BYTES` in
`include/bits/gthr-default.h` (chapter 7). If it fails with `std::mutex` not
declared, the library target is not linked or its include directory is not
first (chapter 19).

## 29.9 Run on the board

The scaffold registers both variants for every test directory. With a
CMSIS-DAP probe attached:

```bash
./run.sh --hwd-only smp_test4
```

`hw.sh` resets the board through the BCM2711 PM watchdog, loads
`smp_test4-hwd` over JTAG, starts core 0, releases the other cores from the
armstub spin-table and reads the verdict through semihosting (chapter 26).
Expected:

```text
12: RESULT: PASS (smp_test4: all µTest++ checks passed, 24572 ms)
12: [hw] smp_test4 PASSED — power-cycle the board before the next test.
1/1 Test #12: aarch64-rpi4b-smp_test4-hwd ......   Passed   65.63 sec
```

The full report is `.hw-logs/smp_test4.tap`.

## 29.10 Keeping the new project reproducible

Three kinds of files are involved, and they have different lifetimes:

| Kind | Examples | Where it belongs |
| --- | --- | --- |
| generated | `tests/`, the lean port, `scaffold.json`, `env.sh` | re-created by the scaffolder |
| added | `third_party/stateos-stdc++/`, the `smp_test4/` directory, the `tests.cmake` hook | the project's version control |
| patched | the `smp_test4` condition in the three runner scripts | record in `scaffold.json`'s `patches` |

For a project that must survive a re-scaffold, keep the added files in the
source workspace as well:

1. put `smp_test4/` under `micro-os-plus-iii-aarch64/test/rpi4b/` in the source
   workspace — replacing the existing load-balancing demo of that name, or using
   another directory name and the matching token — so `discover` lists it and
   `--select` can choose it;
2. add the `stateos-stdc++` block and the `board_test_libs()` branch to that
   port's `tests.cmake`;
3. keep the library in a location the source build can reach, or copy it into
   each generated workspace as above.

Then a fresh scaffold selects the test directly:

```bash
node bin/scaffold.js /home/dan/Work/rpi4b-stdcpp-2 \
  --source /home/dan/Desktop/micro-os-plu-iii-smp-pr-github \
  --select aarch64-rpi4b.smp_test4 \
  --micro-test-plus ... --yes
```

> Lean mode may prune port files that no selected test needs. A hand-added test
> directory that is not part of the source workspace is therefore at risk on a
> re-scaffold; keeping it in the source repo (step 1) removes that risk.

## 29.11 The same recipe on Cortex-M: Pico 2

The steps above are not AArch64-specific. The second application of the guide
is the Raspberry Pi Pico 2 (RP2350, 2 × Cortex-M33), workspace
`pico2-smp-stdcpp/`, test `smp-stdcpp-test`:

```bash
node bin/scaffold.js /home/dan/Work/pico2-smp-stdcpp \
  --source /home/dan/Desktop/micro-os-plu-iii-smp-pr-github \
  --select 4.4.20 \
  --micro-test-plus https://github.com/micro-os-plus/micro-test-plus-xpack.git \
  --micro-test-plus-ref v6.0.0 \
  --yes --no-build
```

`4.4.20` is `cortexm-pico2/smp-test4`, the board's kernel-object SMP test. The
lean workspace then gets the library in `third_party/stateos-stdc++` (a
relative link to the repository copy), the suites under
`micro-os-plus-iii-cortexm/test/pico2/smp-stdcpp-test/`, and the same
`stateos-stdc++` + `board_test_libs()` hook in the board's `tests.cmake`.

Four Cortex-M facts change the details:

| Fact | What it means |
| --- | --- |
| 32-bit Cortex-M33 | `gthread.cc` uses 4 KiB stacks (`thread_stack_bytes`), `size_t` is 32 bits, and the `MTP_NANO_PRINTF` wrappers keep `%zu` readable |
| no native TLS | `thread_local` goes through `__emutls_get_address`; `emutls.cc` also provides `__aeabi_read_tp` for GCC 15's ARM path |
| QEMU does not model the RP2350 SIO block | the pico2 `qemu` image is the generic **single-core** `mps2-an500`, so `OS_NCPU=1`, `release_cores()` is a no-op and `00 - Cores` is not registered; the `hwd` image runs both cores through the bootrom FIFO and the SIO IPIs |
| the board's `boot.S` owns the start-up | it called `main(0, NULL)`, so `--output-file` never reached µTest++. A weak `os_startup_initialize_args()` in `init-fini-stubs.c` gives the default, `boot.S` calls it and passes the result to `main()`, and the µTest++ strong reader (SYS_GET_CMDLINE) overrides it. The qemu case passes `arg=<app>,arg=--output-file,arg=<...>/.qemu-logs/<app>.tap` in its `--semihosting-config` |

The lean workspace also dropped `test/boards/pico2/qemu/` and
`tests/device-qemu-cortexm/` because the selected `smp-test4` is hardware-only;
they were restored (64 KiB and 20 KiB) so the new test can have a qemu image.

Results:

| Case | Checks | Cases | Suites | Time |
| --- | --- | --- | --- | --- |
| `cortexm-pico2-smp-stdcpp-test-qemu` | 72 | 36 | 6 | 0.12 s CTest, 107 ms kernel |
| `cortexm-pico2-smp-stdcpp-test-hwd` | 75 | 37 | 7 | 8–29 s CTest, 405 ms kernel |

The board adds the three `00 - Cores` checks. Both reports are files:

```text
pico2-smp-stdcpp/tests/build/cortexm-pico2-cmake-gcc-debug/
  platform-bin/port-tests/.qemu-logs/smp-stdcpp-test.tap
  platform-bin/port-tests/.hw-logs/smp-stdcpp-test.tap
```

The hardware runner is OpenOCD over CMSIS-DAP (`rp2350.cm0`/`cm1`), which
programs the ELF, enables semihosting on both cores, resumes `cm1` before
`cm0`, and ends on the semihosting `SYS_EXIT`. The board must be power-cycled
between runs.

## 29.12 A third target: WeAct F411CE and SPI flash

The WeAct F411CE (STM32F411CE, one Cortex-M4F) is the hardware-only case. Its
workspace, `weactf411-spi-stdcpp/`, was scaffolded from the board's
`spi-pipeline` test (`4.7.5`) because that is the test that owns the SPI NOR
flash support:

```bash
node bin/scaffold.js /home/dan/Work/weactf411-spi-stdcpp \
  --source /home/dan/Desktop/micro-os-plu-iii-smp-pr-github \
  --select 4.7.5 \
  --micro-test-plus https://github.com/micro-os-plus/micro-test-plus-xpack.git \
  --micro-test-plus-ref v6.0.0 \
  --yes --no-build
```

The new test, `spi-stdcpp-test`, has eight suites: the six stdc++ suites from
the other targets, `00 - Console and start-up` (the semihosting argc/argv and
USART1), and `00 - SPI flash`, which drives the board's own `bsp/spi.cpp` and
`flash.cpp`:

- `spi::probe()` chooses DMA, IRQ or polled; the report prints the choice.
- JEDEC ID, status register 1 and a non-destructive read.
- All three transports read the same JEDEC ID.
- A scratch sector (the last 4 KiB) is read, erased, programmed with a
  pattern, verified, then erased and written back byte-for-byte.

On the board:

```text
# args: argc=3 argv[0]="spi-stdcpp-test" argv[1]="--output-file" argv[2]="...tap"
# console: USART1 115200 baud, BRR=868, CR1=200C, PA9/PA10 AF7
# SPI: transport dma
# SPI: JEDEC EF 40 18          (Winbond W25Q128, 16 MiB)
# SPI: status 00
# SPI: scratch sector at 0x00FFF000 of 0x01000000
```

The `# args:` line is the same transport `mtp-args` checks; the console suite
also verifies the USART1 registers (UE/TE/RE, BRR = 868, PA9/PA10 in AF7) and
waits for `TC` after writing a line. That proves the software side even when
the UART pins are not wired. The board's UART is **PA9 (TX) / PA10 (RX),
115200 8N1**; the runner opens neither it nor the probe's CDC.

Verified on the board with `tio -b 115200 /dev/ttyACM0`: the `# ...` lines
appear live during the run. Open the terminal before starting the run; a UART
has no host-side buffer. After `RESULT: PASS`, OpenOCD may print
`Error setting register pc` / `context restore failed` while it tears down the
core halted at the semihosting exit; they do not affect the CTest result.

Only the last sector is touched, and it is restored, so the SPI flash's
filesystem partitions and the 1 MiB primary archive are left alone.

| Case | Checks | Cases | Suites | Time |
| --- | --- | --- | --- | --- |
| `cortexm-weactf411-spi-stdcpp-test-hwd` | 104 | 44 | 8 | 72.9 s CTest, 785 ms kernel |

This board has **no qemu image** (its `board.cmake` sets no QEMU linker), so
there is no `-qemu` case: the test is hardware only. The complete report is

```text
weactf411-spi-stdcpp/tests/build/cortexm-weactf411-cmake-gcc-debug/
  platform-bin/port-tests/.hw-logs/spi-stdcpp-test.tap
```

The runner programs the flash over CMSIS-DAP and reads the verdict through
semihosting (`hw.sh` passes `--output-file`). USART1 is the board's console
and is not opened by the runner, so a terminal on the virtual COM port is
unaffected. CTest takes about a minute because it erases, programs and
verifies the image plus the stdc++ report.

# 30. Porting the library to another board

The library is board-independent; the board facts are in the port and in three constants in the library:

| Constant | Where | What to check |
| --- | --- | --- |
| `thread_stack_bytes` | `src/gthread.cc` | 16 KiB on AArch64/x86_64, 4 KiB otherwise |
| `__GTHREAD_STORAGE_BYTES/ALIGNMENT` | `include/bits/gthr-default.h` | the `static_assert`s in `gthread.cc` fail if the kernel objects outgrow them |
| tick frequency | `inc/chrono.hh` | `OS_INTEGER_SYSTICK_FREQUENCY_HZ`; the conversion assumes it |

Recipe:

1. Add the library target to the board's test build (chapter 19).
2. Build one test that includes `<thread>`, `<mutex>`, `<future>` and `<condition_variable>`.
3. If the kernel object sizes changed, the `static_assert`s stop the build with a clear message; raise the storage.
4. Run a thread + promise test and a `thread_local` test. The TLS path is the one most likely to differ: check whether the compiler emits `__emutls_get_address` (as on AArch64) or native TLS (then `_GLIBCXX_HAVE_TLS` needs different handling).
5. Run the C++20 sync test: it is where the barrier/latch semantics and the semaphore counter limit show up.
6. Check the memory: each mutex/condition variable is 256 bytes, each thread 16 KiB of stack.

# 31. Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| `std::mutex` does not name a type | `_GLIBCXX_HAS_GTHREADS` not defined, or the gthr include dir is after the toolchain | link `stateos-stdc++`; check `BEFORE` |
| Duplicate/parse error inside `<mutex>` about `std::mutex` | `gthr-default.h` includes the kernel headers | keep the kernel out of the header |
| `abort(), exiting...` on the first `thread_local` access | `memalign()` stub aborts | use the manual aligned allocation |
| `abort(), exiting...` on `std::thread` start | a failed kernel call in `lock`/`join` | check the return code; the port aborts on non-zero |
| Link error: `__gthread_mutex_lock` undefined | the library sources were not compiled into this target | check `target_sources` and `board_test_libs` |
| Link error: `__emutls_get_address` undefined | `src/emutls.cc` missing from the target | add it |
| `binary_semaphore` does not name a type | the package's `<semaphore>` was skipped because `__cpp_lib_semaphore` was already defined | the class is defined unconditionally now; keep the header first |
| Hardware: `JTAG-DP STICKY ERROR` | a killed previous session | the config's `rpi4b_unwedge` clears it; power-cycle if not |
| Hardware: board held in reset | no reset / nRESET wiring | the PM-watchdog reset in `hw.sh`; check `UOS_HW_RESET_WAIT` |
| Hardware: test timeout | the image did not load | check the probe firmware and `UOS_HW_ADAPTER_KHZ` |
| TAP file missing | the runner's `mtp-*` condition did not include the app | the patched runners include `smp_test4` |

# 32. Validation log

The port was validated in this order; each step found something the previous one could not.

1. **Compile the library alone** with the toolchain include paths. Found the `__gthread_create` signature mismatch (`void (*)(void*)`) and the `void*` key casts.
2. **Compile the suites** against the library. Found the `<stop_token>`/`__cpp_lib_semaphore` ordering problem (the package's class was skipped when the macro was already defined).
3. **QEMU run.** Found the `memalign()` abort on the first `thread_local` access (chapter 13.3).
4. **QEMU, all suites.** 75 checks, 37 cases, 7 suites, 0 failed, 106 ms.
5. **Hardware run** on the Raspberry Pi 4 B, CMSIS-DAP probe, all four cores: 75 checks, 0 failed, 24.6 s, CTest 65.6 s.
6. **Regression run:** the rpi4b workspace's other five QEMU cases still pass.
7. **Cortex-M (Pico 2).** The same library and suites on `cortexm-pico2`:
   72 checks under QEMU (single-core emulated image) and 75 on the board
   (both cores), 0 failed. This step found the `boot.S` `argc=0` problem: the
   scaffolded `--output-file` never reached µTest++ until the semihosting
   command line was read before `main()`.
8. **Cortex-M4F + SPI (WeAct F411CE).** `spi-stdcpp-test`: 104 checks, 44
   cases, 8 suites, 0 failed, hardware only. The console suite verifies the
   command line (`argc=3`, `--output-file`) and the USART1 registers; the SPI
   suite reports the W25Q128 (`EF 40 18`), picks DMA, reads the ID with all
   three transports, and restores the 4 KiB scratch sector.

# 33. File inventory and commands

```text
Repository
├── third_party/stateos-stdc++/          the library (this document's subject)
├── rpi4b-smp/
│   ├── third_party/stateos-stdc++       symlink to the library
│   ├── micro-os-plus-iii-aarch64/
│   │   └── test/rpi4b/smp_test4/         the test
│   ├── micro-os-plus-iii-smp/            the kernel
│   └── tests/                            the harness, run.sh, CTest
├── pico2-smp-stdcpp/                     the Cortex-M application
│   ├── third_party/stateos-stdc++       symlink to the library
│   ├── micro-os-plus-iii-cortexm/
│   │   └── test/pico2/smp-stdcpp-test/   the test
│   ├── micro-os-plus-iii-smp/            the kernel
│   └── tests/                            the harness, run.sh, CTest
├── weactf411-spi-stdcpp/                 the Cortex-M4F + SPI application
│   ├── third_party/stateos-stdc++       symlink to the library
│   ├── micro-os-plus-iii-cortexm/
│   │   └── test/weactf411/spi-stdcpp-test/
│   ├── micro-os-plus-iii-smp/            the kernel
│   └── tests/                            the harness, run.sh, CTest
└── rpi-zero-2w-aarch64/                  the other AArch64 workspace
```

```bash
# build and run under QEMU
cd rpi4b-smp/tests
./run.sh smp_test4

# on the board
./run.sh smp_test4 --hwd

# only the hardware image, verbose
./run.sh --no-build --hwd-only -v smp_test4

# the TAP report
cat tests/build/aarch64-rpi4b-cmake-gcc-debug/platform-bin/port-tests/test/.qemu-logs/smp_test4.tap
cat tests/build/aarch64-rpi4b-cmake-gcc-debug/platform-bin/port-tests/test/.hw-logs/smp_test4.tap

# the Cortex-M application
cd /home/dan/Work/micro-os-scaffold-ctest-examples/pico2-smp-stdcpp/tests
./run.sh smp-stdcpp-test
./run.sh smp-stdcpp-test --hwd
cat build/cortexm-pico2-cmake-gcc-debug/platform-bin/port-tests/.qemu-logs/smp-stdcpp-test.tap
cat build/cortexm-pico2-cmake-gcc-debug/platform-bin/port-tests/.hw-logs/smp-stdcpp-test.tap

# the Cortex-M4F + SPI application (hardware only)
cd /home/dan/Work/micro-os-scaffold-ctest-examples/weactf411-spi-stdcpp/tests
./run.sh spi-stdcpp-test
cat build/cortexm-weactf411-cmake-gcc-debug/platform-bin/port-tests/.hw-logs/spi-stdcpp-test.tap
```

# 34. Summary

- **StateOS/stdc++** is the gthread layer plus the non-inline parts of `<thread>`, `<mutex>`, `<condition_variable>`, `<future>`, `<chrono>`, emulated TLS, and C++20 `barrier`/`latch`/`semaphore`, written against a small RTOS API.
- **The port** keeps the package's structure, replaces the StateOS kernel primitives with µOS++ ones, hides the kernel headers behind opaque storage so `<mutex>` and `os.h` do not include each other, converts absolute times to relative ticks, allocates threads from the free store, and implements TLS without `_GLIBCXX_HAVE_TLS`.
- **The test** (`smp_test4`) is a µTest++ application with seven suites, 75 checks and 37 cases, running on four cores. It is built by the same directory-listing rule as every other board test and registered with CTest as `aarch64-rpi4b-smp_test4-qemu` and `-hwd`.
- **The CTest cases** build the library into the image, run it under QEMU or over CMSIS-DAP on the board, read the `RESULT:` line, and keep the complete TAP report under the build tree.
- **Both runs pass:** 75/75 checks on QEMU (0.21 s) and on the Raspberry Pi 4 B (65.6 s CTest, 24.6 s image time).
- **A new project** is one `micro-os-scaffold` command, the library in `third_party/`, the test directory, two CMake hooks and one runner condition (chapter 29).
- **A second target**, a 2 × Cortex-M33 Pico 2, runs the same suites: 72 checks under QEMU (single-core emulated) and 75 on the board (chapter 29.11).
- **A third target**, a Cortex-M4F WeAct F411CE, adds the board's SPI NOR flash to the stdc++ suites: 104 checks on hardware, including the semihosting argc/argv, the USART1 configuration, a JEDEC ID read over all three transports and a scratch sector written and restored (chapter 29.12).

---

*Library: `third_party/stateos-stdc++/README.md` · Harness: `tests/ARCHITECTURE.md` · Workspace: `rpi4b-smp/README.md`, `rpi4b-smp/HOW-TO-RUN.md`*

<!-- PDF build: ./make-stdcxx-pdf.sh (pandoc + weasyprint) -->
