# Code differences: smp2 vs smp-riscv

Date: 2026-11-26

This document compares two working copies of `micro-os-plus-iii`:

- **A** = `micro-os-plus-iii-smp2/micro-os-plus-iii` (this repository, branch `main`, at `be5c50da`)
- **B** = `micro-os-plus-iii-smp-riscv/micro-os-plus-iii`

Only code is compared. Excluded: `docs/`, `*.md`, `*.pdf`, `.git`, `build`, `xpacks`, `node_modules`, `.vscode`, `package-lock.json`.

In every diff below, `-` lines are A and `+` lines are B.

## Summary

| Area | What differs |
|---|---|
| Kernel `src/` | Only two files. `os-idle.cpp`: A has an SMP fix B lacks. `trace.cpp`: B has a RISC-V fix A lacks. |
| Build files | Top `CMakeLists.txt` (where the test-support files live), `cmake/uos-app.cmake` (A allows LIBRARIES-only apps), `package.json` version, `.gitignore`. |
| Test sources | Four files, all RISC-V or 64-bit fixes in B. |
| Test plumbing | A finds the ports through xpm links in the build folder. B finds them as sibling folders and adds a RISC-V case. Every platform's CMake files differ to match. |
| Files in one tree only | B adds `test_smpl/` and five `riscv-*` platforms. A has `board-tests/`, `smp-support/`, `scripts/`, `third_party/`, more tests and per-board sources. |

The two `src/` fixes are in separate files, so either one can be merged into the other tree easily.

## 1. Kernel sources (`src/`)

**`src/rtos/os-idle.cpp`**: with the SMP scheduler on, A builds the idle thread into raw storage (placement `new`) or allocates it with `new`, and never destroys it. Without this, `exit()` on a core other than 0 hangs, because `thread::kill()` waits for ever for core 0's idle thread. This was seen on the Luckfox Lyra with `smp_test_int2`. B still has upstream's version, where the idle thread is a static object with a destructor.

**`src/diag/trace.cpp`**: B makes the alias block Arm-only. The RISC-V port defines `__ARM_EABI__` only to select the kernel's embedded code paths, but the hard-coded mangled `va_list` names (`St9__va_list`) are Arm ones. On RISC-V, B takes the portable `#else` branch instead.

### `src/rtos/os-idle.cpp`

```diff
--- A/src/rtos/os-idle.cpp
+++ B/src/rtos/os-idle.cpp
@@ -16,8 +16,6 @@
 #include <cmsis-plus/rtos/os.h>
 #include <cmsis-plus/diag/instrumentation.h>
 
-#include <new>
-
 #if !defined(OS_DISABLE_WAIT_FOR_INTERRUPT) \
     && (defined(OS_USE_TRACE_SEGGER_RTT) || defined(SEGGER_SYSVIEW_VERSION))
 // When using SEGGER RTT (for trace or SystemView), the core cannot enter WFI.
@@ -151,28 +149,8 @@
 #pragma clang diagnostic ignored "-Wmissing-variable-declarations"
 #endif
 
-#if defined(OS_USE_SMP_SCHEDULER)
-
-// SMP: exit() must not destroy the idle thread. exit() runs the static
-// destructors on whichever core the calling thread is on; when that is not
-// core 0, core 0's idle thread is the thread running there, and nothing else
-// can replace it, so thread::kill() waits for it for ever (seen on the
-// Luckfox Lyra: smp_test_int2's main thread exits on core 1). The secondary
-// cores' idle threads are never destroyed either. The storage is therefore
-// not an object with a destructor: the thread is constructed into it by
-// os_startup_create_thread_idle() (or allocated, with dynamic memory) and
-// lives until the end of the run. Single-core builds keep upstream's code.
-
 #if defined(OS_EXCLUDE_DYNAMIC_MEMORY_ALLOCATIONS)
 
-alignas (thread_inclusive<OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES>) static char
-    os_idle_thread_storage_[sizeof (
-        thread_inclusive<OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES>)];
-
-#endif /* defined(OS_EXCLUDE_DYNAMIC_MEMORY_ALLOCATIONS) */
-
-#elif defined(OS_EXCLUDE_DYNAMIC_MEMORY_ALLOCATIONS)
-
 static thread_inclusive<OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES> os_idle_thread_{
   "idle", os_idle, nullptr
 };
@@ -181,34 +159,15 @@
 
 static std::unique_ptr<thread> os_idle_thread_;
 
-#endif /* defined(OS_USE_SMP_SCHEDULER) */
+#endif /* defined(OS_EXCLUDE_DYNAMIC_MEMORY_ALLOCATIONS) */
 
 #pragma GCC diagnostic pop
 
 void __attribute__ ((weak))
 os_startup_create_thread_idle (void)
 {
-#if defined(OS_USE_SMP_SCHEDULER)
-
 #if defined(OS_EXCLUDE_DYNAMIC_MEMORY_ALLOCATIONS)
 
-  // Constructed in place; never destroyed (see above).
-  os_idle_thread = new (os_idle_thread_storage_)
-      thread_inclusive<OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES>{ "idle", os_idle,
-                                                               nullptr };
-
-#else
-
-  thread::attributes attr = thread::initializer;
-  attr.th_stack_size_bytes = OS_INTEGER_RTOS_IDLE_STACK_SIZE_BYTES;
-
-  // Allocated; never deleted (see above).
-  os_idle_thread = new thread ("idle", os_idle, nullptr, attr);
-
-#endif /* defined(OS_EXCLUDE_DYNAMIC_MEMORY_ALLOCATIONS) */
-
-#elif defined(OS_EXCLUDE_DYNAMIC_MEMORY_ALLOCATIONS)
-
   // The thread object instance was created by the static constructors.
   os_idle_thread = &os_idle_thread_;
 
```

### `src/diag/trace.cpp`

```diff
--- A/src/diag/trace.cpp
+++ B/src/diag/trace.cpp
@@ -181,12 +181,17 @@
 
 // ----------------------------------------------------------------------------
 
-#if defined(__ARM_EABI__)
+#if defined(__ARM_EABI__) && (defined(__arm__) || defined(__aarch64__))
 
 // For embedded platforms, optimise with aliases.
 //
 // Aliases can only refer symbols defined in the same translation unit
 // and C++ de-mangling must be done manually.
+//
+// The mangled names are the Arm ones: `va_list` is std::__va_list
+// (St9__va_list) in both the AAPCS and AAPCS64. A RISC-V build, which defines
+// __ARM_EABI__ only to select the kernel's embedded code paths, mangles
+// va_list differently and takes the portable #else branch below.
 
 int __attribute__ ((weak, alias ("_ZN2os5trace6printfEPKcz")))
 trace_printf (const char* format, ...);
```

## 2. Build files

- **`CMakeLists.txt`**: A takes `hw_result.hpp` and `board-contract.cpp` from `tests/smp-support/`. B looks there first and falls back to `test_smpl/`, which exists only in B.
- **`cmake/uos-app.cmake`**: A accepts `SOURCES` *or* `LIBRARIES`, so an app can come entirely from a test library (`test::<suite>`). B requires `SOURCES`.
- **`package.json`**: `7.2.0-pre.1` in A, `7.1.0` in B.
- **`.gitignore`**: A also ignores the `kbd_forward` host builds, `__pycache__/`, `*.pyc` and `*.bak`.

### `CMakeLists.txt`

