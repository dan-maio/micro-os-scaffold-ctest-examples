/******************************************************************************

    @file    stateos-stdc++: gthread.cc
    @brief   GCC gthread layer for µOS++ III (SMP), adapted from StateOS.

 ******************************************************************************

   Copyright (c) 2018-2025 Rajmund Szymanski. All rights reserved.
   Copyright (c) 2026 Dan. All rights reserved.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to
   deal in the Software without restriction, including without limitation the
   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
   sell copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
   IN THE SOFTWARE.

 ******************************************************************************/

// This translation unit is the only place where the gthread layer meets the
// kernel headers.
#include <cmsis-plus/rtos/os.h>

#include <bits/gthr-default.h>

#include <cerrno>
#include <cstdint>
#include <cstddef>
#include <new>

// -----------------------------------------------------------------------------
// The opaque storage declared in gthr-default.h must hold the kernel objects.
// -----------------------------------------------------------------------------

static_assert (sizeof (os::rtos::mutex) <= __GTHREAD_STORAGE_BYTES,
               "increase __GTHREAD_STORAGE_BYTES for os::rtos::mutex");
static_assert (sizeof (os::rtos::mutex_recursive) <= __GTHREAD_STORAGE_BYTES,
               "increase __GTHREAD_STORAGE_BYTES for os::rtos::mutex_recursive");
static_assert (sizeof (os::rtos::condition_variable) <= __GTHREAD_STORAGE_BYTES,
               "increase __GTHREAD_STORAGE_BYTES for "
               "os::rtos::condition_variable");

static_assert (alignof (os::rtos::mutex) <= __GTHREAD_STORAGE_ALIGNMENT,
               "increase __GTHREAD_STORAGE_ALIGNMENT");
static_assert (alignof (os::rtos::condition_variable)
                   <= __GTHREAD_STORAGE_ALIGNMENT,
               "increase __GTHREAD_STORAGE_ALIGNMENT");

namespace
{
  using mutex_t = os::rtos::mutex;
  using recursive_mutex_t = os::rtos::mutex_recursive;
  using cond_t = os::rtos::condition_variable;
  using thread_t = os::rtos::thread;

  inline mutex_t*
  as_mutex (__gthread_mutex_t* storage)
  {
    return reinterpret_cast<mutex_t*> (storage->__data);
  }

  inline recursive_mutex_t*
  as_recursive_mutex (__gthread_recursive_mutex_t* storage)
  {
    return reinterpret_cast<recursive_mutex_t*> (storage->__data);
  }

  inline cond_t*
  as_cond (__gthread_cond_t* storage)
  {
    return reinterpret_cast<cond_t*> (storage->__data);
  }

  // Convert libstdc++'s absolute time (built from chrono::steady_clock or
  // chrono::system_clock, both of which tick since start-up) into the
  // relative number of system ticks a µOS++ timed call expects.
  os::rtos::clock::duration_t
  timeout_ticks (const __gthread_time_t* abs_timeout)
  {
    constexpr std::int64_t billion = 1000000000LL;
    constexpr std::int64_t freq = os::rtos::clock_systick::frequency_hz;

    if (abs_timeout == nullptr)
      {
        return 0;
      }

    std::int64_t ns;
    if (abs_timeout->tv_sec > 1000000LL)
      {
        ns = 1000000LL * billion; // far enough in the future: no timeout
      }
    else
      {
        ns = abs_timeout->tv_sec * billion + abs_timeout->tv_nsec;
      }

    if (ns <= 0)
      {
        return 0;
      }

    // Round up: a delay is never shorter than requested.
    const std::int64_t abs_ticks = (ns * freq + billion - 1) / billion;
    const std::int64_t now_ticks
        = static_cast<std::int64_t> (os::rtos::sysclock.steady_now ());

    const std::int64_t rel = abs_ticks - now_ticks;
    if (rel <= 0)
      {
        return 0;
      }
    if (rel >= static_cast<std::int64_t> (UINT32_MAX))
      {
        return UINT32_MAX - 1;
      }
    return static_cast<os::rtos::clock::duration_t> (rel);
  }

