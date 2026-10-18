// Host test of the M0 static-init check, split out of pc_smoke.cpp for the Vita source main
// (P-A blocker 2, build-f5/source-port-next-pa/REPORT.md).
//
//   ninja cos_static_init_test && build/cos_static_init_test
//
// The check reads the game's profile list, which a host run can only fill by linking all 502 REL
// actor units; so this test supplies its own list, its own fpcPf_Get and looks at what the check
// reports: the slot count, the summary line, and one error per way the list can be wrong (a name
// out of order, a getter that disagrees, a pointer that is not the list, no NULL terminator).
// The lines must stay byte-identical to the ones the PC harness printed before the split.
#include "pc/pc_static_init.h"

#include "f_pc/f_pc_name.h"
#include "f_pc/f_pc_profile.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

process_profile_definition* g_fpcPfLst_ProfileList[fpcNm_MAX_NUM_e + 1];
process_profile_definition** g_fpcPf_ProfileList_p = g_fpcPfLst_ProfileList;

static process_profile_definition sProfiles[3];
static int sGetShift;

process_profile_definition* fpcPf_Get(s16 profileId) {
    if (g_fpcPf_ProfileList_p == nullptr || profileId < 0 || profileId >= fpcNm_MAX_NUM_e) {
        return nullptr;
    }
    return g_fpcPf_ProfileList_p[profileId + sGetShift];
}

namespace {

int sFailures;

// Runs the check on a pipe and compares its error count and one line of its output.
void check(const char* name, int expectedErrors, const char* expected) {
    char buffer[256] = {0};
    int fds[2];
    if (pipe(fds) != 0) {
        std::fprintf(stderr, "static-init-test: FAIL %s: pipe\n", name);
        sFailures++;
        return;
    }
    int errors = pc::staticInitCheck(fds[1]);
    close(fds[1]);
    ssize_t length = read(fds[0], buffer, sizeof buffer - 1);
    close(fds[0]);
    buffer[length > 0 ? length : 0] = '\0';
    size_t size = std::strlen(buffer);
    if (size > 0 && buffer[size - 1] == '\n') {
        buffer[size - 1] = '\0';
    }
    const char* lastLine = std::strrchr(buffer, '\n');
    if (errors != expectedErrors || std::strstr(buffer, expected) == nullptr) {
        std::fprintf(stderr, "static-init-test: FAIL %s: %d error(s), expected %d, looking for \"%s\"\n%s",
                     name, errors, expectedErrors, expected, buffer);
        sFailures++;
        return;
    }
    std::printf("static-init-test: %s: %s\n", name, lastLine != nullptr ? lastLine + 1 : buffer);
}

} // namespace

int main() {
    check("empty", 0, "0 of 502 profile slots filled, 0 error(s)");

    sProfiles[0].mProcName = 0;
    sProfiles[1].mProcName = 1;
    g_fpcPfLst_ProfileList[0] = &sProfiles[0];
    g_fpcPfLst_ProfileList[1] = &sProfiles[1];
    check("two", 0, "2 of 502 profile slots filled, 0 error(s)");

    sProfiles[1].mProcName = 7;
    check("name", 1, "static-init: profile list entry 1 has mProcName 7");
    sProfiles[1].mProcName = 1;

    sGetShift = 1;
    check("get", 2, "static-init: fpcPf_Get(0) =");
    sGetShift = 0;

    g_fpcPfLst_ProfileList[0] = nullptr;
    g_fpcPfLst_ProfileList[1] = nullptr;
    process_profile_definition** list = g_fpcPf_ProfileList_p;
    g_fpcPf_ProfileList_p = nullptr;
    check("pointer", 1, "static-init: g_fpcPf_ProfileList_p (nil) is not g_fpcPfLst_ProfileList");
    g_fpcPf_ProfileList_p = list;

    g_fpcPfLst_ProfileList[fpcNm_MAX_NUM_e] = &sProfiles[2];
    check("null", 1, "static-init: the profile list has no NULL at index 502");
    g_fpcPfLst_ProfileList[fpcNm_MAX_NUM_e] = nullptr;

    if (sFailures != 0) {
        std::fprintf(stderr, "static-init-test: %d failure(s)\n", sFailures);
        return 1;
    }
    std::printf("static-init-test: ok\n");
    return 0;
}