```diff
--- A/CMakeLists.txt
+++ B/CMakeLists.txt
@@ -1,8 +1,8 @@
 # -----------------------------------------------------------------------------
 # micro-os-plus-iii-smp — the common half of the unified µOS++ III SMP project.
 #
-# This repository is the RTOS kernel (src/, include/) and the shared test
-# scaffolding both silicon ports use (tests/smp-support/). Nothing else.
+# This repository is the RTOS kernel (src/, include/) and the runners that
+# drive a build on QEMU or on hardware (test_smpl/). Nothing else.
 #
 # The test APPLICATIONS are not here. Every board owns its own, in
 # boards/<id>/test/ of the architecture project that has that board, because a
@@ -211,32 +211,44 @@
 add_library (micro-os-plus::port-smp-decls ALIAS micro-os-plus-iii-port-smp-decls-interface)
 
 # -----------------------------------------------------------------------------
-# The test scaffolding both silicon ports need and neither owns.
+# Optional: the test scaffolding both silicon ports need and neither owns.
 # Opt-in, exactly like port-smp-decls above, and for the same reason: these
 # were duplicate copies in two repositories, which is what
 # tools/verify-no-duplicate-sources.py exists to catch.
 #
-#   tests/smp-support/include/hw_result.hpp     how a test ends a run with a
-#                                               verdict
-#   tests/smp-support/src/board-contract.cpp    no code; turns "this board
-#                                               never said X" into a compile
-#                                               error rather than a silent
-#                                               inheritance of the previous
-#                                               board's answer
-#
-# Both come through this target: the header on the include path, the contract
-# as an INTERFACE source, so every consumer compiles it exactly as it did when
-# the file lived in its own port.
+#   test_smpl/include/hw_result.hpp      how a test ends a run with a verdict
+#   test_smpl/src/board-contract.cpp     no code; turns "this board never said
+#                                        X" into a compile error rather than a
+#                                        silent inheritance of the previous
+#                                        board's answer
+#
+# Only the HEADER comes through this target. board-contract.cpp is named
+# directly by each port, in the position its own copy used to occupy in the
+# source list -- an INTERFACE source would be appended somewhere else and
+# would change link order, and the point of this move is that it changes
+# nothing: all 24 images per board stay byte-identical across it.
 #
 # The POSIX host port uses neither: no semihosting, and no board contract of
 # this shape.
 add_library (micro-os-plus-iii-test-support-interface INTERFACE)
-target_include_directories (
-  micro-os-plus-iii-test-support-interface INTERFACE
-  "${CMAKE_CURRENT_SOURCE_DIR}/tests/smp-support/include"
-)
-target_sources (micro-os-plus-iii-test-support-interface INTERFACE
-  "${CMAKE_CURRENT_SOURCE_DIR}/tests/smp-support/src/board-contract.cpp")
+if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/smp-support/include")
+  target_include_directories (
+    micro-os-plus-iii-test-support-interface INTERFACE
+    "${CMAKE_CURRENT_SOURCE_DIR}/tests/smp-support/include"
+  )
+else ()
+  target_include_directories (
+    micro-os-plus-iii-test-support-interface INTERFACE
+    "${CMAKE_CURRENT_SOURCE_DIR}/test_smpl/include"
+  )
+endif ()
+if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/smp-support/src/board-contract.cpp")
+  target_sources (micro-os-plus-iii-test-support-interface INTERFACE
+    "${CMAKE_CURRENT_SOURCE_DIR}/tests/smp-support/src/board-contract.cpp")
+else ()
+  target_sources (micro-os-plus-iii-test-support-interface INTERFACE
+    "${CMAKE_CURRENT_SOURCE_DIR}/test_smpl/src/board-contract.cpp")
+endif ()
 add_library (micro-os-plus::test-support ALIAS micro-os-plus-iii-test-support-interface)
 
 # -----------------------------------------------------------------------------
```

### `cmake/uos-app.cmake`

```diff
--- A/cmake/uos-app.cmake
+++ B/cmake/uos-app.cmake
@@ -24,10 +24,8 @@
   set (_many SOURCES INCLUDES DEFINES OPTIONS LIBRARIES)
   cmake_parse_arguments (A "${_opts}" "${_one}" "${_many}" ${ARGN})
 
-  # An application whose sources all come from its libraries (an upstream
-  # suite, test::<suite>) names no SOURCES of its own.
-  if (NOT A_SOURCES AND NOT A_LIBRARIES)
-    message (FATAL_ERROR "uos_add_app(${_name}): SOURCES or LIBRARIES is required")
+  if (NOT A_SOURCES)
+    message (FATAL_ERROR "uos_add_app(${_name}): SOURCES is required")
   endif ()
 
   add_executable (${_name} ${A_SOURCES})
```

### `package.json`

```diff
--- A/package.json
+++ B/package.json
@@ -1,6 +1,6 @@
 {
   "name": "@micro-os-plus/micro-os-plus-iii",
-  "version": "7.2.0-pre.1",
+  "version": "7.1.0",
   "description": "A source code library with the portable part of µOS++ IIIe",
   "keywords": [
     "xpack",
```

### `.gitignore`

```diff
--- A/.gitignore
+++ B/.gitignore
@@ -58,13 +58,3 @@
 *.swp
 *.swo
 *~
-
-# kbd_forward, built by tests/platforms/aarch32-luckfox-lyra/tests/smp_test_int*/host/Makefile
-tests/platforms/aarch32-luckfox-lyra/tests/*/host/kbd_forward
-
-# Python byte-code caches (the host tools run from their own folders) and
-# editor/backup copies.
-__pycache__/
-*.pyc
-*.bak
-tests/platforms/cortexm-*/tests/*/host/kbd_forward
```

## 3. Test sources (`tests/sources/`)

All four changes are in B, for RISC-V and 64-bit targets.

- **`fp-switch/src/main.cpp`**: adds a RISC-V FPU path:
  - the register test on f0–f31 (`fld`/`fsd` with the D extension, `flw`/`fsw` otherwise) and the `fcsr.fflags` flags;
  - the spin length is calibrated at start-up, so one hold lasts about 3 ticks whatever the core's speed;
  - on SMP, the test fails if no round moved to another core.
- **`instrumentation/src/main.cpp`**: the message-queue slot size becomes `max(sizeof(int), sizeof(void*))`. The queue keeps its free list in the free slots, so `sizeof(int)` broke on RV64.
- **`mutex-stress/include/cmsis-plus/os-app-config.h`**: the main stack is 8000 bytes on RV64 instead of 4000.
- **`rtos-apis/src/test-posix-io-api.cpp`**: opens `/dev/mb-p2` instead of `/dev/mb2`. The old name matched no registered device.

### `tests/sources/fp-switch/src/main.cpp`

