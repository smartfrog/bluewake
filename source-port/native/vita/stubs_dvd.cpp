// P-A blocker 2 POC (build-f5/source-port-next-pa/REPORT.md): the DVD entry points the covered game
// units reference, for a Vita source link with no disc at run time.
//
// The Vita build reads the assets its units were compiled against at start-up (COS_RUNTIME_ASSETS,
// gen_assets.sh --stubs, native/src/pc/runtime/pc_assets.cpp); the runtime disc path (archives,
// messages, stages opened by the game while it runs) is not part of P-A and not implemented. So
// these entry points fail loudly and say why, rather than pretend a file was read: a unit that
// reaches the disc path ends that read with an error a caller can see, and the log names it once.
//
// DVDOpen (and not DVDFastOpen) is what d_a_movie_player.cpp, d_map.cpp and the movie player use;
// the four here are the names the probe's units leave undefined. A new one fails the link check
// (native/vita/check_static_init_link.py) instead of being guessed here.
#include <dolphin/dvd.h>

#include <cstdio>

namespace {

void logOnce(bool& logged, const char* name) {
    if (!logged) {
        logged = true;
        std::fprintf(stderr, "vita-dvd: %s: the Vita source build has no disc at run time "
                             "(assets are read at start-up, COS_RUNTIME_ASSETS)\n", name);
    }
}

} // namespace

extern "C" {

BOOL DVDOpen(const char* fileName, DVDFileInfo* fileInfo) {
    static bool logged;
    logOnce(logged, "DVDOpen");
    (void)fileName;
    (void)fileInfo;
    return FALSE;
}

BOOL DVDClose(DVDFileInfo* fileInfo) {
    static bool logged;
    logOnce(logged, "DVDClose");
    (void)fileInfo;
    return FALSE;
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio) {
    static bool logged;
    logOnce(logged, "DVDReadPrio");
    (void)fileInfo;
    (void)addr;
    (void)length;
    (void)offset;
    (void)prio;
    return -1;
}

s32 DVDCancel(volatile DVDCommandBlock* block) {
    static bool logged;
    logOnce(logged, "DVDCancel");
    (void)block;
    return -1;
}

} // extern "C"
