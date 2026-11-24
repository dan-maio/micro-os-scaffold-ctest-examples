/*
 * usb-sd-stdcpp - USB gadget + SD card file transfer, written with the
 * standard C++ library (the StateOS stdc++ package on µOS++ III SMP).
 *
 * The same device and wire protocol as the platform's usb_test, so the same
 * host apps drive it (send_file.py, host_xfer.py, next to this file): a
 * vendor-specific bulk device (1d6b:0104) that stores the files the host
 * PUTs under /sd/tests/ on the SD card, returns them on GET, lists them, and
 * echoes each stored file to the UART as a framed hexdump.
 *
 * What is different is the application layer, all standard C++:
 *
 *   threads       std::thread for every thread (the USB service thread pins
 *                 itself to core 0 through its µOS++ thread)
 *   channels      Channel<T>: a bounded queue made of std::mutex,
 *                 std::condition_variable and std::deque, instead of µOS++
 *                 message queues
 *   SD requests   each request carries a std::promise; the USB thread waits
 *                 on its std::future (the mount uses the same path)
 *   CRC           a stored file's CRC-32 is computed with std::async while
 *                 the SD write runs
 *   timing        std::chrono::steady_clock for the transfer rates
 *   counters      std::atomic per-core load counters (the SMP tally)
 *
 * Threads:
 *   usb     (core 0) usb_dwc2::service_thread_body (); runs command_loop ()
 *   sd      the only thread that touches the card (POSIX I/O + FatFs, /sd/)
 *   uart    the only writer of the UART
 *   semi    the only writer of the semihosting channel
 *   led     heartbeat
 *   mon     USB bring-up counters until the host configures the device
 *   w0, w1  unpinned load generators (the per-core tally)
 *
 * RESULT: PASS when no CRC error occurred, at least one command was served,
 * and at least 3 cores were active. Under QEMU (no USB device mode) the run
 * reports SKIP, as usb_test does.
 */

#include <cmsis-plus/rtos/os.h>

#include <uart.hpp>
#include <semihosting.hpp>
#include <led.hpp>
#include <exception_handler.hpp>
#include <bcm2837.hpp>
#include <mailbox.hpp>
#include <usb_dwc2.hpp>
#include <smp.hpp>
#include <hw_result.hpp>
#include <test-smp-boot.hpp>

#include "protocol.hpp"
#include "sink.hpp"

#include <cmsis-plus/posix-io/file-system.h>
#include <cmsis-plus/posix/sys/statvfs.h>
#include <uos-test-storage.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <future>
#include <mutex>
#include <string>
#include <thread>

extern "C" unsigned port_cpu_id (void);

namespace
{
using namespace std::chrono_literals;
using Result = usbtest::Sink::Result;

// ---------------------------------------------------------------------------
// A bounded channel: one or more producers, one consumer. send () blocks
// while the channel is full, as the µOS++ message queues of usb_test did, so
// no line is ever dropped.
// ---------------------------------------------------------------------------
template <typename T>
class Channel
{
public:
  explicit Channel (std::size_t capacity) : capacity_ (capacity) { }

  void
  send (T item)
  {
    std::unique_lock<std::mutex> lock (mutex_);
    not_full_.wait (lock, [this] { return items_.size () < capacity_; });
    items_.push_back (std::move (item));
    not_empty_.notify_one ();
  }