```diff
--- A/tests/sources/fp-switch/src/main.cpp
+++ B/tests/sources/fp-switch/src/main.cpp
@@ -19,6 +19,9 @@
 //
 // The registers are loaded and checked in inline assembly, so the result does
 // not depend on how the compiler happens to allocate them.
+//
+// RISC-V with an FPU (__riscv_flen): the same test on f0-f31 (FLEN bits each:
+// fld/fsd with the D extension) and the fflags exception flags of fcsr.
 
 #include <cmsis-plus/rtos/os.h>
 
@@ -28,7 +31,7 @@
 using namespace os;
 using namespace os::rtos;
 
-#if defined(__ARM_FP)
+#if defined(__ARM_FP) || defined(__riscv_flen)
 
 #if defined(OS_USE_SMP_SCHEDULER)
 extern "C" unsigned port_cpu_id (void);
@@ -39,8 +42,32 @@
   constexpr unsigned threads_count = 6;
   constexpr unsigned run_ticks = 3000;
   constexpr unsigned spin_loops = 40000;
+#if defined(__riscv_flen)
+  // RISC-V: the spin is calibrated at start-up (os_main) so that one hold
+  // lasts about hold_ticks ticks whatever the core's speed. The fixed 40000
+  // loops are a tick on the MCUs this test was written for, but a 1.5 GHz
+  // U74 ran them in about 0.03 ms: a hold was then almost never preempted,
+  // and no thread ever resumed a hold on another core (0 migrated).
+  constexpr unsigned hold_ticks = 3;
+  unsigned long spin_n = spin_loops;
+#endif
+#if defined(__ARM_FP)
+  // One FPU register (s0-s31).
+  using fp_reg_t = uint32_t;
   // FPSCR N, Z, C, V: flags only, the rounding and exception bits are left.
-  constexpr uint32_t fpscr_flags_mask = 0xF0000000u;
+  constexpr uint32_t fp_flags_mask = 0xF0000000u;
+  constexpr unsigned fp_flags_shift = 28;
+#else
+  // One FPU register (f0-f31), FLEN bits.
+#if __riscv_flen == 64
+  using fp_reg_t = uint64_t;
+#else
+  using fp_reg_t = uint32_t;
+#endif
+  // fcsr.fflags NV, DZ, OF, UF, NX: flags only, the rounding mode is left.
+  constexpr uint32_t fp_flags_mask = 0x1Fu;
+  constexpr unsigned fp_flags_shift = 0;
+#endif
 
   volatile bool stop;
 
@@ -57,8 +84,9 @@
   // Load in[] into s0-s31 and flags into the FPSCR, spin, store the
   // registers to out[] and return the FPSCR flags read back. The caller's
   // FPSCR is restored.
+#if defined(__ARM_FP)
   uint32_t
-  hold_fp_registers (const uint32_t* in, uint32_t* out, uint32_t flags)
+  hold_fp_registers (const fp_reg_t* in, fp_reg_t* out, uint32_t flags)
   {
     uint32_t saved;
     uint32_t read;
@@ -79,29 +107,94 @@
         " vmsr fpscr, %[saved]                \n"
         : [saved] "=&r"(saved), [read] "=&r"(read), [n] "+r"(n),
           [in] "+r"(in), [out] "+r"(out)
-        : [flags] "r"(flags), [mask] "r"(fpscr_flags_mask)
+        : [flags] "r"(flags), [mask] "r"(fp_flags_mask)
         : "memory", "cc", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
           "s8", "s9", "s10", "s11", "s12", "s13", "s14", "s15", "s16", "s17",
           "s18", "s19", "s20", "s21", "s22", "s23", "s24", "s25", "s26",
           "s27", "s28", "s29", "s30", "s31");
-    return read & fpscr_flags_mask;
+    return read & fp_flags_mask;
+  }
+#else
+#if __riscv_flen == 64
+#define FP_SWITCH_L "fld"
+#define FP_SWITCH_S "fsd"
+#else
+#define FP_SWITCH_L "flw"
+#define FP_SWITCH_S "fsw"
+#endif
+#define FP_SWITCH_ONE(op, r, i, p) " " op " " #r ", " #i "*%c[sz](%[" p "]) \n"
+#define FP_SWITCH_ALL(op, p)                                                  \
+  FP_SWITCH_ONE (op, f0, 0, p) FP_SWITCH_ONE (op, f1, 1, p)                   \
+  FP_SWITCH_ONE (op, f2, 2, p) FP_SWITCH_ONE (op, f3, 3, p)                   \
+  FP_SWITCH_ONE (op, f4, 4, p) FP_SWITCH_ONE (op, f5, 5, p)                   \
+  FP_SWITCH_ONE (op, f6, 6, p) FP_SWITCH_ONE (op, f7, 7, p)                   \
+  FP_SWITCH_ONE (op, f8, 8, p) FP_SWITCH_ONE (op, f9, 9, p)                   \
+  FP_SWITCH_ONE (op, f10, 10, p) FP_SWITCH_ONE (op, f11, 11, p)               \
+  FP_SWITCH_ONE (op, f12, 12, p) FP_SWITCH_ONE (op, f13, 13, p)               \
+  FP_SWITCH_ONE (op, f14, 14, p) FP_SWITCH_ONE (op, f15, 15, p)               \
+  FP_SWITCH_ONE (op, f16, 16, p) FP_SWITCH_ONE (op, f17, 17, p)               \
+  FP_SWITCH_ONE (op, f18, 18, p) FP_SWITCH_ONE (op, f19, 19, p)               \
+  FP_SWITCH_ONE (op, f20, 20, p) FP_SWITCH_ONE (op, f21, 21, p)               \
+  FP_SWITCH_ONE (op, f22, 22, p) FP_SWITCH_ONE (op, f23, 23, p)               \
+  FP_SWITCH_ONE (op, f24, 24, p) FP_SWITCH_ONE (op, f25, 25, p)               \
+  FP_SWITCH_ONE (op, f26, 26, p) FP_SWITCH_ONE (op, f27, 27, p)               \
+  FP_SWITCH_ONE (op, f28, 28, p) FP_SWITCH_ONE (op, f29, 29, p)               \
+  FP_SWITCH_ONE (op, f30, 30, p) FP_SWITCH_ONE (op, f31, 31, p)
+
+  // Load in[] into f0-f31 and flags into fcsr.fflags, spin, store the
+  // registers to out[] and return the fflags read back. The caller's fcsr
+  // is restored. One asm statement, so the compiler cannot touch an FP
+  // register in between.
+  uint32_t
+  hold_fp_registers (const fp_reg_t* in, fp_reg_t* out, uint32_t flags)
+  {
+    unsigned long saved;
+    unsigned long read;
+    unsigned long n = spin_n;
+    __asm__ volatile (
+        " frcsr %[saved]                      \n"
+        FP_SWITCH_ALL (FP_SWITCH_L, "in")
+        " fsflags %[flags]                    \n"
+        "1:                                   \n"
+        " addi %[n], %[n], -1                 \n"
+        " bnez %[n], 1b                       \n"
+        " frflags %[read]                     \n"
+        FP_SWITCH_ALL (FP_SWITCH_S, "out")
+        " fscsr %[saved]                      \n"
+        : [saved] "=&r"(saved), [read] "=&r"(read), [n] "+r"(n)
+        : [in] "r"(in), [out] "r"(out),
+          [flags] "r"(static_cast<unsigned long> (flags)),
+          [sz] "i"(sizeof (fp_reg_t))
+        : "memory", "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8",
+          "f9", "f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f18",
+          "f19", "f20", "f21", "f22", "f23", "f24", "f25", "f26", "f27",
+          "f28", "f29", "f30", "f31");
+    return static_cast<uint32_t> (read) & fp_flags_mask;
   }
+#endif
 
   void*
   worker (void* arg)
   {
     context* ctx = static_cast<context*> (arg);
-    uint32_t in[32];
-    uint32_t out[32];
+    fp_reg_t in[32];
+    fp_reg_t out[32];
 
     while (!stop)
       {
         for (unsigned k = 0; k < 32; ++k)
           {
-            in[k] = 0x40000000u | (ctx->id << 16) | ((ctx->rounds & 0xFFu) << 8)
-                    | k;
+            const uint32_t v = 0x40000000u | (ctx->id << 16)
+                               | ((ctx->rounds & 0xFFu) << 8) | k;
+            in[k] = static_cast<fp_reg_t> (v);
+            if constexpr (sizeof (fp_reg_t) == 8)
+              {
+                // Both halves: a 64-bit register must come back whole.
+                in[k] = (static_cast<fp_reg_t> (v) << 32) | (v ^ 0xA5A5A5A5u);
+              }
           }
-        uint32_t flags = ((ctx->id + ctx->rounds) & 0xFu) << 28;
+        uint32_t flags = ((ctx->id + ctx->rounds) << fp_flags_shift)
+                         & fp_flags_mask;
 
 #if defined(OS_USE_SMP_SCHEDULER)
         unsigned cpu_before = port_cpu_id ();
@@ -149,6 +242,38 @@
   // `stop` and the test never ended. Keep the controller above the workers.
   this_thread::thread ().priority (thread::priority::high);
 
+#if defined(__riscv_flen)
+  {
+    // Calibrate: time holds of a growing spin until one takes at least
+    // 8 ticks, then scale to hold_ticks.
+    fp_reg_t in[32] = {};
+    fp_reg_t out[32];
+    unsigned long n = spin_loops;
+    for (;;)
+      {
+        spin_n = n;
+        sysclock.sleep_for (1); // start on a tick boundary
+        const auto t0 = sysclock.now ();
+        hold_fp_registers (in, out, 0);
+        const auto dt = sysclock.now () - t0;
+        // The cap keeps n * 4 (and n * hold_ticks) inside unsigned long:
+        // 1ul << 40 was undefined on RV32, where unsigned long is 32 bits.
+        if (dt >= 8 || n > (~0ul / 16u))
+          {
+            spin_n = (n * hold_ticks) / (dt != 0 ? dt : 1);
+            if (spin_n == 0)
+              {
+                spin_n = 1;
+              }
+            break;
+          }
+        n *= 4;
+      }
+    std::printf ("\nhold: %lu spin loops, about %u ticks\n", spin_n,
+                 hold_ticks);
+  }
+#endif
+
   std::printf ("\nFPU context switch test, %u threads, %u ticks\n",
                threads_count, run_ticks);
 
@@ -183,6 +308,15 @@
 
   std::printf ("total: %u rounds, %u bad, %u migrated\n", rounds, bad,
                migrated);
+#if defined(__riscv_flen) && defined(OS_USE_SMP_SCHEDULER)
+  // With several cores, a hold must also have survived a move to another
+  // core: without one, the cross-core FPU save/restore was not tested.
+  if (migrated == 0)
+    {
+      std::printf ("no round migrated to another core\n");
+      return 1;
+    }
+#endif
   return (bad == 0 && rounds > 0) ? 0 : 1;
 }
 
@@ -196,4 +330,4 @@
   return 0;
 }
 
-#endif // defined(__ARM_FP)
+#endif // defined(__ARM_FP) || defined(__riscv_flen)
```

