// mutex -*- C++ -*-

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
// _GLIBCXX_HAVE_TLS is not defined on the bare-metal target, so the
// std::call_once callable lives in a global std::function guarded by a
// global std::mutex. StateOS used a function-local static for that mutex;
// this port uses namespace-scope objects instead, because the toolchain's
// static-initialisation guard routines were built without gthreads.

#include <mutex>

#ifdef _GLIBCXX_HAS_GTHREADS

namespace std _GLIBCXX_VISIBILITY(default)
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION

#ifdef _GLIBCXX_HAVE_TLS
  __thread void* __once_callable;
  __thread void (*__once_call)();

  extern "C" void __once_proxy()
  {
    // The caller stored a function pointer in __once_call. If it requires
    // any state, it gets it from __once_callable.
    __once_call();
  }

#else // ! TLS

  // Explicit instantiation due to -fno-implicit-instantiation.
  template class function<void()>;

  function<void()> __once_functor;

  namespace
  {
    // Constructed during start-up, before the scheduler runs; no static
    // initialisation guard is involved.
    mutex once_mutex_instance;
    unique_lock<mutex> once_functor_lock{ once_mutex_instance, defer_lock };
    unique_lock<mutex>* once_functor_lock_ptr = nullptr;
  } // namespace

  mutex&
  __get_once_mutex()
  {
    return once_mutex_instance;
  }

  // Store ptr in a global variable and return the previous value.
  static inline unique_lock<mutex>*
  set_lock_ptr(unique_lock<mutex>* ptr)
  {
    unique_lock<mutex>* previous = once_functor_lock_ptr;
    once_functor_lock_ptr = ptr;
    return previous;
  }

  // code linked against ABI 3.4.12 and later uses this
  void
  __set_once_functor_lock_ptr(unique_lock<mutex>* __ptr)
  {
    (void) set_lock_ptr(__ptr);
  }

  // unsafe - retained for compatibility with ABI 3.4.11
  unique_lock<mutex>&
  __get_once_functor_lock()
  {
    return once_functor_lock;
  }

  // This is called via __gthread_once while __get_once_mutex() is locked.
  extern "C" void
  __once_proxy()
  {
    // Get the callable out of the global functor.
    function<void()> callable = std::move(__once_functor);

    // Then unlock the global mutex
    if (unique_lock<mutex>* lock = set_lock_ptr(nullptr))
    {
      // Caller is using the new ABI and provided a pointer to its lock.
      lock->unlock();
    }
    else
      __get_once_functor_lock().unlock();  // global lock

    // Finally, invoke the callable.
    callable();
  }
#endif // ! TLS

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std

#endif // _GLIBCXX_HAS_GTHREADS
