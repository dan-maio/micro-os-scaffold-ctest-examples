// <semaphore> -*- C++ -*-

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
// The toolchain's <semaphore> is built on libstdc++'s generic atomic-wait
// layer (a mutex and a condition variable). This package keeps the StateOS
// approach: this header is found first and implements the class directly on
// the kernel's counting semaphore, so a wait blocks in the scheduler. The
// toolchain's body is skipped through the same _GLIBCXX_SEMAPHORE guard.

#ifndef _GLIBCXX_SEMAPHORE
#define _GLIBCXX_SEMAPHORE 1

#include <cmsis-plus/rtos/os.h>

#include "chrono.hh"

#include <cassert>
#include <cstddef>

#pragma GCC system_header

#if __cplusplus > 201703L

#ifndef __cpp_lib_semaphore
#define __cpp_lib_semaphore 201907L
#endif

namespace std _GLIBCXX_VISIBILITY(default)
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION

  template<ptrdiff_t __least_max_value = __PTRDIFF_MAX__>
  class counting_semaphore
  {
    static_assert(__least_max_value >= 0);
    static_assert(__least_max_value <= __PTRDIFF_MAX__);

    // The kernel's counter is 16 bits wide.
    static constexpr ptrdiff_t _S_kernel_max
        = os::rtos::semaphore::max_count_value;

  public:
    explicit counting_semaphore(ptrdiff_t __desired = 0) noexcept
    : _M_sem("sem", static_cast<os::rtos::semaphore::count_t>(
                        __least_max_value < _S_kernel_max ? __least_max_value
                                                         : _S_kernel_max),
             static_cast<os::rtos::semaphore::count_t>(__desired))
    { assert(__desired >= 0 && __desired <= max()); }

    ~counting_semaphore() = default;

    counting_semaphore(const counting_semaphore&) = delete;
    counting_semaphore& operator=(const counting_semaphore&) = delete;

    static constexpr ptrdiff_t
    max() noexcept
    { return __least_max_value; }

    void
    release(ptrdiff_t __update = 1) noexcept
    {
      assert(__update >= 0);
      while (__update-- > 0)
        {
          (void)_M_sem.post();
        }
    }

    void
    acquire() noexcept
    { (void)_M_sem.wait(); }

    bool
    try_acquire() noexcept
    { return _M_sem.try_wait() == os::rtos::result::ok; }

    template<typename _Rep, typename _Period>
    bool
    try_acquire_for(const std::chrono::duration<_Rep, _Period>& __rtime) noexcept
    {
      return _M_sem.timed_wait(chrono::systick::count(__rtime))
             == os::rtos::result::ok;
    }

    template<typename _Clock, typename _Dur>
    bool
    try_acquire_until(const std::chrono::time_point<_Clock, _Dur>& __atime) noexcept
    {
      return _M_sem.timed_wait(chrono::systick::until(__atime))
             == os::rtos::result::ok;
    }

  private:
    os::rtos::semaphore_counting _M_sem;
  };

  using binary_semaphore = std::counting_semaphore<1>;

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std

#endif // C++20
#endif // _GLIBCXX_SEMAPHORE
