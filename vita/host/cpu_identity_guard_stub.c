/* Tests-only adoption slot for the CPU identity guard (cpu_identity_guard.c).
 * The real module defines g_bw_guest_cpu in its guest_ram_guard.c; the
 * game-free tests build has no module, so the slot lives here and stays NULL:
 * the guard is transparent until a probe adopts a state through it. */
#include "guest_ram_guard.h"

CPUState* g_bw_guest_cpu;