### `tests/sources/instrumentation/src/main.cpp`

```diff
--- A/tests/sources/instrumentation/src/main.cpp
+++ B/tests/sources/instrumentation/src/main.cpp
@@ -70,7 +70,13 @@
   sysclock.sleep_for (1);
 
   {
-    message_queue* mqueue = new message_queue ("my-mqueue", 7, sizeof (int));
+    // A message slot must hold a pointer: the queue keeps its free list in
+    // the free slots (os-mqueue.cpp asserts msg_size_bytes >= sizeof
+    // (void*)). sizeof (int) satisfied that only on 32-bit targets; the
+    // RISC-V RV64 port was the first 64-bit run of this suite. The messages
+    // sent are still ints.
+    message_queue* mqueue = new message_queue (
+        "my-mqueue", 7, sizeof (int) > sizeof (void*) ? sizeof (int) : sizeof (void*));
 
     int value = 1234;
     mqueue->send (&value, sizeof (value));
```

### `tests/sources/mutex-stress/include/cmsis-plus/os-app-config.h`

```diff
--- A/tests/sources/mutex-stress/include/cmsis-plus/os-app-config.h
+++ B/tests/sources/mutex-stress/include/cmsis-plus/os-app-config.h
@@ -27,7 +27,15 @@
 #define OS_INTEGER_RTOS_CRITICAL_SECTION_INTERRUPT_PRIORITY (4)
 #endif // defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
 
+#if defined(__riscv) && (__riscv_xlen == 64)
+// RV64: every register is 8 bytes and a trap saves a 528-byte frame on the
+// interrupted stack (integer + FP files). The RISC-V port defines
+// __ARM_EABI__ only to select the kernel's embedded code paths, so it lands
+// in this branch; twice the 32-bit size.
+#define OS_INTEGER_RTOS_MAIN_STACK_SIZE_BYTES (8000)
+#else
 #define OS_INTEGER_RTOS_MAIN_STACK_SIZE_BYTES (4000)
+#endif
 
 // ----------------------------------------------------------------------------
 
```

### `tests/sources/rtos-apis/src/test-posix-io-api.cpp`

```diff
--- A/tests/sources/rtos-apis/src/test-posix-io-api.cpp
+++ B/tests/sources/rtos-apis/src/test-posix-io-api.cpp
@@ -322,7 +322,7 @@
 
 static rtos::mutex mx2{ "mx2" };
 
-// /dev/mb2
+// /dev/mb-p2
 // The mutex is not really needed, but it is used to test the template.
 static my_partition2 p2{ "mb-p2", mb, mx2 };
 
@@ -456,7 +456,11 @@
 
   printf ("\n%s - Block device - C API\n", test_name);
   {
-    int fd = open ("/dev/mb2", 0);
+    // The partition registered above as "mb-p2". This block compiles only
+    // on a cross build WITHOUT semihosting syscalls -- the RISC-V platform
+    // was the first, every Arm platform defines OS_USE_SEMIHOSTING_SYSCALLS
+    // -- and its "/dev/mb2" named no device (open() failed with EBADF).
+    int fd = open ("/dev/mb-p2", 0);
 
     buff[0] = 0xFF;
     res = read (fd, buff, bsz);
```

## 4. Test build plumbing (`tests/cmake/`, `tests/platforms/*`)

The two trees find the ports in different ways:

- **`tests/cmake/tests-main.cmake`**:
  - A finds the ports through the xpm links under `build/<config>/xpacks/@micro-os-plus/` (made by `xpm run link-deps`) and sets `UOS_<PORT>_DIR` from them. It then adds the kernel one level up.
  - B points at sibling folders (`../../micro-os-plus-iii-{aarch32,aarch64,posix-arch,cortexm,riscv}`, with a `.git` suffix as fallback) and adds the right port from the platform name. It has a `^riscv` case, and adds the plain kernel only as a fallback.
- **Each platform's `CMakeLists.txt` and `cmake/*.cmake`**: these differ in the same way.
  - A takes QEMU from the build folder's `xpacks/.bin` and lists the port's xpacks folder in its dependencies.
  - B picks the newest `~/.local/xPacks/@xpack-dev-tools/qemu-arm/*` and sets `BOARD` by hand in `definitions.cmake`.

The full diff of `tests-main.cmake` follows, then `qemu-cortex-m3` as an example of the platform changes.

### `tests/cmake/tests-main.cmake`

