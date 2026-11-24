// 00 - SPI flash: the WeAct F411CE's W25Q-series NOR on SPI1, through the
// board driver (bsp/spi.cpp, flash.cpp): JEDEC ID, status, reads, every
// transport, and a scratch sector written and restored.
#include <mtp_board.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <flash.hpp>
#include <spi.hpp>

namespace
{
  std::uint8_t saved[4096];
  std::uint8_t pattern[256];
  std::uint8_t readback[4096];
} // namespace

mt::static_suite suite_spi{
  "00 - SPI flash", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("probe: SPI1 runs, a transport is chosen", [] (auto& t) {
      spi::init ();
      const spi::transport chosen = spi::probe ();
      spi::set_transport (chosen);

      std::printf ("# SPI: transport %s\n", spi::transport_name (chosen));
      t.expect (chosen == spi::transport::polled
                || chosen == spi::transport::irq
                || chosen == spi::transport::dma)
          << "probe () returned a known transport";
      t.expect (std::strcmp (spi::transport_name (chosen), "?") != 0)
          << "the transport has a name";
    });

    ts.test ("JEDEC ID: a Winbond NOR answers on the bus", [] (auto& t) {
      const flash::JedecId id = flash::read_id ();
      std::printf ("# SPI: JEDEC %02X %02X %02X\n", id.manufacturer,
                   id.mem_type, id.capacity);

      t.expect (mt::eq (static_cast<unsigned> (id.manufacturer), 0xEFu))
          << "manufacturer 0xEF (Winbond)";
      t.expect (id.mem_type != 0x00 && id.mem_type != 0xFF)
          << "a real memory type";
      t.expect (id.capacity >= 0x10 && id.capacity < 0x20)
          << "a capacity between 64 KiB and 2 GiB";
    });

    ts.test ("all three transports read the same JEDEC ID", [] (auto& t) {
      const flash::JedecId base = flash::read_id ();

      const spi::transport transports[] = { spi::transport::polled,
                                            spi::transport::irq,
                                            spi::transport::dma };
      for (const spi::transport tr : transports)
        {
          spi::set_transport (tr);
          const flash::JedecId id = flash::read_id ();
          t.expect (mt::eq (static_cast<unsigned> (id.manufacturer),
                            static_cast<unsigned> (base.manufacturer)))
              << spi::transport_name (tr);
          t.expect (mt::eq (static_cast<unsigned> (id.capacity),
                            static_cast<unsigned> (base.capacity)))
              << spi::transport_name (tr);
        }

      spi::set_transport (spi::probe ());
      t.expect (true) << "the probed transport is back";
    });

    ts.test ("status register and a non-destructive read", [] (auto& t) {
      const std::uint8_t status = flash::read_status1 ();
      std::printf ("# SPI: status %02X\n", status);
      t.expect (status != 0xFF) << "the status register is not a floating bus";
      t.expect ((status & 0x01u) == 0) << "the device is not busy";

      // The primary archive begins at 0; read a page without touching it.
      flash::read (0x0000'0000u, readback, 256);
      t.expect (true) << "a 256-byte read at 0 completed";
    });

    ts.test ("scratch sector: erase, program, verify, restore",
             [] (auto& t) {
               const flash::JedecId id = flash::read_id ();
               if (id.capacity < 0x0C || id.capacity >= 0x20)
                 {
                   t.expect (false) << "capacity out of range; cannot pick a "
                                       "scratch sector";
                   return;
                 }

               const std::uint32_t size = 1u << id.capacity;
               const std::uint32_t scratch = size - 4096u;
               std::printf ("# SPI: scratch sector at 0x%08lX of 0x%08lX\n",
                            static_cast<unsigned long> (scratch),
                            static_cast<unsigned long> (size));

               flash::read (scratch, saved, sizeof saved);
               for (std::size_t i = 0; i < sizeof pattern; ++i)
                 {
                   pattern[i] = static_cast<std::uint8_t> ((i * 7u + 3u) & 0xFFu);
                 }

               t.expect (flash::sector_erase (scratch))
                   << "the scratch sector erased";
               t.expect (flash::page_program (scratch, pattern,
                                              sizeof pattern))
                   << "a 256-byte page programmed";
               std::memset (readback, 0, sizeof readback);
               flash::read (scratch, readback, sizeof pattern);
               t.expect (std::memcmp (pattern, readback, sizeof pattern) == 0)
                   << "the programmed page reads back";

               t.expect (flash::sector_erase (scratch))
                   << "the scratch sector erased again";
               t.expect (flash::write (scratch, saved, sizeof saved))
                   << "the original 4 KiB restored";
               std::memset (readback, 0, sizeof readback);
               flash::read (scratch, readback, sizeof saved);
               t.expect (std::memcmp (saved, readback, sizeof saved) == 0)
                   << "the restored sector reads back";
             });
  }
};
