/******************************************************************************

    @file    stateos-stdc++: emutls.cc
    @brief   Emulated thread-local storage for the µOS++ III port.

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

// The GCC emulated TLS entry points. A C++ thread_local variable on this
// target compiles to a call to __emutls_get_address(), which allocates one
// block per thread and stores it under a thread-specific key; the key's
// destructor frees it when the thread exits.

#include <mutex>
#include <system_error>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "bits/gthr.h"

#ifdef _GLIBCXX_HAS_GTHREADS

extern "C"
{
#ifndef EMUTLS_ATTR
#define EMUTLS_ATTR
#endif

EMUTLS_ATTR
void* __emutls_get_address (void*);

EMUTLS_ATTR
void __emutls_register_common (void*, size_t, size_t, void*);
}

// Namespace scope, so its construction does not need a static-init guard.
static std::mutex emutls_mutex{};

struct __emutls_object
{
  size_t size; // object size
  size_t align; // object alignment
  void* key; // __gthread_key_t
  void* init; // initial value
};

// µOS++'s libc does not implement memalign() (the reentrant _memalign_r stub
// calls abort()). Allocate a little more and align by hand; the raw pointer is
// stored just before the aligned block so the key destructor can free it.
static void
emutls_free (void* ptr)
{
  if (ptr != nullptr)
    {
      ::free (static_cast<void**> (ptr)[-1]);
    }
}

static void*
emutls_alloc (__emutls_object* obj)
{
  size_t align = obj->align < sizeof (void*) ? sizeof (void*) : obj->align;

  unsigned char* raw = static_cast<unsigned char*> (
      ::malloc (obj->size + align + sizeof (void*)));
  if (raw == nullptr)
    return nullptr;

  const std::uintptr_t base
      = reinterpret_cast<std::uintptr_t> (raw) + sizeof (void*);
  const std::uintptr_t aligned
      = (base + (align - 1)) & ~static_cast<std::uintptr_t> (align - 1);

  void* ptr = reinterpret_cast<void*> (aligned);
  static_cast<void**> (ptr)[-1] = raw;

  if (obj->init == nullptr)
    return ::memset (ptr, 0, obj->size);
  else
    return ::memcpy (ptr, obj->init, obj->size);
}

void*
__emutls_get_address (void* ptr)
{
  __emutls_object* obj = static_cast<__emutls_object*> (ptr);
  __gthread_key_t key = static_cast<__gthread_key_t> (obj->key);
  if (key == nullptr)
    {
      std::lock_guard<std::mutex> lock (emutls_mutex);
      key = static_cast<__gthread_key_t> (obj->key);
      if (key == nullptr)
        {
          if (__gthread_key_create (&key, emutls_free) != 0)
            std::__throw_system_error (ENOMEM);
          obj->key = static_cast<void*> (key);
        }
    }
  void* address = __gthread_getspecific (key);
  if (address == nullptr)
    {
      address = emutls_alloc (obj);
      if (address == nullptr || __gthread_setspecific (key, address) != 0)
        std::__throw_system_error (ENOMEM);
    }
  return address;
}

void
__emutls_register_common (void* ptr, size_t size, size_t align, void* init)
{
  __emutls_object* obj = static_cast<__emutls_object*> (ptr);
  if (obj->size < size)
    {
      obj->size = size;
      obj->init = init;
    }
  if (obj->align < align)
    obj->align = align;
}

// GCC 15 on 32-bit ARM calls __aeabi_read_tp from the thread-local-storage
// helpers. StateOS returns one static block (single-threaded TLS); the
// AArch64 target uses the emulated TLS path above and needs none of this.
#if defined(__arm__) && (__GNUC__ >= 15)

extern "C" void*
__aeabi_read_tp ()
{
  static char tls[1024] __attribute__ ((aligned (8)));
  return tls;
}

#endif

#endif // _GLIBCXX_HAS_GTHREADS
