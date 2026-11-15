/*
 * init-fini-stubs.c — empty _init/_fini for __libc_init_array.
 *
 * With -nostartfiles the crti.o/crtn.o that normally provide _init/_fini are
 * excluded, but newlib's __libc_init_array() still references them. The C++
 * static constructors live in .init_array (run by __libc_init_array directly),
 * so these bodies can be empty.
 */

void
_init (void)
{
}

void
_fini (void)
{
}

/* Normally provided by the kernel's startup.cpp (excluded here — the RP2350
 * boot.S is used instead). exit() references it; os_main never returns, so an
 * empty body is fine. */
void
os_run_fini_array (void)
{
}

/* The command line, as boot.S hands it to main (). The weak default leaves
 * argc=0/argv=NULL (what boot.S used to hard-code); a µTest++ image provides
 * a strong reader of the semihosting SYS_GET_CMDLINE on top of this. */
__attribute__ ((weak)) void
os_startup_initialize_args (int* p_argc, char*** p_argv)
{
  *p_argc = 0;
  *p_argv = 0;
}
