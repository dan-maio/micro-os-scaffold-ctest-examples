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

// ----------------------------------------------------------------------------
//
// The SD card as a µOS++ POSIX I/O block device.
//
// sd::SdCard is the card driver of the chip's device package (sd.hpp selects
// the controller back-end with SD_BACKEND_*). This wraps it in the kernel's
// posix-io block device, so a file system (the @xpacks/chan-fatfs POSIX I/O
// integration) and the applications reach the card through the µOS++
// interfaces only:
//
//   static sd::block_device sd_card{ "sd" };   // registered as /dev/sd
//
// The card is initialised when the device is first opened (a mount does
// that), not at construction, so a board that declares the device costs
// nothing to a program that never uses it.
//
// The card driver is polled and not thread safe: the file system on top must
// serialise its users (chan_fatfs_file_system_lockable).

#ifndef MICRO_OS_PLUS_DEVICES_STORAGE_SD_BLOCK_DEVICE_H_
#define MICRO_OS_PLUS_DEVICES_STORAGE_SD_BLOCK_DEVICE_H_

#include <cmsis-plus/posix-io/block-device.h>

#include <sd.hpp>

#include <cstdarg>
#include <cstddef>

namespace sd
{
  class block_device_impl : public os::posix::block_device_impl
  {
  public:
    block_device_impl (void);

    block_device_impl (const block_device_impl&) = delete;
    block_device_impl (block_device_impl&&) = delete;
    block_device_impl&
    operator= (const block_device_impl&)
        = delete;
    block_device_impl&
    operator= (block_device_impl&&)
        = delete;

    virtual ~block_device_impl () override;

    // The driver, for what posix-io has no call for (the card type, the
    // driver's last error text).
    SdCard&
    card (void) noexcept
    {
      return card_;
    }

  protected:
    virtual int
    do_vopen (const char* path, int oflag, std::va_list args) override;

    virtual ssize_t
    do_read_block (void* buf, blknum_t blknum, std::size_t nblocks) override;

    virtual ssize_t
    do_write_block (const void* buf, blknum_t blknum,
                    std::size_t nblocks) override;

    virtual int
    do_vioctl (int request, std::va_list args) override;

    virtual void
    do_sync (void) override;

    virtual int
    do_close (void) override;

  private:
    SdCard card_;
  };

  using block_device = os::posix::block_device_implementable<block_device_impl>;

} // namespace sd

#endif // MICRO_OS_PLUS_DEVICES_STORAGE_SD_BLOCK_DEVICE_H_