  T
  receive ()
  {
    std::unique_lock<std::mutex> lock (mutex_);
    not_empty_.wait (lock, [this] { return !items_.empty (); });
    T item = std::move (items_.front ());
    items_.pop_front ();
    not_full_.notify_one ();
    return item;
  }

private:
  std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  std::deque<T> items_;
  std::size_t capacity_;
};

// ---------------------------------------------------------------------------
// Console: the uart and semi threads are the only writers of their channels.
// ---------------------------------------------------------------------------
inline constexpr unsigned kChunkData = 128u;

struct OutMsg
{
  enum class Kind : std::uint8_t
  {
    text,  // a status line
    chunk, // file bytes, printed as a hexdump line
    flush  // drain marker: fulfil the promise once everything before it is out
  };
  Kind kind = Kind::text;
  std::string text;                    // text
  std::string name;                    // chunk
  std::uint32_t offset = 0u;           // chunk
  std::string bytes;                   // chunk
  std::shared_ptr<std::promise<void>> done; // flush
};

Channel<OutMsg> g_uart_q{ 64u };
Channel<OutMsg> g_semi_q{ 16u };

void
write_fmt (const char* fmt, ...)
{
  char b[160];
  std::va_list args;
  va_start (args, fmt);
  std::vsnprintf (b, sizeof (b), fmt, args);
  va_end (args);
  uart::uart1.puts (b);
}

// A status line on both consoles.
void
log_line (const std::string& text)
{
  OutMsg m;
  m.text = text;
  g_uart_q.send (m);
  g_semi_q.send (std::move (m));
}

// A status line on the UART only.
void
log_line_uart (const std::string& text)
{
  OutMsg m;
  m.text = text;
  g_uart_q.send (std::move (m));
}

std::string
format (const char* fmt, ...)
{
  char b[192];
  std::va_list args;
  va_start (args, fmt);
  std::vsnprintf (b, sizeof (b), fmt, args);
  va_end (args);
  return b;
}

std::string
hexdump_line (const OutMsg& m)
{
  std::string line = format ("usb-rx %s %08lx  ", m.name.c_str (),
                             static_cast<unsigned long> (m.offset));
  char hex[4];
  for (unsigned char c : m.bytes)
    {
      std::snprintf (hex, sizeof (hex), "%02x ", static_cast<unsigned> (c));
      line += hex;
    }
  line += '\n';
  return line;
}

void
uart_writer ()
{
  for (;;)
    {
      OutMsg m = g_uart_q.receive ();
      switch (m.kind)
        {
        case OutMsg::Kind::flush:
          m.done->set_value ();
          break;
        case OutMsg::Kind::text:
          uart::uart1.puts_uart (m.text.c_str ());
          break;
        case OutMsg::Kind::chunk:
          uart::uart1.puts_uart (hexdump_line (m).c_str ());
          break;
        }
    }
}

void
semi_writer ()
{
  for (;;)
    {
      OutMsg m = g_semi_q.receive ();
      if (m.kind == OutMsg::Kind::flush)
        {
          m.done->set_value ();
          continue;
        }
#if defined(SEMIHOST)
      if (m.kind == OutMsg::Kind::text)
        {
          semihosting::write_str (m.text.c_str ());
        }
#endif
    }
}

// Wait until both writers have printed everything queued before this call.
void
flush_consoles ()
{
  OutMsg u;
  u.kind = OutMsg::Kind::flush;
  u.done = std::make_shared<std::promise<void>> ();
  auto uart_done = u.done->get_future ();
  OutMsg s;
  s.kind = OutMsg::Kind::flush;
  s.done = std::make_shared<std::promise<void>> ();
  auto semi_done = s.done->get_future ();
  g_uart_q.send (std::move (u));
  g_semi_q.send (std::move (s));
  uart_done.wait ();
  semi_done.wait ();
}

// ---------------------------------------------------------------------------
// Storage: the sd thread is the only one that touches the card. A request
// carries a promise; the caller waits on its future.
// ---------------------------------------------------------------------------
struct SdAnswer
{
  Result result = Result::io_error;
  std::uint32_t out_len = 0u;
};

struct SdReq
{
  enum class Op : std::uint8_t
  {
    mount,
    put,
    get,
    list
  };
  Op op = Op::mount;
  const char* name = nullptr;
  const void* data = nullptr; // put
  void* buf = nullptr;        // get
  std::uint32_t len = 0u;     // put: bytes; get: capacity
  usbtest::FileVisitor visit = nullptr;
  void* visit_ctx = nullptr;
  std::shared_ptr<std::promise<SdAnswer>> answer;
};

Channel<SdReq> g_sd_q{ 4u };

usbtest::RamUartSink g_ram_sink;
usbtest::PosixSink g_posix_sink;
usbtest::Sink* g_sink = &g_ram_sink;

SdAnswer
sd_call (SdReq r)
{
  r.answer = std::make_shared<std::promise<SdAnswer>> ();
  std::future<SdAnswer> f = r.answer->get_future ();
  g_sd_q.send (std::move (r));
  return f.get ();
}

Result
sd_put (const char* n, const void* data, std::uint32_t len)
{
  SdReq r;
  r.op = SdReq::Op::put;
  r.name = n;
  r.data = data;
  r.len = len;
  return sd_call (std::move (r)).result;
}

Result
sd_get (const char* n, void* buf, std::uint32_t cap, std::uint32_t& out_len)
{
  SdReq r;
  r.op = SdReq::Op::get;
  r.name = n;
  r.buf = buf;
  r.len = cap;
  const SdAnswer a = sd_call (std::move (r));
  out_len = a.out_len;
  return a.result;
}

Result
sd_list (usbtest::FileVisitor v, void* ctx)
{
  SdReq r;
  r.op = SdReq::Op::list;
  r.visit = v;
  r.visit_ctx = ctx;
  return sd_call (std::move (r)).result;
}

// In the sd thread: the card's FAT partition at /sd/ (the BOOT card on
// hardware: mounted, never formatted; files only under /sd/tests/).
bool
sd_mount_storage ()
{
  uos_test_storage_start ();
  struct statvfs vfs;
  if (os::posix::statvfs ("/sd/", &vfs) != 0)
    {
      log_line ("storage: no FAT volume mounted at /sd/\n");
      return false;
    }
  log_line (format ("storage: %lu KiB total, %lu KiB free\n",
                    (unsigned long)((unsigned long long)vfs.f_blocks * vfs.f_frsize / 1024u),
                    (unsigned long)((unsigned long long)vfs.f_bfree * vfs.f_frsize / 1024u)));
  return true;
}

void
sd_service ()
{
  for (;;)
    {
      SdReq r = g_sd_q.receive ();
      SdAnswer a;
      switch (r.op)
        {
        case SdReq::Op::mount:
          g_sink = sd_mount_storage () ? static_cast<usbtest::Sink*> (&g_posix_sink)
                                       : static_cast<usbtest::Sink*> (&g_ram_sink);
          log_line (format ("storage: %s\n", g_sink->name ()));
          a.result = Result::ok;
          break;
        case SdReq::Op::put:
          a.result = g_sink->put (r.name, r.data, r.len);
          break;
        case SdReq::Op::get:
          a.result = g_sink->get (r.name, r.buf, r.len, a.out_len);
          break;
        case SdReq::Op::list:
          a.result = g_sink->list (r.visit, r.visit_ctx);
          break;
        }
      r.answer->set_value (a);
    }
}

// ---------------------------------------------------------------------------
// LED, monitor, load generators.
// ---------------------------------------------------------------------------
std::atomic<std::uint32_t> g_iters[OS_NCPU];

void
led_blinker ()
{
  // Above the load generators, so they cannot starve it.
  os::rtos::this_thread::thread ().priority (os::rtos::thread::priority::above_normal);
  for (;;)
    {
      for (unsigned i = 0; i < LED_BLINKS; ++i)
        {
          led::set (true);
          std::this_thread::sleep_for (std::chrono::milliseconds (LED_ON_MS));
          led::set (false);
          std::this_thread::sleep_for (std::chrono::milliseconds (LED_OFF_MS));
        }
      std::this_thread::sleep_for (std::chrono::milliseconds (LED_GAP_MS));
    }
}

void
usb_monitor ()
{
  for (;;)
    {
      std::this_thread::sleep_for (2s);
      if (usb_dwc2::configured ())
        {
          continue;
        }
      const volatile usb_dwc2::Debug& d = usb_dwc2::debug ();
      log_line_uart (format ("usb_dbg isr=%lu rst=%lu enum=%lu setup=%lu\n",
                             (unsigned long)d.isr_calls, (unsigned long)d.usbrst,
                             (unsigned long)d.enumdone, (unsigned long)d.setups));
    }
}

void
load_generator ()
{
  for (std::uint32_t n = 1;; ++n)
    {
      const unsigned c = port_cpu_id ();
      if (c < OS_NCPU)
        {
          g_iters[c].fetch_add (1u, std::memory_order_relaxed);
        }
      std::this_thread::yield ();
      if ((n & 0x3FFu) == 0u)
        {
          std::this_thread::sleep_for (1ms);
        }
    }
}

// ---------------------------------------------------------------------------
// The protocol (usb_test's, unchanged).
// ---------------------------------------------------------------------------
void
send_reply (std::uint32_t status, const void* payload, std::uint32_t len)
{
  usbtest::Reply r;
  r.magic = usbtest::kRspMagic;
  r.status = status;
  r.length = len;
  r.crc32 = (len != 0u) ? usbtest::crc32 (payload, len) : 0u;
  usb_dwc2::ep_write (&r, sizeof (r));
  if (len != 0u)
    {
      usb_dwc2::ep_write (payload, len);
    }
}

// The stored file, as a framed hexdump on the UART (the bulk dump stays off
// semihosting: every line there is a debug halt), the frame on both.
void
stream_file (const char* name, const std::uint8_t* data, std::uint32_t len,
             std::uint32_t crc)
{
  log_line (format ("---- BEGIN %s (%lu bytes) ----\n", name, (unsigned long)len));
  for (std::uint32_t off = 0u; off < len; off += kChunkData)
    {
      OutMsg m;
      m.kind = OutMsg::Kind::chunk;
      m.name = name;
      m.offset = off;
      const std::uint32_t n = (len - off < kChunkData) ? (len - off) : kChunkData;
      m.bytes.assign (reinterpret_cast<const char*> (data + off), n);
      g_uart_q.send (std::move (m));
    }
  log_line (format ("---- END %s crc32=%08lx ----\n", name, (unsigned long)crc));
}

struct ListCtx
{
  std::uint8_t* out;
  std::uint32_t cap;
  std::uint32_t used;
};

bool
list_visit (const usbtest::FileEntry& e, void* ctx)
{
  ListCtx* c = static_cast<ListCtx*> (ctx);
  const std::uint32_t need = static_cast<std::uint32_t> (std::strlen (e.name)) + 1u + 4u;
  if (c->used + need > c->cap)
    {
      return false;
    }
  const std::size_t nl = std::strlen (e.name) + 1u;
  std::memcpy (c->out + c->used, e.name, nl);
  c->used += static_cast<std::uint32_t> (nl);
  for (unsigned b = 0; b < 4u; ++b)
    {
      c->out[c->used++] = static_cast<std::uint8_t> ((e.size_bytes >> (8u * b)) & 0xFFu);
    }
  return true;
}

bool
log_list_visit (const usbtest::FileEntry& e, void* ctx)
{
  ++*static_cast<unsigned*> (ctx);
  log_line (format ("  %-19s  %8lu\n", e.name, (unsigned long)e.size_bytes));
  return true;
}

struct Stats
{
  std::uint32_t commands = 0u;
  std::uint32_t bytes_in = 0u;
  std::uint32_t bytes_out = 0u;
  std::uint32_t crc_errors = 0u;
};

[[noreturn]] void
finish (const Stats& s)
{
  log_line (format ("commands=%lu bytes_in=%lu bytes_out=%lu crc_errors=%lu\n",
                    (unsigned long)s.commands, (unsigned long)s.bytes_in,
                    (unsigned long)s.bytes_out, (unsigned long)s.crc_errors));

  log_line (format ("\n%s listing:\n", g_sink->name ()));
  unsigned files = 0u;
  (void)sd_list (log_list_visit, &files);
  log_line (files == 0u ? std::string ("  (empty)\n") : format ("  %u file(s)\n", files));

  unsigned active = 0u;
  for (unsigned c = 0u; c < OS_NCPU; ++c)
    {
      const std::uint32_t n = g_iters[c].load (std::memory_order_relaxed);
      log_line (format ("  core%u iterations=%lu\n", c, (unsigned long)n));
      active += (n != 0u) ? 1u : 0u;
    }

  const bool ok = (s.crc_errors == 0u) && (s.commands != 0u) && (active >= 3u);
  log_line (ok ? "RESULT: PASS\n" : "RESULT: FAIL (see tally)\n");
  flush_consoles ();
  if (ok)
    {
      hw_result::ok ();
    }
  else
    {
      hw_result::fail ();
    }
  for (;;)
    {
      std::this_thread::sleep_for (1s);
    }
}

void
command_loop (void*)
{
  static std::uint8_t filebuf[1u << 20]; // 1 MiB working buffer
  Stats s;
  log_line ("USB device configured by host\n");

  for (;;)
    {
      usbtest::Command cmd;
      if (usb_dwc2::ep_read (&cmd, sizeof (cmd)) != static_cast<int> (sizeof (cmd))
          || cmd.magic != usbtest::kCmdMagic)
        {
          continue;
        }
      ++s.commands;

      char name[usbtest::kNameMax + 1];
      std::size_t ni = 0u;
      for (; ni < usbtest::kNameMax && cmd.name[ni] != '\0'; ++ni)
        {
          name[ni] = cmd.name[ni];
        }
      name[ni] = '\0';

      switch (static_cast<usbtest::Op> (cmd.op))
        {
        case usbtest::Op::ping:
          send_reply (0u, nullptr, 0u);
          if ((cmd.flags & 1u) != 0u)
            {
              finish (s);
            }
          break;

        case usbtest::Op::put:
          {
            std::uint32_t len = cmd.length < sizeof (filebuf)
                                    ? cmd.length
                                    : static_cast<std::uint32_t> (sizeof (filebuf));
            const int got = usb_dwc2::ep_read (filebuf, len);
            const std::uint32_t ulen = got > 0 ? static_cast<std::uint32_t> (got) : 0u;

            // The CRC runs on another thread while the card is written.
            std::future<std::uint32_t> crc = std::async (
                std::launch::async, [ulen] { return usbtest::crc32 (filebuf, ulen); });
            const auto t0 = std::chrono::steady_clock::now ();
            const Result r = sd_put (name, filebuf, ulen);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                                std::chrono::steady_clock::now () - t0)
                                .count ();
            const std::uint32_t crc_value = crc.get ();
            if (r == Result::ok)
              {
                s.bytes_in += ulen;
                log_line (format ("put %s: %lu bytes in %ld ms\n", name,
                                  (unsigned long)ulen, (long)ms));
                stream_file (name, filebuf, ulen, crc_value);
              }
            send_reply (static_cast<std::uint32_t> (r), nullptr, 0u);
          }
          break;

        case usbtest::Op::get:
          {
            std::uint32_t out_len = 0u;
            const Result r = sd_get (name, filebuf, sizeof (filebuf), out_len);
            if (r == Result::ok)
              {
                s.bytes_out += out_len;
                send_reply (0u, filebuf, out_len);
              }
            else
              {
                send_reply (static_cast<std::uint32_t> (r), nullptr, 0u);
              }
          }
          break;

        case usbtest::Op::list:
          {
            ListCtx ctx{ filebuf, static_cast<std::uint32_t> (sizeof (filebuf)), 0u };
            const Result r = sd_list (list_visit, &ctx);
            send_reply (static_cast<std::uint32_t> (r), r == Result::ok ? filebuf : nullptr,
                        r == Result::ok ? ctx.used : 0u);
          }
          break;

        default:
          send_reply (static_cast<std::uint32_t> (Result::io_error), nullptr, 0u);
          break;
        }
    }
}

