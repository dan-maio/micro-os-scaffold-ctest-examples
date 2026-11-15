// chrono -*- C++ -*-

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
// Both clocks count system ticks from start-up, the same epoch chrono's
// helpers in inc/chrono.hh assume when they turn an absolute time point back
// into a relative tick count.

#include <chrono>

#include <cmsis-plus/rtos/os.h>

#include "inc/chrono.hh"

namespace std _GLIBCXX_VISIBILITY(default)
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION

  namespace chrono
  {
_GLIBCXX_BEGIN_INLINE_ABI_NAMESPACE(_V2)

    constexpr bool system_clock::is_steady;

    system_clock::time_point
    system_clock::now () noexcept
    {
      return time_point (systick::duration (
          static_cast<systick::rep> (os::rtos::sysclock.steady_now ())));
    }

    constexpr bool steady_clock::is_steady;

    steady_clock::time_point
    steady_clock::now () noexcept
    {
      return time_point (systick::duration (
          static_cast<systick::rep> (os::rtos::sysclock.steady_now ())));
    }

_GLIBCXX_END_INLINE_ABI_NAMESPACE(_V2)
  } // namespace chrono

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std
