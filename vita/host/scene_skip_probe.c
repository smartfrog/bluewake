#include "scene_skip.h"

#include <assert.h>
#include <string.h>

static CPUState opening_call(u32 lr) {
    CPUState cpu;
    memset(&cpu, 0, sizeof cpu);
    cpu.lr = lr;
    cpu.gpr[4] = WW_SCENE_SKIP_OPENING_SCENE;
    return cpu;
}

int main(void) {
    assert(!ww_scene_skip_logo_rewrite(NULL, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));

    CPUState cpu = opening_call(0x8022D174u);
    assert(ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));
    assert(cpu.gpr[4] == WW_SCENE_SKIP_NAME_SCENE);

    cpu = opening_call(0x80237440u);
    assert(ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));
    assert(cpu.gpr[4] == WW_SCENE_SKIP_NAME_SCENE);

    cpu = opening_call(0x8023749Cu);
    assert(ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));
    assert(cpu.gpr[4] == WW_SCENE_SKIP_NAME_SCENE);

    cpu = opening_call(0);
    assert(ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));
    assert(cpu.gpr[4] == WW_SCENE_SKIP_NAME_SCENE);

    cpu = opening_call(0x8022D174u);
    cpu.gpr[4] = 9u;
    assert(!ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));
    assert(cpu.gpr[4] == 9u);

    cpu = opening_call(0x8022D174u);
    assert(!ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE + 4u));
    assert(cpu.gpr[4] == WW_SCENE_SKIP_OPENING_SCENE);

    cpu = opening_call(0x8022D174u);
    assert(ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));
    cpu.gpr[4] = WW_SCENE_SKIP_OPENING_SCENE;
    assert(ww_scene_skip_logo_rewrite(&cpu, WW_SCENE_SKIP_CHANGE_OPENING_SCENE));
    assert(cpu.gpr[4] == WW_SCENE_SKIP_NAME_SCENE);

    return 0;
}
