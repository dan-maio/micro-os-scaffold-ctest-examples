# -----------------------------------------------------------------------------
# micro-os-plus-iii-cortexm -- the Cortex-M port of µOS++ III.
#
# The third architecture project, beside -aarch32 and -aarch64 and built the
# same way. It consumes the kernel and the device drivers; neither of them
# knows this project exists, so the dependency runs one way and the kernel
# tree stays merge-clean against upstream.
#
# It holds no copy of them. Clone the siblings beside this one:
#
#   git clone .../micro-os-plus-iii-smp.git
#   git clone .../micro-os-plus-iii-devices.git
#
# and they are found automatically.
#
# WHAT THIS PORT IS. src/rtos/os-core.cpp is the upstream Cortex-M port,
# carried over unmodified: the PendSV context switch, the SysTick tick, and
# the critical sections. It is single-core, and on the three STM32F4 boards
# here that is what runs -- OS_NCPU=1, no kernel lock, no IPI. The kernel's
# SMP branches compile out; no kernel patch is involved, which is exactly how
# the predecessor's boards were proven.
#
# WHAT SMP WILL LOOK LIKE HERE, so the next reader does not assume otherwise.
# The predecessor also has a dual-core Cortex-M SMP core, in its pico2 tree.
# That core is NOT portable Cortex-M SMP and cannot simply replace this file:
# its kernel lock is the RP2350's SIO hardware spinlock 0, its IPI is the SIO
# inter-core FIFO on IRQ 25, and its CPU id is SIO_CPUID. All three are
# Rockchip-of-the-RP2350 -- silicon, not ISA.
#
# In this layout that code belongs to the BOARD, the way the Lyra's GIC-400
# SGI and the Pi's do: test/boards/<id>/src/rtos/. So cortexm gains SMP by a
# board arriving that supplies a lock and an IPI, not by this file changing.
# That is why the STM32 boards come first -- they are the OS_NCPU=1 witness
# that the split is real.
#
# The port reads no board. A board (its flags, defines, start-up and linker
# scripts) is the consumer's: the test platforms of micro-os-plus-iii build a
# `micro-os-plus::board` target and link it after one of the port cores below.
#
# Exports: micro-os-plus::cortexm, ::cortexm-rp2350, ::cortexm-rp2040 (the
# port cores) and ::cortexm-qemu-<isa> (the generic QEMU cores).
# -----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.20)

project (micro-os-plus-iii-cortexm LANGUAGES C CXX ASM)

