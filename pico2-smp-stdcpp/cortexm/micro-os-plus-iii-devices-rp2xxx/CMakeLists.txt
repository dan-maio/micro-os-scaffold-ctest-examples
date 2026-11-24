# -----------------------------------------------------------------------------
# micro-os-plus-iii-devices-rp2xxx -- the Raspberry Pi RP2040 and RP2350
# devices of µOS++ III: the CMSIS device headers and SystemCoreClock, written
# from the datasheets; no Pico SDK.
#
# A board links the SoC target it is built on (the consumer's
# micro-os-plus::board).
#
# Exports: micro-os-plus::soc-rp2350, ::soc-rp2040
# -----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.20)

project (micro-os-plus-iii-devices-rp2xxx LANGUAGES C CXX)

set (UOS_DEVICES_RP2XXX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")

if (NOT TARGET micro-os-plus::soc-rp2350)
  add_library (micro-os-plus-iii-soc-rp2350-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-soc-rp2350-interface
    INTERFACE
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/rp2350/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/rp2350/include/cmsis"
  )
  target_sources (
    micro-os-plus-iii-soc-rp2350-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/rp2350/src/system_rp2350.c"
  )
  add_library (micro-os-plus::soc-rp2350
               ALIAS micro-os-plus-iii-soc-rp2350-interface)
endif ()

# The RP2040 (Raspberry Pi Pico): the CMSIS device header and SystemCoreClock,
# written from the datasheet as the RP2350's are; no Pico SDK.
if (NOT TARGET micro-os-plus::soc-rp2040)
  add_library (micro-os-plus-iii-soc-rp2040-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-soc-rp2040-interface
    INTERFACE
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/rp2040/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/rp2040/include/cmsis"
  )
  target_sources (
    micro-os-plus-iii-soc-rp2040-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/rp2040/src/system_rp2040.c"
  )
  add_library (micro-os-plus::soc-rp2040
               ALIAS micro-os-plus-iii-soc-rp2040-interface)
endif ()
