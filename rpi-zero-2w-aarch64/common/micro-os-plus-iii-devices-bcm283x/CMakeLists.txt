# -----------------------------------------------------------------------------
# micro-os-plus-iii-devices-bcm283x -- the Broadcom BCM283x devices of
# µOS++ III: BCM2837 (Raspberry Pi 3 B, Zero 2 W) and BCM2711 (Pi 4 B) SoC
# support, and their SD and USB drivers.
#
#   soc/<chip>/          the SoC's include/ and src/; a board names the one it
#                        is built on, and the board target (the consumer's
#                        micro-os-plus::board) compiles it
#   drivers/aarch32/     the drivers as built for AArch32 and for AArch64. The
#   drivers/aarch64/     two sets differ (sd.cpp, sd_sdhost.cpp, usb_dwc2.*);
#                        they were the two ports' copies and are kept as they
#                        are until they are reconciled and re-proven
#
# The chip-independent part (sd.hpp, the posix-io block device) is
# micro-os-plus-iii-devices-storage.
#
# Exports: micro-os-plus::soc-bcm2837, ::soc-bcm2711, ::devices-sd-posix (the
# SD card as a posix-io block device), ::devices-sdhost-posix (the same over
# the SDHOST controller), ::devices-usb (the DWC2 gadget)
# -----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.20)

project (micro-os-plus-iii-devices-bcm283x LANGUAGES C CXX)

set (UOS_DEVICES_BCM283X_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")

# The SoCs: headers only. Their src/ is compiled by the board target, which
# names the SoC it is built on.
if (NOT TARGET micro-os-plus::soc-bcm2837)
  add_library (micro-os-plus-iii-soc-bcm2837-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-soc-bcm2837-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/bcm2837/include"
  )
  add_library (micro-os-plus::soc-bcm2837
               ALIAS micro-os-plus-iii-soc-bcm2837-interface)
endif ()

if (NOT TARGET micro-os-plus::soc-bcm2711)
  add_library (micro-os-plus-iii-soc-bcm2711-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-soc-bcm2711-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/bcm2711/include"
  )
  add_library (micro-os-plus::soc-bcm2711
               ALIAS micro-os-plus-iii-soc-bcm2711-interface)
endif ()

# The drivers, for the architecture being built.
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  set (_drivers "${CMAKE_CURRENT_SOURCE_DIR}/drivers/aarch64")
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
  set (_drivers "${CMAKE_CURRENT_SOURCE_DIR}/drivers/aarch32")
else ()
  set (_drivers "")
endif ()

if (_drivers AND NOT TARGET micro-os-plus::devices-sd-posix)
  # The SD card driver as a µOS++ POSIX I/O block device (sd::block_device,
  # micro-os-plus-iii-devices-storage); the file system comes from
  # @xpacks/chan-fatfs.
  add_library (micro-os-plus-iii-devices-sd-posix-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-devices-sd-posix-interface INTERFACE "${_drivers}/include"
  )
  target_sources (
    micro-os-plus-iii-devices-sd-posix-interface INTERFACE "${_drivers}/src/sd.cpp"
  )
  target_link_libraries (
    micro-os-plus-iii-devices-sd-posix-interface
    INTERFACE micro-os-plus::devices-storage-sd micro-os-plus::devices-storage-posix
  )
  add_library (micro-os-plus::devices-sd-posix ALIAS micro-os-plus-iii-devices-sd-posix-interface)
endif ()

if (_drivers AND NOT TARGET micro-os-plus::devices-sdhost-posix)
  # The BCM2837's other SD controller, SDHOST, as a µOS++ POSIX I/O block
  # device (sd::block_device): for a board whose Arasan controller drives
  # something else (the Zero 2 W's and Pi 3 B's Wi-Fi radio). Instead of
  # devices-sd-posix, never with it: both define sd::SdCard.
  add_library (micro-os-plus-iii-devices-sdhost-posix-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-devices-sdhost-posix-interface INTERFACE "${_drivers}/include"
  )
  target_compile_definitions (
    micro-os-plus-iii-devices-sdhost-posix-interface INTERFACE SD_BACKEND_SDHOST
  )
  target_sources (
    micro-os-plus-iii-devices-sdhost-posix-interface INTERFACE "${_drivers}/src/sd_sdhost.cpp"
  )
  target_link_libraries (
    micro-os-plus-iii-devices-sdhost-posix-interface
    INTERFACE micro-os-plus::devices-storage-sd micro-os-plus::devices-storage-posix
  )
  add_library (micro-os-plus::devices-sdhost-posix ALIAS micro-os-plus-iii-devices-sdhost-posix-interface)
endif ()

if (_drivers AND NOT TARGET micro-os-plus::devices-usb)
  # The DWC2 USB device controller, for a test that also reaches the card
  # through the posix-io set above (devices-sd-posix).
  add_library (micro-os-plus-iii-devices-usb-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-devices-usb-interface INTERFACE "${_drivers}/include"
  )
  target_sources (
    micro-os-plus-iii-devices-usb-interface INTERFACE "${_drivers}/src/usb_dwc2.cpp"
  )
  add_library (micro-os-plus::devices-usb ALIAS micro-os-plus-iii-devices-usb-interface)
endif ()

# Where a consumer that builds a driver itself (a test with its own SD set)
# finds them.
set (UOS_DEVICES_BCM283X_DRIVERS "${_drivers}" CACHE INTERNAL "")
