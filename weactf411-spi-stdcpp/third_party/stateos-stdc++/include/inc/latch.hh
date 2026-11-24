// <latch> -*- C++ -*-

// Copyright (C) 2020-2021 Free Software Foundation, Inc.
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

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

// ---------------------------------------------------
// Adapted for µOS++ III (SMP) from the StateOS implementation, 2026.
//
// The StateOS version built on the kernel's raw wait lists. This one uses a
// µOS++ mutex and condition variable, which keeps it independent of the
// kernel's internal task lists and correct on every core.

#ifndef _GLIBCXX_LATCH
#define _GLIBCXX_LATCH 1

#include <cmsis-plus/rtos/os.h>

#include <cassert>
#include <cstddef>

#pragma GCC system_header

#if __cplusplus > 201703L

#ifndef __cpp_lib_latch
#define __cpp_lib_latch 201907L
#endif

namespace std _GLIBCXX_VISIBILITY(default)
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION

  class latch
  {
  public:
    static constexpr ptrdiff_t
    max() noexcept
    { return __PTRDIFF_MAX__; }

    explicit latch(ptrdiff_t __expected) noexcept : _M_latch(__expected)
    { assert(__expected >= 0 && __expected <= max()); }

    ~latch() = default;

    latch(const latch&) = delete;
    latch& operator=(const latch&) = delete;

    void
    count_down(ptrdiff_t __update = 1) noexcept
    {
      assert(__update >= 0 && __update <= _M_latch);
      if (__update <= 0)
        return;

      (void)_M_mutex.lock();
      _M_latch -= __update;
      if (_M_latch <= 0)
        {
          _M_latch = 0;
          (void)_M_cond.broadcast();
        }
      (void)_M_mutex.unlock();
    }

    bool
    try_wait() noexcept
    {
      (void)_M_mutex.lock();
      const bool __done = _M_latch == 0;
      (void)_M_mutex.unlock();
      return __done;
    }

    void
    wait() noexcept
    {
      (void)_M_mutex.lock();
      while (_M_latch != 0)
        {
          (void)_M_cond.wait(_M_mutex);
        }
      (void)_M_mutex.unlock();
    }

    void
    arrive_and_wait(ptrdiff_t __update = 1) noexcept
    {
      count_down(__update);
      wait();
    }

  private:
    os::rtos::mutex             _M_mutex;
    os::rtos::condition_variable _M_cond;
    ptrdiff_t                   _M_latch;
  };

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std

#endif // C++20
#endif // _GLIBCXX_LATCH
