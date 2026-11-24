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
python3 micro-os-plus-iii/tests/platforms/aarch64-rpi-zero-2w/tests/usb-sd-stdcpp/host_xfer.py \
  --sizes 0,1,511,512,513,1024,4096,65536
# or, instead, one file:
python3 micro-os-plus-iii/tests/platforms/aarch64-rpi-zero-2w/tests/usb-sd-stdcpp/send_file.py myfile.bin --get --terminate
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

## Results on the board (2026-11-24)

`host_xfer.py --sizes 0,1,511,512,513,1024,4096,65536`:

- every size passed PUT, GET and the CRC check, plus LIST;
- the board's tally: 19 commands, 72 193 bytes in and out, no CRC error;
- all 4 cores active;
- `RESULT: PASS`, in 42 s;
- the SD writes took 21 to 69 ms each, with the files under `/sd/tests/`.

**The 1 MiB size** (in `host_xfer.py`'s default list) fails here, and it
fails the same way with the original `usb_test`. The device prints every
stored file to the UART as a hexdump before it replies. For 1 MiB that is
about 3.3 MB of text, roughly 5 minutes at 115200 baud, and the host waits
only 120 s for the reply. Use `--sizes` up to 64 KiB (or 128 KiB). For one
large file, `send_file.py` alone also waits 120 s.

`host_xfer.py` ends the run itself, so don't run `send_file.py
--terminate` after it. Run `send_file.py` on its own, or before
`host_xfer.py`.
