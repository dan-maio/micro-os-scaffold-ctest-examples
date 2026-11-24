# `usb-sd-stdcpp`: USB gadget + SD card, written with standard C++

A local test of this workspace (`scripts/local-tests.txt`). It is the
platform's `usb_test`, with the same device, wire protocol and SD-card storage,
and an application layer rewritten on the standard C++ library: the StateOS
stdc++ package adapted to µOS++ III (`third_party/stateos-stdc++/`).

| Part | Written with |
| --- | --- |
| every thread (usb, sd, uart, semi, led, mon, two load generators) | `std::thread` (the USB thread pins itself to core 0 through its µOS++ thread) |
| console and SD request queues | `Channel<T>`: `std::mutex` + `std::condition_variable` + `std::deque`, bounded, blocking |
| SD requests and the mount | a `std::promise` per request, the USB thread waits on its `std::future` |
| the CRC-32 of a stored file | `std::async`, while the SD write runs |
| transfer timing | `std::chrono::steady_clock` |
| the per-core load tally | `std::atomic` counters |

Copied from `usb_test` unchanged: `protocol.hpp` (32-byte command, 16-byte
reply, CRC-32), `sink.hpp`/`sink.cpp` (files under `/sd/tests/` through µOS++
POSIX I/O and Chan FatFs, or a 128 KiB RAM sink without a card), and the host
apps `send_file.py` and `host_xfer.py`. The build settings are in
`../tests.local.cmake`: `stateos-stdc++`, `micro-os-plus::devices-usb`, the
storage list, and `usb_test`'s defines.

## Run it

Under QEMU (no USB device mode, no card): `RESULT: SKIP (no USB device mode)`,
after the cores, the threads and the storage fallback have run.

```bash
./run.sh usb-sd-stdcpp
```

On the board:

- power the Zero 2 W from `PWR IN`;
- connect the host to the `USB` socket (the OTG port);
- insert the boot SD card;
- attach the JTAG probe (`PROBE=cmsis-dap`).

Then:

```bash
PROBE=cmsis-dap ./run.sh --hwd-only usb-sd-stdcpp &
# when the console shows "USB device configured by host":
python3 micro-os-plus-iii/tests/platforms/aarch32-rpi-zero-2w/tests/usb-sd-stdcpp/host_xfer.py \
  --sizes 0,1,511,512,513,1024,4096,65536
# or, instead, one file:
python3 micro-os-plus-iii/tests/platforms/aarch32-rpi-zero-2w/tests/usb-sd-stdcpp/send_file.py myfile.bin --get --terminate
```

`host_xfer.py` PUTs and GETs each size, checks the CRCs, lists the folder,
and ends the run with a terminating PING. `send_file.py ... --terminate`
does the same for one file. Either way, the board then prints its tally and
verdict. The host side needs `pyusb` and, without root, the udev rule from
`usb_test/README.md` (`1d6b:0104`).

`RESULT: PASS` requires all three:

- no CRC error;
- at least one command served;
- at least 3 cores active.

For each stored file, the console prints the time the SD write took
(`put <name>: N bytes in M ms`) and the CRC computed alongside it.

## The console dump

Each stored file is printed on the UART in a short form:

- the BEGIN line;
- the first two hexdump lines;
- a `... N bytes not shown ...` line;
- the last hexdump line;
- the END line, with the CRC-32 of the whole file.

`usb_test` prints the full dump before it replies. For 1 MiB that is about
3.3 MB of text, roughly 5 minutes at 115200 baud, longer than the host's
120 s wait, so 1 MiB transfers timed out there.

## Results on the board (2026-11-24, AArch32)

This is the same test as in `rpi-zero-2w-aarch64-usb-stdcpp`, on the Zero 2 W
in AArch32 mode (4x Cortex-A53, 32-bit).

- Every `std::thread` gets 16 KiB of stack: `STATEOS_STDCPP_THREAD_STACK_BYTES=16384`
  in `../tests.local.cmake`. The stdc++ default is 4 KiB outside AArch64, too
  small for the sd thread (a FatFs file and its sector on the stack) and the
  usb thread.
- JTAG at 10000 kHz: this workspace keeps its own `bcm2837-aarch32.cfg`
  (`keep` in `scripts/local-tests.txt`).
- `host_xfer.py` with its default sizes, 0 B to 1 MiB:
  - every size passed PUT, GET and the CRC check, plus LIST, in 3.5 s;
  - the board's tally: 19 commands, 1 116 673 bytes in and out;
  - all 4 cores active, `RESULT: PASS`.
- `send_file.py` with a random 1 MiB file: a byte-for-byte match, and the
  board's CRC `ef389730` equal to the host's.
- This confirms the AArch32 driver's part of the USB fix below on the
  board.

**The bug this test found**, in the bcm283x device package's
`usb_dwc2::ep_read ()`:

- The driver pointed the USB DMA straight at the caller's buffer.
- This test's 1 MiB buffer ended 48 bytes into a cache line shared with
  the per-core counters, which the load threads update constantly.
- That line stayed live in the cache with the buffer's old bytes, so the
  last 48 bytes of every 1 MiB transfer arrived as zeros. The board's CRC,
  computed right after the receive, already differed from the host's.

The fix (micro-os-plus-iii-devices-bcm283x `027acb3`, AArch64 and AArch32):

- each chunk is received into a 64-byte-aligned driver buffer, then copied
  to the caller's;
- any caller buffer is now safe, including `usb_test`'s, which has the same
  layout.

`host_xfer.py` ends the run itself (a terminating PING), so run
`send_file.py --terminate` on its own, not after it.
