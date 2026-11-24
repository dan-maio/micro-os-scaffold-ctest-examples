/******************************************************************************

    @file    stateos-stdc++: chrono.hh
    @brief   chrono helpers for the µOS++ III port, adapted from StateOS.

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

#ifndef __STATEOS_STDCXX_CHRONO_HH
#define __STATEOS_STDCXX_CHRONO_HH

#include <cmsis-plus/rtos/os.h>

#include <bits/chrono.h>
#include <cstdint>

namespace std
{
  namespace chrono
  {
    // The kernel's system clock, exposed as a chrono clock so that the
    // <semaphore>, <latch> and <barrier> implementations can convert a
    // duration or a time point into the tick count a µOS++ timed call
    // expects.
    struct systick
    {
      using rep = std::int64_t;
      using period = ratio<1, OS_INTEGER_SYSTICK_FREQUENCY_HZ>;
      using duration = chrono::duration<rep, period>;
      using time_point = chrono::time_point<systick, duration>;

      static constexpr bool is_steady = true;

      static time_point
      now () noexcept
      {
        return time_point (duration (
            static_cast<rep> (os::rtos::sysclock.steady_now ())));
      }

      // Rounded up to whole ticks: a delay is never shorter than requested.
      // Negative delays become 0 (poll once), out-of-range delays saturate.
      template <class Rep, class Period>
      static os::rtos::clock::duration_t
      count (const chrono::duration<Rep, Period>& delay)
      {
        auto ticks = duration_cast<duration> (delay);
        if (ticks < delay)
          {
            ticks += duration (1);
          }

        constexpr rep limit
            = static_cast<rep> (static_cast<std::uint32_t> (-2));
        const rep value = ticks.count ();
        if (value <= 0)
          {
            return 0;
          }
        if (value >= limit)
          {
            return static_cast<os::rtos::clock::duration_t> (limit);
          }
        return static_cast<os::rtos::clock::duration_t> (value);
      }

      // Any clock: the time point minus that clock's own now ().
      template <class Clock, class Duration>
      static os::rtos::clock::duration_t
      until (const chrono::time_point<Clock, Duration>& time)
      {
        return count (time - Clock::now ());
      }

      static constexpr os::rtos::clock::duration_t
      count (os::rtos::clock::duration_t ticks)
      {
        return ticks;
      }
    };
  } // namespace chrono
} // namespace std

#endif // __STATEOS_STDCXX_CHRONO_HH
