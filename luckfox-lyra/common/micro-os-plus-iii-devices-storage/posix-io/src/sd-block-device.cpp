/*
 * This file is part of the µOS++ project (https://micro-os-plus.github.io/).
 * Copyright (c) 2026 Liviu Ionescu. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose is hereby granted, under the terms of the MIT license.
 *
 * If a copy of the license was not distributed with this file, it can be
 * obtained from https://opensource.org/licenses/mit.
 */

// The SD card as a µOS++ POSIX I/O block device (see sd-block-device.h).

#include <sd-block-device.h>

#include <cerrno>

namespace sd
{
  block_device_impl::block_device_impl (void)
  {
    // Until the card is up: one 512-byte block geometry, no blocks.
    block_logical_size_bytes_ = kSectorSize;
    block_physical_size_bytes_ = kSectorSize;
    num_blocks_ = 0;
  }

  block_device_impl::~block_device_impl ()
  {
  }

#pragma GCC diagnostic push
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

  int
  block_device_impl::do_vopen (const char* path, int oflag,
                               std::va_list args)
  {
    // Called on the first open only (posix-io counts the opens). The card
    // is brought up once; a later reopen finds it ready.
    if (!card_.ready () && !card_.init ())
      {
        errno = EIO;
        return -1;
      }
    num_blocks_ = card_.sector_count ();
    return 0;
  }

  ssize_t
  block_device_impl::do_read_block (void* buf, blknum_t blknum,
                                    std::size_t nblocks)
  {
    if (!card_.read_sectors (static_cast<std::uint32_t> (blknum),
                             static_cast<std::uint32_t> (nblocks), buf))
      {
        errno = EIO;
        return -1;
      }
    return static_cast<ssize_t> (nblocks);
  }

  ssize_t
  block_device_impl::do_write_block (const void* buf, blknum_t blknum,
                                     std::size_t nblocks)
  {
    if (!card_.write_sectors (static_cast<std::uint32_t> (blknum),
                              static_cast<std::uint32_t> (nblocks), buf))
      {
        errno = EIO;
        return -1;
      }
    return static_cast<ssize_t> (nblocks);
  }

  int
  block_device_impl::do_vioctl (int request, std::va_list args)
  {
    errno = ENOTTY;
    return -1;
  }

#pragma GCC diagnostic pop

  void
  block_device_impl::do_sync (void)
  {
    // Polled, unbuffered driver: every write is on the card when it returns.
  }

  int
  block_device_impl::do_close (void)
  {
    // The card stays initialised; there is nothing to release.
    return 0;
  }

} // namespace sd
