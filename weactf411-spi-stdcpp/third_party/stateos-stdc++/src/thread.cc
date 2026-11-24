// thread -*- C++ -*-

// Copyright (C) 2008-2025 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

// ---------------------------------------------------
// Adapted for µOS++ III (SMP) from StateOS, 2026.
//
// The thread entry points (__gthread_create/join/detach/equal/self/yield)
// live in gthread.cc. What remains here is the libstdc++ `std::thread` part,
// the thread-specific key table, and this_thread::__sleep_for.

#include <thread>
#include <mutex>
#include <unordered_map>
#include <system_error>
#include <cassert>
#include <cxxabi.h>

#include <cmsis-plus/rtos/os.h>

#include "inc/chrono.hh"

#ifdef _GLIBCXX_HAS_GTHREADS

// The key table is guarded by a kernel mutex; a std::mutex here would come
// back through the very functions below.
static os::rtos::mutex key_mutex{ "stdc++-keys" };

struct oskey_t : public std::unordered_map<__gthread_t, void*>
{
  using unordered_map<__gthread_t, void*>::unordered_map;
};

static std::unordered_map<__gthread_key_t, void (*) (void*)> key_map{};

static inline oskey_t*
as_key (__gthread_key_t key)
{
  return static_cast<oskey_t*> (key);
}

int
__gthread_key_create (__gthread_key_t* keyp, void (*dtor) (void*))
{
  assert (keyp);
  (void)key_mutex.lock ();
#if __cplusplus < 201402L
  auto ptr = std::unique_ptr<oskey_t> (new oskey_t ());
#else
  auto ptr = std::make_unique<oskey_t> ();
#endif
  if (!ptr || !key_map.insert ({ ptr.get (), dtor }).second)
    {
      (void)key_mutex.unlock ();
      return 1;
    }
  *keyp = ptr.release ();
  (void)key_mutex.unlock ();
  return 0;
}

int
__gthread_key_delete (__gthread_key_t key)
{
  assert (key);
  (void)key_mutex.lock ();
  auto ptr = std::unique_ptr<oskey_t> (static_cast<oskey_t*> (key));
  const bool erased = key_map.erase (ptr.get ()) != 0;
  (void)key_mutex.unlock ();
  return erased ? 0 : 1;
}

void*
__gthread_getspecific (__gthread_key_t key)
{
  assert (key);
  (void)key_mutex.lock ();
  auto it = as_key (key)->find (__gthread_self ());
  void* value = it != as_key (key)->end () ? it->second : nullptr;
  (void)key_mutex.unlock ();
  return value;
}

int
__gthread_setspecific (__gthread_key_t key, const void* ptr)
{
  assert (key);
  (void)key_mutex.lock ();
#if __cplusplus < 201703L
  (*as_key (key))[__gthread_self ()] = const_cast<void*> (ptr);
#else
  as_key (key)->insert_or_assign (__gthread_self (), const_cast<void*> (ptr));
#endif
  (void)key_mutex.unlock ();
  return 0;
}

// Called by the thread trampoline in gthread.cc once the thread function has
// returned: run, then clear, every value this thread stored under a key that
// has a destructor.
extern "C" void
__gthread_key_atexit (void)
{
  (void)key_mutex.lock ();
  auto task = __gthread_self ();
#if __cplusplus < 201703L
  for (auto const& item : key_map)
  {
    auto const& key = item.first;
    auto const& dtor = item.second;
#else
  for (auto const& [key, dtor] : key_map)
  {
#endif
    auto const& it = as_key (key)->find (task);
    if (it != as_key (key)->end ())
    {
      void* ptr = it->second;
      if (dtor != nullptr && ptr != nullptr)
        dtor (ptr);
      as_key (key)->erase (it);
    }
  }
  (void)key_mutex.unlock ();
}

namespace std _GLIBCXX_VISIBILITY(default)
{
  extern "C"
  {
    static void
    execute_native_thread_routine (void* __p)
    {
      thread::_State_ptr __t{ static_cast<thread::_State*> (__p) };
      __t->_M_run ();
      __gthread_key_atexit ();
    }

#if _GLIBCXX_THREAD_ABI_COMPAT
    static void
    execute_native_thread_routine_compat (void* __p)
    {
      thread::_Impl_base* __t = static_cast<thread::_Impl_base*> (__p);
      thread::__shared_base_type __local;
      // Now that a new thread has been created we can transfer ownership of
      // the thread state to a local object, breaking the reference cycle
      // created in thread::_M_start_thread.
      __local.swap (__t->_M_this_ptr);
      __t->_M_run ();
      __gthread_key_atexit ();
    }
#endif
  } // extern "C"

_GLIBCXX_BEGIN_NAMESPACE_VERSION

  thread::_State::~_State () = default;

  void
  thread::join ()
  {
    int __e = EINVAL;

    if (_M_id != id ())
      __e = __gthread_join (_M_id._M_thread, nullptr);

    if (__e)
      __throw_system_error (__e);

    _M_id = id ();
  }

  void
  thread::detach ()
  {
    int __e = EINVAL;

    if (_M_id != id ())
      __e = __gthread_detach (_M_id._M_thread);

    if (__e)
      __throw_system_error (__e);

    _M_id = id ();
  }

  void
  thread::_M_start_thread (_State_ptr state, void (*) ())
  {
    const int err = __gthread_create (&_M_id._M_thread,
                                      &execute_native_thread_routine,
                                      state.get ());
    if (err)
      __throw_system_error (err);
    state.release ();
  }

#if _GLIBCXX_THREAD_ABI_COMPAT
  void
  thread::_M_start_thread (__shared_base_type __b)
  {
    _M_start_thread (std::move (__b), nullptr);
  }

  void
  thread::_M_start_thread (__shared_base_type __b, void (*) ())
  {
    auto ptr = __b.get ();
    // Create a reference cycle that will be broken in the new thread.
    ptr->_M_this_ptr = std::move (__b);
    int __e = __gthread_create (&_M_id._M_thread,
                                &execute_native_thread_routine_compat, ptr);
    if (__e)
      {
        ptr->_M_this_ptr.reset (); // break reference cycle, destroying *ptr.
        __throw_system_error (__e);
      }
  }
#endif

  unsigned int
  thread::hardware_concurrency () noexcept
  {
    return OS_NCPU != 0 ? OS_NCPU : 0;
  }

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std

#endif // _GLIBCXX_HAS_GTHREADS

#ifndef _GLIBCXX_NO_SLEEP
namespace std _GLIBCXX_VISIBILITY(default)
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION
namespace this_thread
{
  void
  __sleep_for (chrono::seconds __s, chrono::nanoseconds __ns)
  {
    using ticks_t = os::rtos::clock::duration_t;

    const ticks_t s = chrono::systick::count (__s);
    const ticks_t n = chrono::systick::count (__ns);
    const ticks_t max = static_cast<ticks_t> (-2);
    const ticks_t total = (s > max - n) ? max : static_cast<ticks_t> (s + n);

    (void)os::rtos::sysclock.sleep_for (total);
  }
}
_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std
#endif // ! NO_SLEEP
