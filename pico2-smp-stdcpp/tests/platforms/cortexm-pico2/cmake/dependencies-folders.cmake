# -----------------------------------------------------------------------------
# Folders to add_subdirectory() for this platform.
#
# The Cortex-M port (and, through it, the kernel) is added by tests-main.cmake.
# The QEMU Cortex-M device is the only extra: the `qemu` images link it instead
# of the board (see cmake/definitions.cmake).
# -----------------------------------------------------------------------------

message (
  VERBOSE
  "Including tests/platforms/${PLATFORM_NAME}/cmake/dependencies-folders.cmake..."
)

set (
  xpack_dependencies_folders
  "${CMAKE_SOURCE_DIR}/device-qemu-cortexm"
  # The BINARY_DIR is the build/<config> folder; run.sh links the xPack there.
  "${CMAKE_BINARY_DIR}/xpacks/@xpacks/arm-cmsis"
)