  // --- Threads ---------------------------------------------------------------

  // The stack of every std::thread. The defaults fit the package's own tests;
  // an application whose threads need more (a FatFs file on the stack, a USB
  // service loop) sets STATEOS_STDCPP_THREAD_STACK_BYTES.
#if defined(STATEOS_STDCPP_THREAD_STACK_BYTES)
  constexpr std::size_t thread_stack_bytes = STATEOS_STDCPP_THREAD_STACK_BYTES;
#elif defined(__aarch64__) || defined(__x86_64__)
  constexpr std::size_t thread_stack_bytes = 16384;
#else
  constexpr std::size_t thread_stack_bytes = 4096;
#endif

  struct thread_start
  {
    void (*func) (void*);
    void* arg;
  };

  void*
  thread_trampoline (void* p)
  {
    thread_start* start = static_cast<thread_start*> (p);
    void (*func) (void*) = start->func;
    void* arg = start->arg;
    delete start;

    func (arg);

    // Run the per-thread destructors (thread_local variables, the
    // notify_all_at_thread_exit list, std::future's deferred results).
    __gthread_key_atexit ();

    return nullptr;
  }

  // Detached thread objects cannot be deleted from inside the thread itself
  // and µOS++ has no detached-thread reaper hook, so they are remembered here
  // and deleted by a later call once the kernel reports them terminated. The
  // table bounds the memory held by threads that were never reaped.
  constexpr unsigned detached_slots = 8;
  thread_t* detached_threads[detached_slots] = {};

  // detach () may be called from more than one thread.
  os::rtos::mutex detached_mutex{ "stdc++-detached" };

  void
  reap_detached_threads (void)
  {
    for (unsigned i = 0; i < detached_slots; ++i)
      {
        thread_t* th = detached_threads[i];
        if (th != nullptr && th->state () == thread_t::state::terminated)
          {
            detached_threads[i] = nullptr;
            delete th;
          }
      }
  }

  void
  remember_detached_thread (thread_t* th)
  {
    (void)detached_mutex.lock ();
    reap_detached_threads ();
    for (unsigned i = 0; i < detached_slots; ++i)
      {
        if (detached_threads[i] == nullptr)
          {
            detached_threads[i] = th;
            (void)detached_mutex.unlock ();
            return;
          }
      }
    // Table full: leave the object to the application; it stays alive.
    (void)detached_mutex.unlock ();
  }
} // namespace

// -----------------------------------------------------------------------------
// Mutexes.
// -----------------------------------------------------------------------------

extern "C" void
__gthread_mutex_init (__gthread_mutex_t* mutex)
{
  new (mutex->__data) mutex_t ();
}

extern "C" void
__gthread_recursive_mutex_init (__gthread_recursive_mutex_t* mutex)
{
  new (mutex->__data) recursive_mutex_t ();
}

extern "C" int
__gthread_mutex_destroy (__gthread_mutex_t* mutex)
{
  as_mutex (mutex)->~mutex_t ();
  return 0;
}

extern "C" int
__gthread_mutex_lock (__gthread_mutex_t* mutex)
{
  return as_mutex (mutex)->lock ();
}

extern "C" int
__gthread_mutex_trylock (__gthread_mutex_t* mutex)
{
  return as_mutex (mutex)->try_lock () == os::rtos::result::ok ? 0 : EBUSY;
}

extern "C" int
__gthread_mutex_timedlock (__gthread_mutex_t* mutex,
                           const __gthread_time_t* abs_timeout)
{
  return as_mutex (mutex)->timed_lock (timeout_ticks (abs_timeout));
}

extern "C" int
__gthread_mutex_unlock (__gthread_mutex_t* mutex)
{
  return as_mutex (mutex)->unlock ();
}

extern "C" int
__gthread_recursive_mutex_destroy (__gthread_recursive_mutex_t* mutex)
{
  as_recursive_mutex (mutex)->~recursive_mutex_t ();
  return 0;
}

extern "C" int
__gthread_recursive_mutex_lock (__gthread_recursive_mutex_t* mutex)
{
  return as_recursive_mutex (mutex)->lock ();
}

