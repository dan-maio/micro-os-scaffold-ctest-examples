/*
 * spi-stdcpp-test -- the StateOS stdc++ package adapted to µOS++ III, on the
 * WeAct Studio F411CE (STM32F411CE, one Cortex-M4F), plus the board's SPI NOR
 * flash.
 *
 * The stdc++ suites are the same ones the Raspberry Pi 4 B and the Pico 2
 * run; the board adds a suite that drives the W25Q-series SPI flash through
 * the board's driver: JEDEC ID, status, reads, all three transports, and a
 * scratch sector erased, written and restored.
 *
 * The console is USART1 (model 2); the verdict and the TAP report also go
 * through semihosting. The UART is not touched by the runner.
 */
#include <mtp_board.hpp>

#include <cstdio>

int
os_main (int argc, char* argv[])
{
  mtp_board::start ("spi-stdcpp-test");
#if defined(MTP_HAS_SMP_BOOT) && MTP_HAS_SMP_BOOT
  mtp_board::release_cores ();
#endif
  std::printf ("# stdc++: StateOS stdc++ adapted to µOS++ III, %u core%s\n",
               static_cast<unsigned> (OS_NCPU),
               static_cast<unsigned> (OS_NCPU) == 1 ? "" : "s");
  return mtp_board::run (argc, argv);
}

#include <mtp_startup.hpp>
