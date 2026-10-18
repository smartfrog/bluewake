// P-A blocker 2 POC (build-f5/source-port-next-pa/REPORT.md): the PC port's optional features, off
// in the Vita source build.
//
// The game units call these hooks (d_drawlist, d_map, d_particle, m_Do_ext, ...), and their real
// implementations live in native/src/pc/features (pc_aspect, pc_qol, pc_dynres, pc_gpu_opts) and
// native/src/pc/game_hooks, which are PC runtime code: settings files, an options menu, Aurora's
// GPU timer and debug markers. None of that exists on the Vita yet, and P-A only needs the game
// units to link, so every entry point the covered units reference answers the base game's value
// (4:3, no quality-of-life option, no dynamic resolution, no depth of field, no extra paint pass)
// and logs its first call once. A unit whose behaviour depends on a feature's effect therefore
// shows up in the log instead of silently taking a no-op for an answer.
//
// Only the names the probe's units reference are defined; a new one fails the link check
// (native/vita/check_static_init_link.py) instead of being guessed here.
#include "pc/game_hooks.h"
#include "pc/pc_aspect.h"
#include "pc/pc_dynres.h"
#include "pc/pc_gpu_opts.h"
#include "pc/pc_qol.h"

#include <cstdio>

namespace {

void logOnce(bool& logged, const char* name) {
    if (!logged) {
        logged = true;
        std::fprintf(stderr, "vita-pc-features: %s: the PC feature is off in the Vita source build\n",
                     name);
    }
}

} // namespace

extern "C" {

int pc_aspect(void) {
    static bool logged;
    logOnce(logged, "pc_aspect");
    return PC_ASPECT_4_3;
}

float pc_aspect_t(void) {
    static bool logged;
    logOnce(logged, "pc_aspect_t");
    return 0.0f;
}

int pc_qol(PcQol which) {
    static bool logged;
    logOnce(logged, "pc_qol");
    (void)which;
    return 0;
}

int pc_dof_enabled(void) {
    static bool logged;
    logOnce(logged, "pc_dof_enabled");
    return 0;
}

void pc_dynres_3d_begin(void) {
    static bool logged;
    logOnce(logged, "pc_dynres_3d_begin");
}

void pc_dynres_3d_end(void) {
    static bool logged;
    logOnce(logged, "pc_dynres_3d_end");
}

void pc_gpu_group(const char* name) {
    static bool logged;
    logOnce(logged, "pc_gpu_group");
    (void)name;
}

int pc_paint_is_extra(void) {
    static bool logged;
    logOnce(logged, "pc_paint_is_extra");
    return 0;
}

} // extern "C"