// The USB service thread: core 0, as usb_test's.
void
usb_service ()
{
  os::rtos::this_thread::thread ().cpu_affinity (1u << 0);
  std::this_thread::yield ();
  usb_dwc2::service_thread_body (); // never returns
}

} // namespace

int
os_main (int, char*[])
{
  write_fmt ("\n=== usb-sd-stdcpp on " PORT_BANNER_LONG " ===\n");

  const bool powered = mailbox::set_power_state (mailbox::kPowerDeviceUsb, true);
  write_fmt ("USB power domain: %s\n", powered ? "on" : "FAILED");
  const usb_dwc2::HwInfo hw = usb_dwc2::probe_hw ();
  write_fmt ("DWC2 GSNPSID: 0x%08lx\n", (unsigned long)hw.snpsid);

  // Everything slow happens before the USB core is connected (usb_test's
  // order): the cores, the console and storage threads, the SD mount.
  smp_install_boot_threads ();
  smp::start_secondary_cores ();
  write_fmt ("cores: %s after %d ms\n", test_join_summary (), test_wait_secondaries (3000));

  led::init ();
  std::thread (uart_writer).detach ();
  std::thread (semi_writer).detach ();
  std::thread (led_blinker).detach ();
  std::thread (sd_service).detach ();
  {
    SdReq mount;
    mount.op = SdReq::Op::mount;
    (void)sd_call (std::move (mount));
  }
  std::thread (usb_monitor).detach ();
  std::thread (load_generator).detach ();
  std::thread (load_generator).detach ();

  usb_dwc2::set_configured_callback (command_loop, nullptr);
  const usb_dwc2::InitResult ir = usb_dwc2::init ();
  if (ir != usb_dwc2::InitResult::ok)
    {
      log_line (format ("\nRESULT: SKIP (%s)\n", usb_dwc2::init_result_str (ir)));
      flush_consoles ();
      hw_result::ok ();
      for (;;)
        {
          std::this_thread::sleep_for (1s);
        }
    }
  std::thread (usb_service).detach ();

  for (;;)
    {
      std::this_thread::sleep_for (1s);
    }
}