extern "C" int
__gthread_recursive_mutex_trylock (__gthread_recursive_mutex_t* mutex)
{
  return as_recursive_mutex (mutex)->try_lock () == os::rtos::result::ok
             ? 0
             : EBUSY;
}

extern "C" int
__gthread_recursive_mutex_timedlock (__gthread_recursive_mutex_t* mutex,
                                     const __gthread_time_t* abs_timeout)
{
  return as_recursive_mutex (mutex)->timed_lock (
      timeout_ticks (abs_timeout));
}

extern "C" int
__gthread_recursive_mutex_unlock (__gthread_recursive_mutex_t* mutex)
{
  return as_recursive_mutex (mutex)->unlock ();
}

// -----------------------------------------------------------------------------
// Once. A three-state flag: 0 = not run, 1 = running, 2 = done.
// -----------------------------------------------------------------------------

extern "C" int
__gthread_once (__gthread_once_t* once, void (*func) (void))
{
  if (once == nullptr || func == nullptr)
    {
      return EINVAL;
    }

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

// -----------------------------------------------------------------------------
// Condition variables.
// -----------------------------------------------------------------------------

extern "C" void
__gthread_cond_init (__gthread_cond_t* cond)
{
  new (cond->__data) cond_t ();
}

extern "C" int
__gthread_cond_destroy (__gthread_cond_t* cond)
{
  as_cond (cond)->~cond_t ();
  return 0;
}

extern "C" int
__gthread_cond_signal (__gthread_cond_t* cond)
{
  return as_cond (cond)->signal ();
}

extern "C" int
__gthread_cond_broadcast (__gthread_cond_t* cond)
{
  return as_cond (cond)->broadcast ();
}

extern "C" int
__gthread_cond_wait (__gthread_cond_t* cond, __gthread_mutex_t* mutex)
{
  return as_cond (cond)->wait (*as_mutex (mutex));
}

extern "C" int
__gthread_cond_wait_recursive (__gthread_cond_t* cond,
                               __gthread_recursive_mutex_t* mutex)
{
  return as_cond (cond)->wait (*as_recursive_mutex (mutex));
}

extern "C" int
__gthread_cond_timedwait (__gthread_cond_t* cond,
                          __gthread_mutex_t* mutex,
                          const __gthread_time_t* abs_timeout)
{
  return as_cond (cond)->timed_wait (*as_mutex (mutex),
                                     timeout_ticks (abs_timeout));
}

// -----------------------------------------------------------------------------
// Threads.
// -----------------------------------------------------------------------------

extern "C" int
__gthread_create (__gthread_t* thread, void (*func) (void*), void* args)
{
  if (thread == nullptr || func == nullptr)
    {
      return EINVAL;
    }

  thread_start* start = new thread_start{ func, args };

  thread_t::attributes attr;
  attr.th_stack_size_bytes = thread_stack_bytes;
  attr.th_priority = thread_t::priority::normal;

  thread_t* th = new thread_t{ "stdc++", &thread_trampoline, start, attr };

  *thread = static_cast<__gthread_t> (static_cast<void*> (th));
  return 0;
}

extern "C" int
__gthread_join (__gthread_t thread, void** value_ptr)
{
  if (thread == nullptr)
    {
      return EINVAL;
    }

  thread_t* th = static_cast<thread_t*> (thread);
  const int result = th->join (value_ptr);
  delete th;
  return result;
}

extern "C" int
__gthread_detach (__gthread_t thread)
{
  if (thread == nullptr)
    {
      return EINVAL;
    }

  thread_t* th = static_cast<thread_t*> (thread);
  const int result = th->detach ();
  if (result == os::rtos::result::ok)
    {
      remember_detached_thread (th);
    }
  return result;
}

extern "C" int
__gthread_equal (__gthread_t t1, __gthread_t t2)
{
  return t1 == t2 ? 0 : 1;
}

extern "C" __gthread_t
__gthread_self (void)
{
  return static_cast<__gthread_t> (
      static_cast<void*> (&os::rtos::this_thread::thread ()));
}

extern "C" int
__gthread_yield (void)
{
  os::rtos::this_thread::yield ();
  return 0;
}
