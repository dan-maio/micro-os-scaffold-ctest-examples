/******************************************************************************

    @file    stateos-stdc++: gthr-default.h
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

#ifndef _GLIBCXX_GCC_GTHR_MICRO_OS_PLUS_H
#define _GLIBCXX_GCC_GTHR_MICRO_OS_PLUS_H

// -----------------------------------------------------------------------------
// This file is found as <bits/gthr-default.h> because this package's include
// directory is put BEFORE the toolchain's C++ include directory.
//
// The declared functions are defined in src/gthread.cc, one translation unit
// that sees the full µOS++ headers. Keeping the kernel headers out of this
// file matters: <bits/gthr.h> is included from inside <mutex>, and the kernel
// headers include <mutex> themselves. Opaque, fixed-size storage plus
// out-of-line functions breaks that cycle.
// -----------------------------------------------------------------------------

#ifndef _GLIBCXX_HAS_GTHREADS
#error "Required _GLIBCXX_HAS_GTHREADS (third_party/stateos-stdc++/CMakeLists.txt defines it)."
#endif

#if __cplusplus < 201103L
#error "Required C++11 or above."
#endif

// -----------------------------------------------------------------------------

#define __GTHREADS 1
#define __GTHREADS_CXX0X 1
#define __GTHREAD_HAS_COND 1
#define _GLIBCXX_THREAD_IMPL 1
#define _GLIBCXX_USE_SCHED_YIELD 1

// _GLIBCXX_HAVE_TLS is deliberately NOT defined: the AArch64 bare-metal
// target has no run-time TLS block. libstdc++ then keeps the std::call_once
// callable in a global (mutex protected) instead of a __thread variable, and
// C++ thread_local variables go through the emulated TLS routines in
// src/emutls.cc.

// -----------------------------------------------------------------------------
// Opaque storage.
//
// libstdc++ embeds __gthread_mutex_t, __gthread_cond_t and
// __gthread_recursive_mutex_t by value, so the types must be complete. They
// hold enough aligned bytes for the corresponding µOS++ object; gthread.cc
// static_asserts that the sizes and alignments still fit.

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

// A thread is identified by the address of its µOS++ thread object, which is
// also what os::rtos::this_thread::thread () returns. libstdc++ only compares
// and stores the value.
typedef void* __gthread_t;

typedef void* __gthread_key_t;

// State of std::once_flag. Zero is the constant initialiser.
typedef int __gthread_once_t;
#define __GTHREAD_ONCE_INIT 0

// Absolute time, as libstdc++ builds it from a chrono time_point. The layout
// matches the two-member aggregate libstdc++ initialises.
struct __gthread_time_t
{
  long long tv_sec;
  long tv_nsec;
};

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C"
{
#endif

static inline int
__gthread_active_p (void)
{
  return 1;
}

// --- Mutexes ----------------------------------------------------------------

void __gthread_mutex_init (__gthread_mutex_t* mutex);
void __gthread_recursive_mutex_init (__gthread_recursive_mutex_t* mutex);

int __gthread_mutex_destroy (__gthread_mutex_t* mutex);
int __gthread_mutex_lock (__gthread_mutex_t* mutex);
int __gthread_mutex_trylock (__gthread_mutex_t* mutex);
int __gthread_mutex_timedlock (__gthread_mutex_t* mutex,
                               const __gthread_time_t* abs_timeout);
int __gthread_mutex_unlock (__gthread_mutex_t* mutex);

int __gthread_recursive_mutex_destroy (__gthread_recursive_mutex_t* mutex);
int __gthread_recursive_mutex_lock (__gthread_recursive_mutex_t* mutex);
int __gthread_recursive_mutex_trylock (__gthread_recursive_mutex_t* mutex);
int __gthread_recursive_mutex_timedlock (
    __gthread_recursive_mutex_t* mutex,
    const __gthread_time_t* abs_timeout);
int __gthread_recursive_mutex_unlock (__gthread_recursive_mutex_t* mutex);

#define __GTHREAD_MUTEX_INIT_FUNCTION(__mutex) \
  __gthread_mutex_init (__mutex)
#define __GTHREAD_RECURSIVE_MUTEX_INIT_FUNCTION(__mutex) \
  __gthread_recursive_mutex_init (__mutex)

// --- Once -------------------------------------------------------------------

int __gthread_once (__gthread_once_t* once, void (*func) (void));

// --- Condition variables ----------------------------------------------------

void __gthread_cond_init (__gthread_cond_t* cond);
int __gthread_cond_destroy (__gthread_cond_t* cond);
int __gthread_cond_signal (__gthread_cond_t* cond);
int __gthread_cond_broadcast (__gthread_cond_t* cond);
int __gthread_cond_wait (__gthread_cond_t* cond,
                         __gthread_mutex_t* mutex);
int __gthread_cond_wait_recursive (__gthread_cond_t* cond,
                                   __gthread_recursive_mutex_t* mutex);
int __gthread_cond_timedwait (__gthread_cond_t* cond,
                              __gthread_mutex_t* mutex,
                              const __gthread_time_t* abs_timeout);

#define __GTHREAD_COND_INIT_FUNCTION(__cond) __gthread_cond_init (__cond)

// --- Threads ----------------------------------------------------------------

int __gthread_create (__gthread_t* thread, void (*func) (void*),
                      void* args);
int __gthread_join (__gthread_t thread, void** value_ptr);
int __gthread_detach (__gthread_t thread);
int __gthread_equal (__gthread_t t1, __gthread_t t2);
__gthread_t __gthread_self (void);
int __gthread_yield (void);

// Called by the thread trampoline after the thread function returns: runs the
// per-thread destructors registered with __gthread_key_create ().
void __gthread_key_atexit (void);

// --- Thread specific data ---------------------------------------------------

int __gthread_key_create (__gthread_key_t* keyp, void (*dtor) (void*));
int __gthread_key_delete (__gthread_key_t key);
void* __gthread_getspecific (__gthread_key_t key);
int __gthread_setspecific (__gthread_key_t key, const void* ptr);

#ifdef __cplusplus
} // extern "C"
#endif

// -----------------------------------------------------------------------------

#endif // _GLIBCXX_GCC_GTHR_MICRO_OS_PLUS_H
