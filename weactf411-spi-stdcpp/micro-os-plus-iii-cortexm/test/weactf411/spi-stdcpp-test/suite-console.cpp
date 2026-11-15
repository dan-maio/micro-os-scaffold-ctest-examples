// 00 - Console and start-up: the semihosting command line the runner passes
// (argc/argv), and the USART1 console the board prints on. The checks are in
// the report even when the UART pins are not wired: both go to the console
// and to the TAP file through semihosting.
#include <mtp_board.hpp>

#include <board.hpp>
#include <uart.hpp>

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "stm32f4xx.h"

namespace
{
  bool
  ends_with (const char* text, const char* suffix)
  {
    if (text == nullptr || suffix == nullptr)
      {
        return false;
      }
    const std::size_t n = std::strlen (text);
    const std::size_t m = std::strlen (suffix);
    return n >= m && std::strcmp (text + n - m, suffix) == 0;
  }
} // namespace

mt::static_suite suite_console{
  "00 - Console and start-up", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("semihosting command line reaches os_main ()", [] (auto& t) {
      const int argc = mtp_board::argument_count ();
      const char* argv0 = mtp_board::argument (0);
      const char* argv1 = mtp_board::argument (1);
      const char* argv2 = mtp_board::argument (2);

      t.expect (argc >= 3) << "os_main () received the runner's words";
      t.expect (argv0 != nullptr
                && std::strcmp (argv0, "spi-stdcpp-test") == 0)
          << "argv[0] names this test";
      t.expect (argv1 != nullptr
                && std::strcmp (argv1, "--output-file") == 0)
          << "argv[1] is --output-file";
      t.expect (ends_with (argv2, ".tap"))
          << "argv[2] is the report file";

      std::printf ("# args: argc=%d argv[0]=\"%s\" argv[1]=\"%s\" "
                   "argv[2]=\"%s\"\n",
                   argc, argv0 != nullptr ? argv0 : "(null)",
                   argv1 != nullptr ? argv1 : "(null)",
                   argv2 != nullptr ? argv2 : "(null)");
    });

    ts.test ("USART1 console: PA9/PA10, 115200 8N1, enabled",
             [] (auto& t) {
               constexpr std::uint32_t enable
                   = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
               const std::uint32_t cr1 = USART1->CR1;
               const std::uint32_t divisor
                   = (SystemCoreClock + (board::UART_BAUD / 2U))
                     / board::UART_BAUD;

               t.expect ((cr1 & enable) == enable)
                   << "USART1 is enabled with transmitter and receiver";
               t.expect (mt::eq (static_cast<unsigned long> (USART1->BRR),
                                 static_cast<unsigned long> (divisor)))
                   << "the baud divisor matches the kernel clock";
               t.expect (((GPIOA->MODER
                           >> (board::UART_TX_PIN * 2U))
                          & 0x3U)
                         == 0x2U)
                   << "PA9 is alternate-function";
               t.expect (((GPIOA->MODER
                           >> (board::UART_RX_PIN * 2U))
                          & 0x3U)
                         == 0x2U)
                   << "PA10 is alternate-function";
               t.expect (((GPIOA->AFR[1]
                           >> ((board::UART_TX_PIN & 0x7U) * 4U))
                          & 0xFU)
                         == board::UART_AF7)
                   << "PA9 is AF7 (USART1_TX)";
               t.expect (((GPIOA->AFR[1]
                           >> ((board::UART_RX_PIN & 0x7U) * 4U))
                          & 0xFU)
                         == board::UART_AF7)
                   << "PA10 is AF7 (USART1_RX)";

               std::printf ("# console: USART1 %lu baud, BRR=%lu, "
                            "CR1=%04lX, PA%u/PA%u AF%u\n",
                            static_cast<unsigned long> (board::UART_BAUD),
                            static_cast<unsigned long> (USART1->BRR),
                            static_cast<unsigned long> (cr1),
                            static_cast<unsigned> (board::UART_TX_PIN),
                            static_cast<unsigned> (board::UART_RX_PIN),
                            static_cast<unsigned> (board::UART_AF7));
             });

    ts.test ("the UART transmitter drains a line", [] (auto& t) {
      uart::write ("# UART: console alive\r\n");
      for (std::uint32_t i = 0; i < 2000000U; ++i)
        {
          if ((USART1->SR & USART_SR_TC) != 0U)
            {
              break;
            }
        }
      t.expect ((USART1->SR & USART_SR_TC) != 0U)
          << "transmission complete (TC set)";
    });
  }
};