```diff
--- A/tests/cmake/tests-main.cmake
+++ B/tests/cmake/tests-main.cmake
@@ -46,26 +46,6 @@
 # Set `xpack_dependencies_folders` with the platform specific dependencies.
 include ("platforms/${PLATFORM_NAME}/cmake/dependencies-folders.cmake")
 
-# UNTIL THE BOARD FACTS MOVE INTO THE PLATFORMS: the platforms of the ports
-# read UOS_<PORT>_DIR, the port's real folder behind its link in
-# build/<config>/xpacks/@micro-os-plus/ (`xpm run link-deps --config <config>`).
-# The real path, so that the board scripts find the kernel next to the port.
-foreach (_dep IN LISTS xpack_dependencies_folders)
-  string (REGEX MATCH "/xpacks/@micro-os-plus/(micro-os-plus-iii-[a-z0-9-]+)$" _m "${_dep}")
-  if (_m)
-    set (_port "${CMAKE_MATCH_1}")
-    if (NOT EXISTS "${_dep}/CMakeLists.txt")
-      message (FATAL_ERROR "${_port} is not linked into this build folder.\n"
-        "Run: xpm run link-deps --config <configuration>")
-    endif ()
-    string (REPLACE "micro-os-plus-iii-" "" _var "${_port}")
-    string (TOUPPER "${_var}" _var)
-    string (REPLACE "-" "_" _var "${_var}")
-    get_filename_component (UOS_${_var}_DIR "${_dep}" REALPATH)
-    message (STATUS "µOS++ III ${_port}: ${UOS_${_var}_DIR}")
-  endif ()
-endforeach ()
-
 # Iterate the platform dependencies and `add_subdirectory()`.
 xpack_add_dependencies_subdirectories (
   "${xpack_dependencies_folders}" "xpacks-bin"
@@ -73,9 +53,83 @@
 
 # -----------------------------------------------------------------------------
 
-# Add the project library, defined one level above.
-message (VERBOSE "Adding top library...")
-add_subdirectory (".." "top-bin")
+# The library under test is the architecture port, which itself adds the SMP
+# kernel (micro-os-plus::iii) and the devices package. Do NOT add the kernel
+# separately: both the kernel and the port define micro-os-plus::iii, and
+# adding it twice fails on the duplicate alias. The port is chosen from the
+# platform name.
+message (VERBOSE "Selecting the library under test for ${PLATFORM_NAME}...")
+get_filename_component (_uos_siblings "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
+
+set (UOS_SMP_DIR "${CMAKE_SOURCE_DIR}/.."
+     CACHE PATH "µOS++ III SMP kernel working copy" FORCE)
+set (UOS_AARCH32_DIR "${_uos_siblings}/micro-os-plus-iii-aarch32"
+     CACHE PATH "µOS++ III AArch32 port working copy")
+set (UOS_AARCH64_DIR "${_uos_siblings}/micro-os-plus-iii-aarch64"
+     CACHE PATH "µOS++ III AArch64 port working copy")
+set (UOS_POSIX_ARCH_DIR "${_uos_siblings}/micro-os-plus-iii-posix-arch"
+     CACHE PATH "µOS++ III POSIX-arch port working copy")
+set (UOS_CORTEXM_DIR "${_uos_siblings}/micro-os-plus-iii-cortexm"
+     CACHE PATH "µOS++ III Cortex-M port working copy")
+set (UOS_RISCV_DIR "${_uos_siblings}/micro-os-plus-iii-riscv"
+     CACHE PATH "µOS++ III RISC-V port working copy")
+
+foreach (_dep_var IN ITEMS UOS_AARCH32_DIR UOS_AARCH64_DIR UOS_POSIX_ARCH_DIR UOS_CORTEXM_DIR
+                          UOS_RISCV_DIR)
+  if (NOT EXISTS "${${_dep_var}}/CMakeLists.txt"
+      AND EXISTS "${${_dep_var}}.git/CMakeLists.txt")
+    set (${_dep_var} "${${_dep_var}}.git" CACHE PATH
+         "µOS++ III sibling working copy" FORCE)
+  endif ()
+endforeach ()
+
+if (PLATFORM_NAME MATCHES "^aarch32")
+  message (VERBOSE "Adding the AArch32 port (brings iii + devices)...")
+  if (NOT EXISTS "${UOS_AARCH32_DIR}/CMakeLists.txt")
+    message (FATAL_ERROR "Cannot find the AArch32 port at ${UOS_AARCH32_DIR}")
+  endif ()
+  add_subdirectory ("${UOS_AARCH32_DIR}" "port-bin")
+elseif (PLATFORM_NAME MATCHES "^aarch64")
+  message (VERBOSE "Adding the AArch64 port (brings iii + devices)...")
+  if (NOT EXISTS "${UOS_AARCH64_DIR}/CMakeLists.txt")
+    message (FATAL_ERROR "Cannot find the AArch64 port at ${UOS_AARCH64_DIR}")
+  endif ()
+  add_subdirectory ("${UOS_AARCH64_DIR}" "port-bin")
+elseif (PLATFORM_NAME MATCHES "^native")
+  # The POSIX-arch port: the same kernel and devices, on the host compiler.
+  message (VERBOSE "Adding the POSIX-arch port (brings iii + devices)...")
+  if (NOT EXISTS "${UOS_POSIX_ARCH_DIR}/CMakeLists.txt")
+    message (FATAL_ERROR "Cannot find the POSIX-arch port at ${UOS_POSIX_ARCH_DIR}")
+  endif ()
+  add_subdirectory ("${UOS_POSIX_ARCH_DIR}" "port-bin")
+elseif (PLATFORM_NAME MATCHES "^cortexm" OR PLATFORM_NAME MATCHES "^qemu-cortex")
+  # The Cortex-M port (RP2040/RP2350, STM32F4...) and the generic QEMU
+  # Cortex-M machines (qemu-cortex-m0/m3/m4f/m7f): brings iii + devices.
+  message (VERBOSE "Adding the Cortex-M port (brings iii + devices)...")
+  if (NOT EXISTS "${UOS_CORTEXM_DIR}/CMakeLists.txt")
+    message (FATAL_ERROR "Cannot find the Cortex-M port at ${UOS_CORTEXM_DIR}")
+  endif ()
+  add_subdirectory ("${UOS_CORTEXM_DIR}" "port-bin")
+elseif (PLATFORM_NAME MATCHES "^pico2" OR PLATFORM_NAME MATCHES "^2xcortex")
+  # The pico2's Cortex-M33 run emulated (pico2-1cpu, 2xcortex-m33). Same port,
+  # but the platform links its GENERIC M33 core; no board sources are used.
+  message (VERBOSE "Adding the Cortex-M port for the emulated pico2...")
+  if (NOT EXISTS "${UOS_CORTEXM_DIR}/CMakeLists.txt")
+    message (FATAL_ERROR "Cannot find the Cortex-M port at ${UOS_CORTEXM_DIR}")
+  endif ()
+  add_subdirectory ("${UOS_CORTEXM_DIR}" "port-bin")
+elseif (PLATFORM_NAME MATCHES "^riscv")
+  # The RISC-V port (Milk-V Duo, RV64GC, M-mode): brings iii.
+  message (VERBOSE "Adding the RISC-V port (brings iii)...")
+  if (NOT EXISTS "${UOS_RISCV_DIR}/CMakeLists.txt")
+    message (FATAL_ERROR "Cannot find the RISC-V port at ${UOS_RISCV_DIR}")
+  endif ()
+  add_subdirectory ("${UOS_RISCV_DIR}" "port-bin")
+else ()
+  # Fallback: the plain kernel, one level above (upstream behaviour).
+  message (VERBOSE "Adding the top library...")
+  add_subdirectory (".." "top-bin")
+endif ()
 
 # -----------------------------------------------------------------------------
 # Platform specifics.
```

### `tests/platforms/qemu-cortex-m3/CMakeLists.txt`

```diff
--- A/tests/platforms/qemu-cortex-m3/CMakeLists.txt
+++ B/tests/platforms/qemu-cortex-m3/CMakeLists.txt
@@ -11,13 +11,14 @@
 # -----------------------------------------------------------------------------
 include ("cmake/platform-library.cmake")
 
-# The QEMU binary, from the configuration's xpm .bin (the per-config pin).
-unset (_qemu CACHE)
-find_program (
-  _qemu qemu-system-arm
-  HINTS "${CMAKE_BINARY_DIR}/xpacks/.bin"
-)
-if (NOT _qemu)
+# The QEMU binary, from the newest installed qemu-arm xpack.
+file (GLOB _qemu_bins
+      "$ENV{HOME}/.local/xPacks/@xpack-dev-tools/qemu-arm/*/.content/bin")
+if (_qemu_bins)
+  list (SORT _qemu_bins COMPARE NATURAL ORDER DESCENDING)
+  list (GET _qemu_bins 0 _qemu_bin)
+  set (_qemu "${_qemu_bin}/qemu-system-arm")
+else ()
   set (_qemu "qemu-system-arm")
 endif ()
 
```

### `tests/platforms/qemu-cortex-m3/cmake/definitions.cmake`

```diff
--- A/tests/platforms/qemu-cortex-m3/cmake/definitions.cmake
+++ B/tests/platforms/qemu-cortex-m3/cmake/definitions.cmake
@@ -12,3 +12,6 @@
 set (xpack_device_compile_definition "MICRO_OS_PLUS_DEVICE_QEMU_CORTEX_M3")
 set (xpack_device_linker_script_file_name "mem-mps2-an385.ld")
 
+# The port's board this platform derives from. The generic image does not link
+# the board, but the Cortex-M port reads BOARD to pick its board.cmake.
+set (BOARD "nucleof411" CACHE STRING "Target board (the port's board id)" FORCE)
```

