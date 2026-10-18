// E3 follow-up POC (build-f5/source-port-next-pa/REPORT.md, blocker 2): smokeStaticInit moved out
// of pc_smoke.cpp unchanged, so a build without pc_harness.cpp / Aurora DVD can run it.
//
// The only change from the harness copy is the output: it writes to a caller's fd with write(2)
// instead of fprintf(stderr), because pc_smoke.cpp's writef is part of the harness it must not
// pull in. The lines are byte for byte the ones the harness printed.
#include "pc/pc_static_init.h"

#include "f_pc/f_pc_name.h"
#include "f_pc/f_pc_profile.h"

#include <cstdarg>
#include <cstdio>
#include <unistd.h>

namespace pc {

namespace {

void putf(int fd, const char* format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    int length = std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (length > 0) {
        unsigned int size = length < static_cast<int>(sizeof line) ? static_cast<unsigned int>(length)
                                                                   : sizeof line - 1;
        [[maybe_unused]] ssize_t written = write(fd, line, size);
    }
}

} // namespace

int staticInitCheck(int fd) {
    int errors = 0;
    int profiles = 0;

    if (g_fpcPf_ProfileList_p != g_fpcPfLst_ProfileList) {
        putf(fd, "static-init: g_fpcPf_ProfileList_p %p is not g_fpcPfLst_ProfileList %p\n",
             (void*)g_fpcPf_ProfileList_p, (void*)g_fpcPfLst_ProfileList);
        errors++;
    }
    if (g_fpcPfLst_ProfileList[fpcNm_MAX_NUM_e] != NULL) {
        putf(fd, "static-init: the profile list has no NULL at index %d\n", (int)fpcNm_MAX_NUM_e);
        errors++;
    }
    for (int i = 0; i < fpcNm_MAX_NUM_e; i++) {
        process_profile_definition* profile = g_fpcPfLst_ProfileList[i];
        if (fpcPf_Get(i) != profile) {
            putf(fd, "static-init: fpcPf_Get(%d) = %p, list entry %p\n", i, (void*)fpcPf_Get(i),
                 (void*)profile);
            errors++;
        }
        if (profile == NULL) {
            continue;
        }
        profiles++;
        if (profile->mProcName != i) {
            putf(fd, "static-init: profile list entry %d has mProcName %d\n", i, profile->mProcName);
            errors++;
        }
    }

    putf(fd, "static-init: %d of %d profile slots filled, %d error(s)\n", profiles,
         (int)fpcNm_MAX_NUM_e, errors);
    return errors;
}

} // namespace pc
