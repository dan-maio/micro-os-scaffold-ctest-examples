// <barrier> -*- C++ -*-

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

// This implementation is based on libcxx/include/barrier
//===-- barrier.h --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------===//
// Adapted for µOS++ III (SMP) from the StateOS implementation, 2026.
//
// The StateOS version built on the kernel's raw wait lists. This one uses a
// µOS++ mutex and condition variable. The completion function runs with the
// barrier's mutex held; it must therefore not call back into the barrier.

#ifndef _GLIBCXX_BARRIER
#define _GLIBCXX_BARRIER 1

#include <cmsis-plus/rtos/os.h>

#include <cassert>
#include <cstddef>
#include <utility>

#pragma GCC system_header

#if __cplusplus > 201703L

#ifndef __cpp_lib_barrier
#define __cpp_lib_barrier 201907L
#endif

namespace std _GLIBCXX_VISIBILITY(default)
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION

  struct __empty_completion
  {
    void
    operator()() noexcept
    { }
  };

  template<typename _CompletionF = __empty_completion>
  class barrier
  {
  public:
    using arrival_token = ptrdiff_t;

    static constexpr ptrdiff_t
    max() noexcept
    { return __PTRDIFF_MAX__; }

    explicit
    barrier(ptrdiff_t __count, _CompletionF __completion = _CompletionF())
    : _M_phase(0), _M_expected(__count),
      _M_completion(std::move(__completion)), _M_pending(__count)
    { assert(__count >= 0 && __count <= max()); }

    barrier(barrier const&) = delete;
    barrier& operator=(barrier const&) = delete;

    arrival_token
    arrive(ptrdiff_t __update = 1) noexcept
    {
      assert(__update > 0 && __update <= _M_pending);

      (void)_M_mutex.lock();
      const arrival_token __token = _M_phase;
      if (__update > 0 && _M_pending > 0)
        {
          _M_pending -= __update;
          if (_M_pending <= 0)
            {
              _M_completion();
              ++_M_phase;
              _M_pending = _M_expected;
              (void)_M_cond.broadcast();
            }
        }
      (void)_M_mutex.unlock();
      return __token;
    }

    void
    wait(arrival_token __phase) noexcept
    {
      (void)_M_mutex.lock();
      while (__phase == _M_phase)
        {
          (void)_M_cond.wait(_M_mutex);
        }
      (void)_M_mutex.unlock();
    }

    void
    arrive_and_wait() noexcept
    { wait(arrive()); }

    void
    arrive_and_drop() noexcept
    {
      (void)_M_mutex.lock();
      if (_M_expected > 0)
        --_M_expected;
      if (_M_pending > 0)
        {
          --_M_pending;
          if (_M_pending <= 0)
            {
              _M_completion();
              ++_M_phase;
              _M_pending = _M_expected;
              (void)_M_cond.broadcast();
            }
        }
      (void)_M_mutex.unlock();
    }

  private:
    os::rtos::mutex             _M_mutex;
    os::rtos::condition_variable _M_cond;
    arrival_token               _M_phase;
    ptrdiff_t                   _M_expected;
    _CompletionF                _M_completion;
    ptrdiff_t                   _M_pending;
  };

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std

#endif // C++20
#endif // _GLIBCXX_BARRIER