// ---------------------------------------------------------------------------
// µOS++ start-up (usb_test's).
// ---------------------------------------------------------------------------
extern "C"
{
  extern char __heap_start[];
  extern char __heap_end[];
  extern char __fiq_stack_top[];
  extern char __irq_stack_top[];

  void os_startup_initialize_hardware_early (void) { }

  void
  os_startup_initialize_hardware (void)
  {
    uart::uart1.init ();
    os_startup_initialize_free_store (
        __heap_start, static_cast<std::size_t> (__heap_end - __heap_start));
    exception::init ();
  }

  [[noreturn]] static void
  custom_main_trampoline (void)
  {
    std::exit (os_main (0, nullptr));
  }

  extern void os_startup_create_thread_idle (void);
  extern os::rtos::thread* os_main_thread;

  int
  main (int, char*[])
  {
    using namespace os::rtos;
#if defined(OS_HAS_INTERRUPTS_STACK)
    interrupts::stack ()->set (
        reinterpret_cast<thread::stack::element_t*> (__fiq_stack_top),
        __irq_stack_top - __fiq_stack_top);
    interrupts::stack ()->initialize ();
#endif
    scheduler::initialize ();

    static thread::stack::element_t main_stack[8192];
    thread::attributes attr = thread::initializer;
    attr.th_stack_address = main_stack;
    attr.th_stack_size_bytes = sizeof (main_stack);
    static thread main_thread{ "main",
                               reinterpret_cast<thread::func_t> (custom_main_trampoline),
                               nullptr, attr };
    os_main_thread = &main_thread;

    os_startup_create_thread_idle ();
    scheduler::start ();
    return 0;
  }
}