set (UOS_CORTEXM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")

# -----------------------------------------------------------------------------
# The kernel (micro-os-plus::iii and its parts) is not added here. Like every
# µOS++ port, this is a leaf library: the consumer (the tests, an application)
# adds the kernel. The targets below link kernel targets by name, which CMake
# resolves when it generates, so the kernel may be added after the port.

# -----------------------------------------------------------------------------
# The SoC targets (micro-os-plus::soc-stm32f411xe, ::soc-stm32f412rx,
# ::soc-rp2350, ::soc-rp2040) are the device packages',
# micro-os-plus-iii-devices-stm32f4 and -rp2xxx.

# -----------------------------------------------------------------------------
# The port cores.
#
# Cortex-M is one ISA family but not one scheduler. Upstream's core
# (micro-os-plus::cortexm) is single-core: PendSV, SysTick, PRIMASK critical
# sections. The RP2350 and RP2040 flavours are that plus an SMP branch gated on
# OS_USE_SMP_SCHEDULER, and the SMP branch is the chip's SIO: hardware
# spinlock 0 for the kernel lock, the inter-core FIFO for the IPI, SIO_CPUID
# for the core index. Each flavour's include-<chip>/ holds only the headers
# that differ and is searched before include/.
#
# They are not merged into one file for a stated reason: the three STM32
# boards are hardware-proven on upstream's core, and moving them onto a
# different scheduler has to be re-proven on the desk, not assumed. A board
# therefore names the core it was proven with (UOS_PORT_LIB).
#
# The kernel is not linked here; the consumer links it, as upstream.
function (uos_add_cortexm_core name core)
  add_library (micro-os-plus-iii-${name}-interface INTERFACE)
  target_include_directories (micro-os-plus-iii-${name}-interface INTERFACE ${ARGN})
  target_sources (
    micro-os-plus-iii-${name}-interface
    INTERFACE
          "${core}"
          # Strong _Exit(): terminate through semihosting SYS_EXIT, so
          # std::exit() ends the run with a real exit code instead of idling.
          # The AArch32 and AArch64 ports carry the same file.
          src/semihosting-exit.cpp
  )
  add_library (micro-os-plus::${name} ALIAS micro-os-plus-iii-${name}-interface)
endfunction ()

uos_add_cortexm_core (cortexm "src/rtos/os-core.cpp" include)
uos_add_cortexm_core (cortexm-rp2350 "src/rtos/os-core-rp2350.cpp" include-rp2350 include)
uos_add_cortexm_core (cortexm-rp2040 "src/rtos/os-core-rp2040.cpp" include-rp2040 include)

# Upstream's name for the base core. The four upstream board platforms
# (tests/platforms/nucleo-*, raspberrypi-pico) still link it by that name;
# the board variants above are local additions and do not replace it.
add_library (micro-os-plus::iii-cortexm ALIAS micro-os-plus-iii-cortexm-interface)

# The GENERIC ports, for the `qemu` image of a board QEMU does not model.
#
# A board like the pico2 overrides the port core (os-core-rp2350.cpp) and the
# include directory (include-rp2350/) with RP2350-specific ones. Its emulated
# image must not: it links the generic core and the generic include, the same
# way the upstream Cortex-M port does, plus whatever device provides the
# vector table (the harness's device-qemu-cortexm). The board's tests are
# unchanged; only what runs under them differs.
#
# There is one generic core per ISA the harness's device-qemu-cortexm can
# model -- M0, M3, M4F and M7. They share the upstream single-core port core
# (src/rtos/os-core.cpp) and the generic include/; they differ only in the
# -mcpu flags. The matching device (its vectors, its CMSIS core and its linker
# script) is chosen by the harness platform. They are single-core by
# construction: the platform sets OS_NCPU=1 and never defines
# OS_USE_SMP_SCHEDULER, so the kernel's SMP branches compile out.
function (uos_add_generic_qemu_core suffix cpu_flags)
  add_library (micro-os-plus-iii-cortexm-qemu-${suffix}-interface INTERFACE
               EXCLUDE_FROM_ALL)
  target_include_directories (
    micro-os-plus-iii-cortexm-qemu-${suffix}-interface INTERFACE "include")
  target_sources (
    micro-os-plus-iii-cortexm-qemu-${suffix}-interface
    INTERFACE "src/rtos/os-core.cpp" src/semihosting-exit.cpp
              src/libc/getentropy.c)
  target_link_libraries (
    micro-os-plus-iii-cortexm-qemu-${suffix}-interface
    INTERFACE micro-os-plus::iii)
  # The console is the semihosting channel; the kernel's trace-semihosting
  # backend only compiles when one of these is defined, otherwise the weak
  # no-op trace::write wins and nothing is printed.
  target_compile_definitions (
    micro-os-plus-iii-cortexm-qemu-${suffix}-interface
    INTERFACE OS_USE_TRACE_SEMIHOSTING_STDOUT)
  target_compile_options (
    micro-os-plus-iii-cortexm-qemu-${suffix}-interface INTERFACE ${cpu_flags})
  target_link_options (
    micro-os-plus-iii-cortexm-qemu-${suffix}-interface
    INTERFACE ${cpu_flags}
              # The kernel's startup provides _start/main; do not also pull
              # newlib's crt0.
              -nostartfiles)
  add_library (micro-os-plus::cortexm-qemu-${suffix}
               ALIAS micro-os-plus-iii-cortexm-qemu-${suffix}-interface)
  message (
    VERBOSE
    "> micro-os-plus::cortexm-qemu-${suffix} -> micro-os-plus-iii-cortexm-qemu-${suffix}-interface"
  )
endfunction ()

uos_add_generic_qemu_core (m0 "-mcpu=cortex-m0;-mthumb;-mfloat-abi=soft")
uos_add_generic_qemu_core (m3 "-mcpu=cortex-m3;-mthumb;-mfloat-abi=soft")
uos_add_generic_qemu_core (m4f "-mcpu=cortex-m4;-mthumb;-mfloat-abi=hard;-mfpu=fpv4-sp-d16")
uos_add_generic_qemu_core (m7 "-mcpu=cortex-m7;-mthumb;-mfloat-abi=hard;-mfpu=fpv5-d16")

# Backward-compatible alias: before the per-ISA cores, the M7 core was the
# only generic one and carried the unsuffixed name.
add_library (micro-os-plus::cortexm-qemu
             ALIAS micro-os-plus-iii-cortexm-qemu-m7-interface)

# The GENERIC Cortex-M33 port, for the emulated pico2 configurations.
#
# Same idea as cortexm-qemu, but built for the Cortex-M33 and against the
# generic M33 core/include (include-m33/), whose SMP branch uses the SSE-200
# CPUID/MHU instead of the RP2350 SIO. The single-core and SMP variants are
# selected by the executable's OS_USE_SMP_SCHEDULER define, so this interface
# deliberately forces neither.
add_library (micro-os-plus-iii-cortexm-qemu-m33-interface INTERFACE
             EXCLUDE_FROM_ALL)
target_include_directories (micro-os-plus-iii-cortexm-qemu-m33-interface
                            INTERFACE "include-m33")
target_sources (
  micro-os-plus-iii-cortexm-qemu-m33-interface
  INTERFACE "src/rtos/os-core-m33.cpp" src/semihosting-exit.cpp
            src/libc/getentropy.c
)
target_link_libraries (micro-os-plus-iii-cortexm-qemu-m33-interface
                       INTERFACE micro-os-plus::iii)
target_compile_definitions (
  micro-os-plus-iii-cortexm-qemu-m33-interface
  INTERFACE OS_USE_TRACE_SEMIHOSTING_STDOUT
)
target_compile_options (
  micro-os-plus-iii-cortexm-qemu-m33-interface
  INTERFACE -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
)
target_link_options (
  micro-os-plus-iii-cortexm-qemu-m33-interface
  INTERFACE -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
            # The kernel's startup provides _start/main; do not also pull
            # newlib's crt0.
            -nostartfiles
            # NOT nano: the harness suites pull wcsftime (via libstdc++), and
            # the v8-m.main nano libc omits its swprintf dependency. The 16M
            # RAM of the an505/an521 map has room for the full libc.
)
add_library (micro-os-plus::cortexm-qemu-m33
             ALIAS micro-os-plus-iii-cortexm-qemu-m33-interface)
message (
  VERBOSE
  "> micro-os-plus::cortexm-qemu-m33 -> micro-os-plus-iii-cortexm-qemu-m33-interface"
)

message (STATUS "µOS++ III Cortex-M port: micro-os-plus::cortexm (+ -rp2350, -rp2040)")
