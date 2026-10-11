// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BW_DVD_SEAM_H
#define BW_DVD_SEAM_H

// Force-include header for the pinned GXRuntime/src/dvd.c. The pin is compiled
// byte-identical; only its four stdio primitives resolve to the seam. See
// dvd_io.h for the transport contract.
#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>

#include "dvd_io.h"

#define fopen  bw_dvd_fopen
#define fread  bw_dvd_fread
#define fseeko bw_dvd_fseeko
#define fclose bw_dvd_fclose

#endif
