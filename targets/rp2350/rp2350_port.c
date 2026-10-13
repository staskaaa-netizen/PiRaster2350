#include "rp2350_port.h"

/* The default: say nothing. A program that wants the backend's handful of
   one-shot init lines defines its own `lvds_port_log()` and this one is no
   longer used. */
__attribute__((weak)) void lvds_port_log(const char *fmt, ...)
{
    (void)fmt;
}