### `tests/platforms/qemu-cortex-m3/cmake/dependencies-folders.cmake`

```diff
--- A/tests/platforms/qemu-cortex-m3/cmake/dependencies-folders.cmake
+++ B/tests/platforms/qemu-cortex-m3/cmake/dependencies-folders.cmake
@@ -10,7 +10,6 @@
   "${CMAKE_SOURCE_DIR}/sources/rtos-apis"
   "${CMAKE_SOURCE_DIR}/sources/mutex-stress"
   "${CMAKE_SOURCE_DIR}/sources/cmsis-os-validator"
-  "${CMAKE_BINARY_DIR}/xpacks/@micro-os-plus/micro-os-plus-iii-cortexm"
   # The generic QEMU Cortex-M device (vectors, CMSIS).
   "${CMAKE_SOURCE_DIR}/device-qemu-cortexm"
   # The SOURCE_DIR is the `tests` folder.
```

### Every changed platform file

Number of changed lines (`<` plus `>`) in each file:

| File | Lines changed |
|---|---:|
| `tests/platforms/2xcortex-m33/cmake/definitions.cmake` | 3 |
| `tests/platforms/2xcortex-m33/cmake/dependencies-folders.cmake` | 1 |
| `tests/platforms/2xcortex-m33/CMakeLists.txt` | 13 |
| `tests/platforms/aarch32-luckfox-lyra/cmake/definitions.cmake` | 14 |
| `tests/platforms/aarch32-luckfox-lyra/cmake/dependencies-folders.cmake` | 8 |
| `tests/platforms/aarch32-luckfox-lyra/cmake/platform-library.cmake` | 53 |
| `tests/platforms/aarch32-luckfox-lyra/CMakeLists.txt` | 152 |
| `tests/platforms/aarch32-luckfox-lyra/src/platform-support.cpp` | 2 |
| `tests/platforms/aarch32-rpi3b/cmake/definitions.cmake` | 7 |
| `tests/platforms/aarch32-rpi3b/cmake/dependencies-folders.cmake` | 4 |
| `tests/platforms/aarch32-rpi3b/cmake/platform-library.cmake` | 62 |
| `tests/platforms/aarch32-rpi3b/CMakeLists.txt` | 152 |
| `tests/platforms/aarch32-rpi3b/src/platform-support.cpp` | 2 |
| `tests/platforms/aarch32-rpi-zero-2w/cmake/dependencies-folders.cmake` | 4 |
| `tests/platforms/aarch32-rpi-zero-2w/cmake/platform-library.cmake` | 55 |
| `tests/platforms/aarch32-rpi-zero-2w/CMakeLists.txt` | 122 |
| `tests/platforms/aarch64-rpi3b/cmake/definitions.cmake` | 7 |
| `tests/platforms/aarch64-rpi3b/cmake/dependencies-folders.cmake` | 4 |
| `tests/platforms/aarch64-rpi3b/cmake/platform-library.cmake` | 51 |
| `tests/platforms/aarch64-rpi3b/CMakeLists.txt` | 128 |
| `tests/platforms/aarch64-rpi4b/cmake/definitions.cmake` | 7 |
| `tests/platforms/aarch64-rpi4b/cmake/dependencies-folders.cmake` | 4 |
| `tests/platforms/aarch64-rpi4b/cmake/platform-library.cmake` | 51 |
| `tests/platforms/aarch64-rpi4b/CMakeLists.txt` | 139 |
| `tests/platforms/aarch64-rpi-zero-2w/cmake/dependencies-folders.cmake` | 4 |
| `tests/platforms/aarch64-rpi-zero-2w/cmake/platform-library.cmake` | 51 |
| `tests/platforms/aarch64-rpi-zero-2w/CMakeLists.txt` | 100 |
| `tests/platforms/cortexm-nucleof411/cmake/definitions.cmake` | 5 |
| `tests/platforms/cortexm-nucleof411/cmake/dependencies-folders.cmake` | 5 |
| `tests/platforms/cortexm-nucleof411/cmake/platform-library.cmake` | 15 |
| `tests/platforms/cortexm-nucleof411/CMakeLists.txt` | 66 |
| `tests/platforms/cortexm-pico/cmake/definitions.cmake` | 3 |
| `tests/platforms/cortexm-pico/cmake/dependencies-folders.cmake` | 5 |
| `tests/platforms/cortexm-pico/cmake/platform-library.cmake` | 15 |
| `tests/platforms/cortexm-pico/CMakeLists.txt` | 116 |
| `tests/platforms/cortexm-pico2/cmake/definitions.cmake` | 3 |
| `tests/platforms/cortexm-pico2/cmake/dependencies-folders.cmake` | 5 |
| `tests/platforms/cortexm-pico2/cmake/platform-library.cmake` | 15 |
| `tests/platforms/cortexm-pico2/CMakeLists.txt` | 110 |
| `tests/platforms/cortexm-pico2-pizero/cmake/definitions.cmake` | 3 |
| `tests/platforms/cortexm-pico2-pizero/cmake/dependencies-folders.cmake` | 3 |
| `tests/platforms/cortexm-pico2-pizero/cmake/platform-library.cmake` | 15 |
| `tests/platforms/cortexm-pico2-pizero/CMakeLists.txt` | 66 |
| `tests/platforms/cortexm-pico2-rp2350b-psram/cmake/definitions.cmake` | 4 |
| `tests/platforms/cortexm-pico2-rp2350b-psram/cmake/dependencies-folders.cmake` | 5 |
| `tests/platforms/cortexm-pico2-rp2350b-psram/cmake/platform-library.cmake` | 15 |
| `tests/platforms/cortexm-pico2-rp2350b-psram/CMakeLists.txt` | 96 |
| `tests/platforms/cortexm-weactf411/cmake/definitions.cmake` | 5 |
| `tests/platforms/cortexm-weactf411/cmake/dependencies-folders.cmake` | 5 |
| `tests/platforms/cortexm-weactf411/cmake/platform-library.cmake` | 15 |
| `tests/platforms/cortexm-weactf411/CMakeLists.txt` | 66 |
| `tests/platforms/cortexm-weactf412/cmake/definitions.cmake` | 5 |
| `tests/platforms/cortexm-weactf412/cmake/dependencies-folders.cmake` | 5 |
| `tests/platforms/cortexm-weactf412/cmake/platform-library.cmake` | 15 |
| `tests/platforms/cortexm-weactf412/CMakeLists.txt` | 66 |
| `tests/platforms/native/cmake/dependencies-folders.cmake` | 3 |
| `tests/platforms/native/cmake/platform-library.cmake` | 47 |
| `tests/platforms/native/CMakeLists.txt` | 69 |
| `tests/platforms/pico2-1cpu/cmake/definitions.cmake` | 3 |
| `tests/platforms/pico2-1cpu/cmake/dependencies-folders.cmake` | 1 |
| `tests/platforms/pico2-1cpu/CMakeLists.txt` | 15 |
| `tests/platforms/qemu-cortex-m0/cmake/definitions.cmake` | 3 |
| `tests/platforms/qemu-cortex-m0/cmake/dependencies-folders.cmake` | 1 |
| `tests/platforms/qemu-cortex-m0/CMakeLists.txt` | 15 |
| `tests/platforms/qemu-cortex-m3/cmake/definitions.cmake` | 3 |
| `tests/platforms/qemu-cortex-m3/cmake/dependencies-folders.cmake` | 1 |
| `tests/platforms/qemu-cortex-m3/CMakeLists.txt` | 15 |
| `tests/platforms/qemu-cortex-m4f/cmake/definitions.cmake` | 3 |
| `tests/platforms/qemu-cortex-m4f/cmake/dependencies-folders.cmake` | 1 |
| `tests/platforms/qemu-cortex-m4f/CMakeLists.txt` | 15 |
| `tests/platforms/qemu-cortex-m7f/cmake/definitions.cmake` | 3 |
| `tests/platforms/qemu-cortex-m7f/cmake/dependencies-folders.cmake` | 1 |
| `tests/platforms/qemu-cortex-m7f/CMakeLists.txt` | 15 |

