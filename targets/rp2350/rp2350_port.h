#ifndef RP2350_PORT_H
#define RP2350_PORT_H

#include <stdint.h>

#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The four things this target needs that are not pixels, with the bare
   Pico-SDK defaults. A program that has its own millisecond clock, its own
   logging or its own busy section defines these before this file is included
   (`-DLVDS_PORT_LOG=...` on a build, or a header ahead of it) - that is the
   whole port, and it is why the target can be lifted into a project that is
   not uCNC without editing it.

   None of these four sits in the scanout path: the millisecond clock is the
   "is core1 up yet" timeout, the delay is PSRAM bring-up, the log is a handful
   of one-shot lines at init, and the busy flag guards the present copy. */

#ifndef LVDS_PORT_MILLIS
#define LVDS_PORT_MILLIS() ((uint32_t)to_ms_since_boot(get_absolute_time()))
#endif

#ifndef LVDS_PORT_DELAY_MS
#define LVDS_PORT_DELAY_MS(ms) sleep_ms((uint32_t)(ms))
#endif

/* Where the backend says what it is doing at init. Silent by default: a
   library has no business printing. A program overrides the *function* -
   `void lvds_port_log(const char *fmt, ...)` - with its own, and the weak
   default below disappears. The machine's build points it at its own log. */
void lvds_port_log(const char *fmt, ...);

#ifndef LVDS_PORT_LOG
#define LVDS_PORT_LOG(...) lvds_port_log(__VA_ARGS__)
#endif

/* True while the CPU must not spend time on a present. The machine's reason is
   an SD card read in progress, and the module has always skipped the copy
   then; a build with no such critical section leaves it false. */
#ifndef LVDS_PORT_IO_CRITICAL
#define LVDS_PORT_IO_CRITICAL() (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* RP2350_PORT_H */
