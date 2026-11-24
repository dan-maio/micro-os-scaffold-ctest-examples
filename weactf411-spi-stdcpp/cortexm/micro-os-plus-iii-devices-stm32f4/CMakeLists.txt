# -----------------------------------------------------------------------------
# micro-os-plus-iii-devices-stm32f4 -- the STMicroelectronics STM32F4 devices
# of µOS++ III: the device headers (with the Arm CMSIS core headers they use),
# SystemInit and the interrupt vectors of the STM32F411xE and STM32F412Rx.
#
# A board links the SoC target it is built on (the consumer's
# micro-os-plus::board).
#
# Exports: micro-os-plus::soc-stm32f411xe, ::soc-stm32f412rx
# -----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.20)

project (micro-os-plus-iii-devices-stm32f4 LANGUAGES C CXX)

set (UOS_DEVICES_STM32F4_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")

if (NOT TARGET micro-os-plus::soc-stm32f411xe)
  add_library (micro-os-plus-iii-soc-stm32f4xx-common-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-soc-stm32f4xx-common-interface
    INTERFACE
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/stm32f4xx/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/stm32f4xx/include/cmsis"
  )
  target_sources (
    micro-os-plus-iii-soc-stm32f4xx-common-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/stm32f4xx/src/system_stm32f4xx.c"
  )

  add_library (micro-os-plus-iii-soc-stm32f411xe-interface INTERFACE)
  target_link_libraries (
    micro-os-plus-iii-soc-stm32f411xe-interface
    INTERFACE micro-os-plus-iii-soc-stm32f4xx-common-interface
  )
  target_sources (
    micro-os-plus-iii-soc-stm32f411xe-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/stm32f4xx/src/vectors_stm32f411xe.c"
  )
  add_library (micro-os-plus::soc-stm32f411xe
               ALIAS micro-os-plus-iii-soc-stm32f411xe-interface)

  add_library (micro-os-plus-iii-soc-stm32f412rx-interface INTERFACE)
  target_link_libraries (
    micro-os-plus-iii-soc-stm32f412rx-interface
    INTERFACE micro-os-plus-iii-soc-stm32f4xx-common-interface
  )
  target_sources (
    micro-os-plus-iii-soc-stm32f412rx-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/stm32f4xx/src/vectors_stm32f412rx.c"
  )
  add_library (micro-os-plus::soc-stm32f412rx
               ALIAS micro-os-plus-iii-soc-stm32f412rx-interface)
endif ()