## 5. Files in one tree only

### Only in B

- `tests/platforms/riscv-ch32v307-evt`
- `tests/platforms/riscv-mangopi-mq-pro`
- `tests/platforms/riscv-milkv-duo`
- `tests/platforms/riscv-pico2`
- `tests/platforms/riscv-visionfive2-lite`
- `test_smpl`

`test_smpl/` holds `include/`, `src/` and the runners `run-qemu.sh`, `run-hw.sh` and `run-host.sh`.

### Only in A

Main items:

- folders under `tests/`: `board-tests/`, `smp-support/`, `scripts/` and `third_party/`;
- the tests `smp_test0`–`smp_test4`, `smp-mat-test`, `smp-mat-sdcard-test`, `smp-num-test`, `smp-pipeline-test`, `smp-pro-cons-test`, `clock-test` and `sd_test`;
- `tests/cmake/uos-{aarch-board,cortexm-board,hwd-reset,tests-run}.cmake` and `uos-hwd.tcl`;
- per-board `src/`, `device/`, `tests/`, `openocd.cfg` and `board.cmake` for the aarch32, luckfox-lyra, pico, weactf411 and weactf412 platforms.

<details>
<summary>Full list (192 entries)</summary>

- `tests/board-tests`
- `tests/cmake/uos-aarch-board.cmake`
- `tests/cmake/uos-cortexm-board.cmake`
- `tests/cmake/uos-hwd-reset.cmake`
- `tests/cmake/uos-hwd.tcl`
- `tests/cmake/uos-tests-run.cmake`
- `tests/.gitignore`
- `tests/platforms/aarch32-luckfox-lyra/cmake/board.cmake`
- `tests/platforms/aarch32-luckfox-lyra/device`
- `tests/platforms/aarch32-luckfox-lyra/include/gic.hpp`
- `tests/platforms/aarch32-luckfox-lyra/include/led.hpp`
- `tests/platforms/aarch32-luckfox-lyra/include/osal.hpp`
- `tests/platforms/aarch32-luckfox-lyra/include/reference-clock.h`
- `tests/platforms/aarch32-luckfox-lyra/include/rk3506.hpp`
- `tests/platforms/aarch32-luckfox-lyra/include/smp.hpp`
- `tests/platforms/aarch32-luckfox-lyra/include/uart.hpp`
- `tests/platforms/aarch32-luckfox-lyra/openocd.cfg`
- `tests/platforms/aarch32-luckfox-lyra/src/mmu.cpp`
- `tests/platforms/aarch32-luckfox-lyra/src/port_sys.cpp`
- `tests/platforms/aarch32-luckfox-lyra/src/rtos`
- `tests/platforms/aarch32-luckfox-lyra/src/smp.cpp`
- `tests/platforms/aarch32-luckfox-lyra/src/startup.S`
- `tests/platforms/aarch32-luckfox-lyra/src/timer_arm.cpp`
- `tests/platforms/aarch32-luckfox-lyra/tests`
- `tests/platforms/aarch32-luckfox-lyra/usb`
- `tests/platforms/aarch32-rpi3b/cmake/board.cmake`
- `tests/platforms/aarch32-rpi3b/device`
- `tests/platforms/aarch32-rpi3b/include/reference-clock.h`
- `tests/platforms/aarch32-rpi3b/src/cmsis-os-validator-support.cpp`
- `tests/platforms/aarch32-rpi3b/tests`
- `tests/platforms/aarch32-rpi-zero-2w/bcm2837-aarch32.cfg`
- `tests/platforms/aarch32-rpi-zero-2w/cmake/board.cmake`
- `tests/platforms/aarch32-rpi-zero-2w/config.txt`
- `tests/platforms/aarch32-rpi-zero-2w/device`
- `tests/platforms/aarch32-rpi-zero-2w/include/led.hpp`
- `tests/platforms/aarch32-rpi-zero-2w/include/reference-clock.h`
- `tests/platforms/aarch32-rpi-zero-2w/include/smp.hpp`
- `tests/platforms/aarch32-rpi-zero-2w/include/uart.hpp`
- `tests/platforms/aarch32-rpi-zero-2w/openocd-cmsisdap-rpi3.cfg`
- `tests/platforms/aarch32-rpi-zero-2w/openocd-jlink-rpi3.cfg`
- `tests/platforms/aarch32-rpi-zero-2w/openocd-olimex.cfg`
- `tests/platforms/aarch32-rpi-zero-2w/openocd-probe.cfg`
- `tests/platforms/aarch32-rpi-zero-2w/qemu-raspi3-shim`
- `tests/platforms/aarch32-rpi-zero-2w/reset.cfg`
- `tests/platforms/aarch32-rpi-zero-2w/rpi3-aarch32.cfg`
- `tests/platforms/aarch32-rpi-zero-2w/run-test.cfg`
- `tests/platforms/aarch32-rpi-zero-2w/src/cmsis-os-validator-support.cpp`
- `tests/platforms/aarch32-rpi-zero-2w/src/mmu.cpp`
- `tests/platforms/aarch32-rpi-zero-2w/src/port_sys.cpp`
- `tests/platforms/aarch32-rpi-zero-2w/src/rtos`
- `tests/platforms/aarch32-rpi-zero-2w/src/smp.cpp`
- `tests/platforms/aarch32-rpi-zero-2w/src/startup.S`
- `tests/platforms/aarch32-rpi-zero-2w/src/timer_arm.cpp`
- `tests/platforms/aarch32-rpi-zero-2w/tests`
- `tests/platforms/aarch32-rpi-zero-2w/wifi`
- `tests/platforms/aarch64-rpi3b/cmake/board.cmake`
- `tests/platforms/aarch64-rpi3b/device`
- `tests/platforms/aarch64-rpi3b/include/reference-clock.h`
- `tests/platforms/aarch64-rpi3b/src/cmsis-os-validator-support.cpp`
- `tests/platforms/aarch64-rpi3b/tests`
- `tests/platforms/aarch64-rpi4b/cmake/board.cmake`
- `tests/platforms/aarch64-rpi4b/config.txt`
- `tests/platforms/aarch64-rpi4b/device`
- `tests/platforms/aarch64-rpi4b/include/board_ram.h`
- `tests/platforms/aarch64-rpi4b/include/led.hpp`
- `tests/platforms/aarch64-rpi4b/include/mmu_hi.hpp`
- `tests/platforms/aarch64-rpi4b/include/reference-clock.h`
- `tests/platforms/aarch64-rpi4b/include/smp.hpp`
- `tests/platforms/aarch64-rpi4b/include/uart.hpp`
- `tests/platforms/aarch64-rpi4b/net`
- `tests/platforms/aarch64-rpi4b/openocd-cmsis-dap-rpi4b.cfg`
- `tests/platforms/aarch64-rpi4b/reset.cfg`
- `tests/platforms/aarch64-rpi4b/run-test.cfg`
- `tests/platforms/aarch64-rpi4b/src/cmsis-os-validator-support.cpp`
- `tests/platforms/aarch64-rpi4b/src/cxa_guard.cpp`
- `tests/platforms/aarch64-rpi4b/src/mmu.cpp`
- `tests/platforms/aarch64-rpi4b/src/port_sys.cpp`
- `tests/platforms/aarch64-rpi4b/src/rtos`
- `tests/platforms/aarch64-rpi4b/src/smp.cpp`
- `tests/platforms/aarch64-rpi4b/src/startup.S`
- `tests/platforms/aarch64-rpi4b/src/timer_arm.cpp`
- `tests/platforms/aarch64-rpi4b/tests`
- `tests/platforms/aarch64-rpi-zero-2w/cmake/board.cmake`
- `tests/platforms/aarch64-rpi-zero-2w/config.txt`
- `tests/platforms/aarch64-rpi-zero-2w/device`
- `tests/platforms/aarch64-rpi-zero-2w/hw-park`
- `tests/platforms/aarch64-rpi-zero-2w/include/board_ram.h`
- `tests/platforms/aarch64-rpi-zero-2w/include/led.hpp`
- `tests/platforms/aarch64-rpi-zero-2w/include/reference-clock.h`
- `tests/platforms/aarch64-rpi-zero-2w/include/smp.hpp`
- `tests/platforms/aarch64-rpi-zero-2w/include/uart.hpp`
- `tests/platforms/aarch64-rpi-zero-2w/openocd-cmsisdap-rpi3.cfg`
- `tests/platforms/aarch64-rpi-zero-2w/openocd-jlink-rpi3.cfg`
- `tests/platforms/aarch64-rpi-zero-2w/openocd-olimex.cfg`
- `tests/platforms/aarch64-rpi-zero-2w/openocd-probe.cfg`
- `tests/platforms/aarch64-rpi-zero-2w/reset.cfg`
- `tests/platforms/aarch64-rpi-zero-2w/rpi3-jlink.cfg`
- `tests/platforms/aarch64-rpi-zero-2w/run-test.cfg`
- `tests/platforms/aarch64-rpi-zero-2w/src/cmsis-os-validator-support.cpp`
- `tests/platforms/aarch64-rpi-zero-2w/src/mmu.cpp`
- `tests/platforms/aarch64-rpi-zero-2w/src/port_sys.cpp`
- `tests/platforms/aarch64-rpi-zero-2w/src/rtos`
- `tests/platforms/aarch64-rpi-zero-2w/src/smp.cpp`
- `tests/platforms/aarch64-rpi-zero-2w/src/startup.S`
- `tests/platforms/aarch64-rpi-zero-2w/src/timer_arm.cpp`
- `tests/platforms/aarch64-rpi-zero-2w/tests`
- `tests/platforms/aarch64-rpi-zero-2w/verify-bootcard.py`
- `tests/platforms/aarch64-rpi-zero-2w/wifi`
- `tests/platforms/cortexm-nucleof411/cmake/board.cmake`
- `tests/platforms/cortexm-nucleof411/device`
- `tests/platforms/cortexm-nucleof411/include/board.hpp`
- `tests/platforms/cortexm-nucleof411/include/cmsis-plus/os-app-config.h`
- `tests/platforms/cortexm-nucleof411/include/led.hpp`
- `tests/platforms/cortexm-nucleof411/include/test-end-console.h`
- `tests/platforms/cortexm-nucleof411/include/uart.hpp`
- `tests/platforms/cortexm-nucleof411/openocd.cfg`
- `tests/platforms/cortexm-nucleof411/src`
- `tests/platforms/cortexm-nucleof411/tests`
- `tests/platforms/cortexm-pico/cmake/board.cmake`
- `tests/platforms/cortexm-pico/device`
- `tests/platforms/cortexm-pico/glue`
- `tests/platforms/cortexm-pico/include/bsp`
- `tests/platforms/cortexm-pico/include/cmsis-plus/os-app-config.h`
- `tests/platforms/cortexm-pico/include/test-end-console.h`
- `tests/platforms/cortexm-pico/openocd.cfg`
- `tests/platforms/cortexm-pico/qemu`
- `tests/platforms/cortexm-pico/src`
- `tests/platforms/cortexm-pico/tests`
- `tests/platforms/cortexm-pico/usb`
- `tests/platforms/cortexm-pico2/cmake/board.cmake`
- `tests/platforms/cortexm-pico2/device`
- `tests/platforms/cortexm-pico2/glue`
- `tests/platforms/cortexm-pico2/include/bsp`
- `tests/platforms/cortexm-pico2/include/cmsis-plus/os-app-config.h`
- `tests/platforms/cortexm-pico2/include/test-end-console.h`
- `tests/platforms/cortexm-pico2/openocd.cfg`
- `tests/platforms/cortexm-pico2/qemu`
- `tests/platforms/cortexm-pico2/src`
- `tests/platforms/cortexm-pico2/tests`
- `tests/platforms/cortexm-pico2/usb`
- `tests/platforms/cortexm-pico2-pizero/cmake/board.cmake`
- `tests/platforms/cortexm-pico2-pizero/openocd.cfg`
- `tests/platforms/cortexm-pico2-pizero/tests`
- `tests/platforms/cortexm-pico2-rp2350b-psram/cmake/board.cmake`
- `tests/platforms/cortexm-pico2-rp2350b-psram/include/test-end-console.h`
- `tests/platforms/cortexm-pico2-rp2350b-psram/openocd.cfg`
- `tests/platforms/cortexm-pico2-rp2350b-psram/tests`
- `tests/platforms/cortexm-weactf411/cmake/board.cmake`
- `tests/platforms/cortexm-weactf411/device`
- `tests/platforms/cortexm-weactf411/include/board.hpp`
- `tests/platforms/cortexm-weactf411/include/cmsis-plus/os-app-config.h`
- `tests/platforms/cortexm-weactf411/include/flash.hpp`
- `tests/platforms/cortexm-weactf411/include/flatfs.hpp`
- `tests/platforms/cortexm-weactf411/include/led.hpp`
- `tests/platforms/cortexm-weactf411/include/spi.hpp`
- `tests/platforms/cortexm-weactf411/include/test-end-console.h`
- `tests/platforms/cortexm-weactf411/include/uart.hpp`
- `tests/platforms/cortexm-weactf411/openocd.cfg`
- `tests/platforms/cortexm-weactf411/src`
- `tests/platforms/cortexm-weactf411/tests`
- `tests/platforms/cortexm-weactf412/cmake/board.cmake`
- `tests/platforms/cortexm-weactf412/device`
- `tests/platforms/cortexm-weactf412/include/board.hpp`
- `tests/platforms/cortexm-weactf412/include/cmsis-plus/os-app-config.h`
- `tests/platforms/cortexm-weactf412/include/led.hpp`
- `tests/platforms/cortexm-weactf412/include/test-end-console.h`
- `tests/platforms/cortexm-weactf412/include/uart.hpp`
- `tests/platforms/cortexm-weactf412/openocd.cfg`
- `tests/platforms/cortexm-weactf412/src`
- `tests/platforms/cortexm-weactf412/tests`
- `tests/platforms/native/include/led.hpp`
- `tests/platforms/native/include/reference-clock.h`
- `tests/platforms/native/include/smp.hpp`
- `tests/platforms/native/include/timer_arm.hpp`
- `tests/platforms/native/include/uart.hpp`
- `tests/platforms/native/src`
- `tests/platforms/native/tests`
- `tests/scripts`
- `tests/smp-support`
- `tests/sources/clock-test`
- `tests/sources/sd_test`
- `tests/sources/smp-mat-sdcard-test`
- `tests/sources/smp-mat-test`
- `tests/sources/smp-num-test`
- `tests/sources/smp-pipeline-test`
- `tests/sources/smp-pro-cons-test`
- `tests/sources/smp_test0`
- `tests/sources/smp_test1`
- `tests/sources/smp_test2`
- `tests/sources/smp_test3`
- `tests/sources/smp_test4`
- `tests/third_party`

</details>

## How this was made

```sh
cd ~/Work
diff -ruN -x .git -x node_modules -x build -x docs -x '*.md' -x '*.pdf' \
  -x package-lock.json -x xpacks -x .vscode \
  micro-os-plus-iii-smp2/micro-os-plus-iii \
  micro-os-plus-iii-smp-riscv/micro-os-plus-iii
```

The full output, including every new file, is about 188k lines. Most of it is the directories that exist in only one tree.
