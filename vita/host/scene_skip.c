#include "scene_skip.h"

#include <stddef.h>

bool ww_scene_skip_logo_rewrite(CPUState* cpu, u32 address) {
    if (cpu == NULL || address != WW_SCENE_SKIP_CHANGE_OPENING_SCENE ||
        cpu->lr != WW_SCENE_SKIP_LOGO_CALL_RETURN || cpu->gpr[4] != WW_SCENE_SKIP_OPENING_SCENE)
        return false;
    cpu->gpr[4] = WW_SCENE_SKIP_NAME_SCENE;
    return true;
}
