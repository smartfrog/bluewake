#include "scene_skip.h"

#include <assert.h>
#include <string.h>

static CPUState logo_call(void) {
    CPUState cpu;
    memset(&cpu, 0, sizeof cpu);
    cpu.lr = WW_SCENE_SKIP_LOGO_CALL_RETURN;
    cpu.gpr[4] = WW_SCENE_SKIP_OPENING_SCENE;
    return cpu;
}

int main(void) {
    assert(!ww_scene_skip_logo_rewrite(NULL, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));

    CPUState cpu = logo_call();
    assert(ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));
    assert(cpu.gpr[4] == WW_SCENE_SKIP_NAME_SCENE);

    cpu = logo_call();
    cpu.lr = WW_SCENE_SKIP_LOGO_CALL_RETURN + 4u;
    assert(!ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));
    assert(cpu.gpr[4] == WW_SCENE_SKIP_OPENING_SCENE);

    cpu = logo_call();
    cpu.gpr[4] = WW_SCENE_SKIP_OPENING_SCENE + 1u;
    assert(!ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));
    assert(cpu.gpr[4] == WW_SCENE_SKIP_OPENING_SCENE + 1u);

    cpu = logo_call();
    assert(!ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE + 4u));
    assert(cpu.gpr[4] == WW_SCENE_SKIP_OPENING_SCENE);

    cpu = logo_call();
    assert(ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));
    cpu.gpr[4] = WW_SCENE_SKIP_OPENING_SCENE;
    assert(ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));
    assert(cpu.gpr[4] == WW_SCENE_SKIP_NAME_SCENE);

    return 0;
}
