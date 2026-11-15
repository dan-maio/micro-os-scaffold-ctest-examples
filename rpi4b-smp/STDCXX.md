# StateOS stdc++ on µOS++ III

<p class="subtitle">A detailed guide to the StateOS stdc++ package, its port to µOS++ III SMP, and its use in a µTest++ test and a CTest case — from the GCC gthread contract to the Raspberry Pi 4 B hardware run</p>

**Library:** `third_party/stateos-stdc++/`<br>
**Test:** `rpi4b-smp/micro-os-plus-iii-aarch64/test/rpi4b/smp_test4/`<br>
**Workspace:** `rpi4b-smp/`<br>
**Target:** Raspberry Pi 4 B, BCM2711, 4 × Cortex-A72, AArch64, µOS++ III SMP<br>
**Toolchain:** `aarch64-none-elf-gcc` 15.2.1, C++20, `-fno-exceptions -fno-rtti`

This document answers three questions, in this order:

1. **What is StateOS/stdc++?** — the package, the GCC contract it implements, and what it assumes about its kernel.
2. **How was it ported to µOS++ III?** — the design, every file, and the problems that had to be solved on the way.
3. **How is it used in a test and in CTest?** — the CMake target, a µTest++ test, the QEMU and hardware CTest cases, and what a passing run looks like.

It is written for someone who has to maintain this port, adapt it to another board, or write a new test with it. It is not a libstdc++ tutorial; it explains only the parts the port touches.

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

# 29. Porting the library to another board

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

# 30. Troubleshooting

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

# 31. Validation log

The port was validated in this order; each step found something the previous one could not.

1. **Compile the library alone** with the toolchain include paths. Found the `__gthread_create` signature mismatch (`void (*)(void*)`) and the `void*` key casts.
2. **Compile the suites** against the library. Found the `<stop_token>`/`__cpp_lib_semaphore` ordering problem (the package's class was skipped when the macro was already defined).
3. **QEMU run.** Found the `memalign()` abort on the first `thread_local` access (chapter 13.3).
4. **QEMU, all suites.** 75 checks, 37 cases, 7 suites, 0 failed, 106 ms.
5. **Hardware run** on the Raspberry Pi 4 B, CMSIS-DAP probe, all four cores: 75 checks, 0 failed, 24.6 s, CTest 65.6 s.
6. **Regression run:** the workspace's other five QEMU cases still pass.

# 32. File inventory and commands

```text
Repository
├── third_party/stateos-stdc++/          the library (this document's subject)
├── rpi4b-smp/
│   ├── third_party/stateos-stdc++       symlink to the library
│   ├── micro-os-plus-iii-aarch64/
│   │   └── test/rpi4b/smp_test4/         the test
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
```

# 33. Summary

- **StateOS/stdc++** is the gthread layer plus the non-inline parts of `<thread>`, `<mutex>`, `<condition_variable>`, `<future>`, `<chrono>`, emulated TLS, and C++20 `barrier`/`latch`/`semaphore`, written against a small RTOS API.
- **The port** keeps the package's structure, replaces the StateOS kernel primitives with µOS++ ones, hides the kernel headers behind opaque storage so `<mutex>` and `os.h` do not include each other, converts absolute times to relative ticks, allocates threads from the free store, and implements TLS without `_GLIBCXX_HAVE_TLS`.
- **The test** (`smp_test4`) is a µTest++ application with seven suites, 75 checks and 37 cases, running on four cores. It is built by the same directory-listing rule as every other board test and registered with CTest as `aarch64-rpi4b-smp_test4-qemu` and `-hwd`.
- **The CTest cases** build the library into the image, run it under QEMU or over CMSIS-DAP on the board, read the `RESULT:` line, and keep the complete TAP report under the build tree.
- **Both runs pass:** 75/75 checks on QEMU (0.21 s) and on the Raspberry Pi 4 B (65.6 s CTest, 24.6 s image time).

---

*Library: `third_party/stateos-stdc++/README.md` · Harness: `tests/ARCHITECTURE.md` · Workspace: `rpi4b-smp/README.md`, `rpi4b-smp/HOW-TO-RUN.md`*

<!-- PDF build: ./make-stdcxx-pdf.sh (pandoc + weasyprint) -->
