/******************************************************************************

    @file    stateos-stdc++: critical_section.hh
    @brief   Scheduler critical section for the µOS++ III port.

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

#ifndef __STATEOS_STDCXX_CRITICAL_SECTION_HH
#define __STATEOS_STDCXX_CRITICAL_SECTION_HH

#include <cmsis-plus/rtos/os.h>

namespace std
{
  // StateOS parks the scheduler in its own core lock. µOS++ has an RAII
  // scheduler critical section that stops thread switches on the calling
  // core; it nests and restores the previous state.
  struct critical_section
  {
    critical_section () = default;
    ~critical_section () = default;

    critical_section (critical_section&&) = delete;
    critical_section (const critical_section&) = delete;
    critical_section& operator= (critical_section&&) = delete;
    critical_section& operator= (const critical_section&) = delete;

  private:
    os::rtos::scheduler::critical_section section_;
  };
} // namespace std

#endif // __STATEOS_STDCXX_CRITICAL_SECTION_HH
