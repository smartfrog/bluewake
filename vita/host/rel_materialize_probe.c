#include "probe_support.h"
#include "dvd_io.h"
#include "dispatch_loop.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static bool fail_trace;
static bool fail_alias;
static bool fail_link_trace;
static bool fail_sections;
static bool track_sections;
static void* section_allocations[WW_REL_MAX_IMAGES];
static u32 live_section_allocations;
static u32 total_section_allocations;
static u32 dispatched_address;
static u32 loop_successor;
static u32 loop_dispatches;
static u32 loop_instructions;
static bool loop_live;
static const char* constructor_scenario;
static u32 constructor_calls;
static u32 constructor_leaf_calls;

static int constructor_chunk(CPUState* cpu, u32 address) {
    if (address == 0x80001004u)
        return 0;
    if (cpu->host_call && ppc_host_call(cpu, address))
        return 1;
    cpu->downcount--;
    switch (address) {
    case 0xC09900F4u:
        mem_write32(cpu, cpu->gpr[1] - 16u, cpu->gpr[1]);
        cpu->gpr[1] -= 16u;
        mem_write32(cpu, cpu->gpr[1] + 20u, cpu->lr);
        cpu->gpr[3] = strcmp(constructor_scenario, "second-ctor-raw") == 0 ?
            mem_read32(cpu, 0x80002110u) + 0xB0u : 0xC0997710u;
        if (strcmp(constructor_scenario, "second-ctor-outside") == 0)
            cpu->gpr[3] += 16u;
        cpu->lr = strcmp(constructor_scenario, "second-ctor-wrong-owner") == 0 ? 0xC1B100F8u : 0xC09900F8u;
        if (strcmp(constructor_scenario, "second-ctor-raw") == 0)
            cpu->lr = mem_read32(cpu, 0x80002110u) + 0x84u;
        cpu->pc = 0xC0241178u;
        return 1;
    case 0xC0241178u:
        mem_write32(cpu, cpu->gpr[1] - 16u, cpu->gpr[1]);
        cpu->gpr[1] -= 16u;
        mem_write32(cpu, cpu->gpr[1] + 20u, cpu->lr);
        mem_write32(cpu, cpu->gpr[1] + 12u, cpu->gpr[31]);
        cpu->gpr[31] = cpu->gpr[3];
        cpu->pc = 0xC02411D8u;
        return 1;
    case 0xC02411D4u:
        cpu->gpr[31] += 4u;
        cpu->pc = 0xC02411D8u;
        return 1;
    case 0xC02411D8u:
        cpu->gpr[12] = mem_read32(cpu, cpu->gpr[31]);
        if (cpu->gpr[12] != 0u) {
            cpu->ctr = cpu->gpr[12];
            cpu->lr = 0xC02411D4u;
            cpu->pc = cpu->ctr & ~3u;
        } else {
            cpu->gpr[31] = mem_read32(cpu, cpu->gpr[1] + 12u);
            cpu->lr = mem_read32(cpu, cpu->gpr[1] + 20u);
            cpu->gpr[1] += 16u;
            cpu->pc = cpu->lr;
        }
        return 1;
    case 0xC09900FCu:
    case 0xC0990100u:
        assert(address == (constructor_calls == 0u ? 0xC09900FCu : 0xC0990100u));
        constructor_calls++;
        assert(cpu->gpr[13] == 0x80400000u && cpu->gpr[2] == 0x80300000u);
        assert(cpu->lr == 0xC02411D4u && cpu->gpr[1] == 0x80003FE0u);
        if (strcmp(constructor_scenario, "second-ctor-stale") == 0)
            assert(ww_host_unlink_rel(cpu->external_user_data, 0x80002100u) == NULL);
        if (strcmp(constructor_scenario, "second-ctor-changed-table") == 0)
            mem_write32(cpu, 0xC0997714u, 0xC1B100F4u);
        if (address == 0xC09900FCu && strcmp(constructor_scenario, "second-ctor-leaf") == 0) {
            mem_write32(cpu, cpu->gpr[1] - 16u, cpu->gpr[1]);
            cpu->gpr[1] -= 16u;
            mem_write32(cpu, cpu->gpr[1] + 20u, cpu->lr);
            cpu->lr = 0xC0990104u;
            cpu->pc = 0xC0990108u;
            return 1;
        }
        cpu->pc = cpu->lr;
        return 1;
    case 0xC0990104u:
        cpu->lr = 0xC099010Cu;
        cpu->pc = 0xC0990108u;
        return 1;
    case 0xC0990108u:
        assert(cpu->gpr[1] == 0x80003FD0u);
        assert(cpu->lr == (constructor_leaf_calls == 0u ? 0xC0990104u : 0xC099010Cu));
        constructor_leaf_calls++;
        for (u32 offset = 0u; offset < 12u; offset += 4u)
            assert(ppc_stfs_op(cpu, 1u, 0xC0997700u + offset, address));
        cpu->pc = cpu->lr;
        return 1;
    case 0xC099010Cu:
        cpu->lr = mem_read32(cpu, cpu->gpr[1] + 20u);
        cpu->gpr[1] += 16u;
        cpu->pc = cpu->lr;
        return 1;
    case 0xC09900F8u:
        assert(cpu->lr == (strcmp(constructor_scenario, "second-ctor-raw") == 0 ?
            mem_read32(cpu, 0x80002110u) + 0x84u : 0xC09900F8u));
        cpu->lr = 0xC0990110u;
        cpu->pc = 0xC02410B4u;
        return 1;
    case 0xC02410B4u:
        cpu->pc = cpu->lr;
        return 1;
    case 0xC0990110u:
        cpu->lr = mem_read32(cpu, cpu->gpr[1] + 20u);
        cpu->gpr[1] += 16u;
        cpu->pc = cpu->lr;
        return 1;
    default:
        assert(false);
        return 0;
    }
}

static int constructor_dispatch(CPUState* cpu, u32 address) {
    return bluewake_chassis_dispatch_loop(cpu, address, constructor_chunk, ww_host_edge, cpu->external_user_data);
}

static int loop_chunk_dispatch(CPUState* cpu, u32 address) {
    loop_dispatches++;
    if (cpu->host_call && ppc_host_call(cpu, address))
        return 1;
    loop_instructions++;
    cpu->downcount--;
    cpu->pc = loop_live && address == 0xC09900F4u ? cpu->lr : loop_successor;
    return 1;
}

static int composite_dispatch(CPUState* cpu, u32 address) {
    return bluewake_chassis_dispatch_loop(cpu, address, loop_chunk_dispatch,
                                         ww_host_edge, cpu->external_user_data);
}

static u64 actor_code_hash(const char* scenario) {
    u8 bytes[32];
    for (u32 at = 0u; at < sizeof(bytes); ++at)
        bytes[at] = (u8)((at + 0x80u) * 7u + 3u);
    if (strncmp(scenario, "second-branch-", 14u) == 0) {
        bytes[0] = strcmp(scenario, "second-branch-rel14-displacement") == 0 ? 0x40u : 0x48u;
        memset(bytes + 1u, 0, 3u);
    }
    u64 hash = 14695981039346656037ull;
    for (u32 at = 0u; at < sizeof(bytes); ++at) {
        hash ^= bytes[at];
        hash *= 1099511628211ull;
    }
    return hash;
}

static int rel_dispatch(CPUState* cpu, u32 address) {
    dispatched_address = address;
    if (address != 0xC09900F4u)
        return 0;
    assert(cpu->pc == address);
    cpu->pc = cpu->lr;
    return 1;
}

void* __real_calloc(size_t count, size_t size);
void __real_free(void* pointer);

void* __wrap_calloc(size_t count, size_t size) {
    if (fail_alias && count == 1u && size == 256u)
        return NULL;
    if (fail_sections && count == 4u && size == sizeof(WwRelSection))
        return NULL;
    void* result = __real_calloc(count, size);
    if (track_sections && result != NULL && count >= 2u && count <= 256u && size == sizeof(WwRelSection)) {
        u32 at = 0u;
        while (at < WW_REL_MAX_IMAGES && section_allocations[at] != NULL)
            at++;
        assert(at < WW_REL_MAX_IMAGES);
        section_allocations[at] = result;
        live_section_allocations++;
        total_section_allocations++;
    }
    return result;
}

void __wrap_free(void* pointer) {
    if (pointer != NULL)
        for (u32 at = 0u; at < WW_REL_MAX_IMAGES; ++at)
            if (section_allocations[at] == pointer) {
                section_allocations[at] = NULL;
                live_section_allocations--;
                break;
            }
    __real_free(pointer);
}

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (stream == stderr && ((fail_trace && strncmp(format, "[rel-materialized]", 18u) == 0) ||
                            (fail_link_trace && strncmp(format, "[rel-link]", 10u) == 0))) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static void request_rel(WwHost* host, u32 object, u32 text, const char* name) {
    CPUState* cpu = &host->cpu;
    cpu->pc = 0x80240744u;
    cpu->gpr[3] = object;
    mem_write32(cpu, object + 0x1Cu, text);
    memcpy(cpu->ram + text - GC_RAM_BASE, name, strlen(name) + 1u);
    assert(ww_host_edge(host, cpu, cpu->pc));
}

static void second_module_case(WwHost* host, const char* scenario, StaticRecompRange* ranges) {
    CPUState* cpu = &host->cpu;
    const bool profile_owner_expired = strncmp(scenario, "second-profile-", 15u) == 0;
    const bool profile_global_changed = strcmp(scenario, "second-profile-global-cleared") == 0 ||
        strcmp(scenario, "second-profile-global-replaced") == 0;
    const bool rejected = strstr(scenario, "bad-") != NULL || strstr(scenario, "reloc-") != NULL ||
                          strstr(scenario, "branch-") != NULL || strstr(scenario, "lifecycle-") != NULL ||
                          strstr(scenario, "materialize-") != NULL ||
                          strcmp(scenario, "second-dependency-unlinked") == 0 ||
                          strcmp(scenario, "second-dependency-unloaded") == 0 ||
                          strcmp(scenario, "second-no-code") == 0 || strcmp(scenario, "second-missing-bss") == 0 ||
                          strcmp(scenario, "second-scratch-full") == 0 ||
                          profile_global_changed;
    const WwRelImage profile = host->rel_images[0];
    const u32 profile_owner_word = profile_owner_expired ?
        (strcmp(scenario, "second-profile-owner-reused") == 0 ? 0xA5A5A5A5u : 0u) : profile.guest;
    mem_write32(cpu, profile.owner + 0x10u, profile_owner_word);
    const u32 profile_global = profile_global_changed ?
        (strcmp(scenario, "second-profile-global-cleared") == 0 ? 0u : 0xC0997710u) : 0xC1F80178u;
    mem_write32(cpu, 0x803F6A68u, profile_global);
    const bool dependency_live = strcmp(scenario, "second-dependency-live") == 0;
    const bool other_constructor_owner = strcmp(scenario, "second-ctor-wrong-owner") == 0 ||
        strcmp(scenario, "second-ctor-changed-table") == 0;
    if (dependency_live || other_constructor_owner || strcmp(scenario, "second-dependency-unloaded") == 0) {
        request_rel(host, 0x80002200u, 0x80003200u, "ww34_fixture_b");
        assert(host->error == NULL && mem_read32(cpu, 0x80002210u) != 0u);
        if (!dependency_live && !other_constructor_owner) {
            assert(ww_host_unlink_rel(host, 0x80002200u) == NULL);
            assert(ww_host_collect_rels(host) == NULL && host->alias_count == 1u);
        }
    }
    fail_sections = strcmp(scenario, "second-materialize-sections") == 0;
    fail_alias = strcmp(scenario, "second-materialize-alias") == 0;
    fail_trace = strcmp(scenario, "second-materialize-trace") == 0;
    if (strcmp(scenario, "second-scratch-full") == 0)
        ranges[2] = (StaticRecompRange){WW_REL_SCRATCH_BASE, WW_REL_SCRATCH_LIMIT};
    request_rel(host, 0x80002100u, 0x80003100u, "ww34_fixture_a");
    assert(memcmp(&profile, &host->rel_images[0], sizeof(profile)) == 0);
    assert(mem_read32(cpu, 0x80002010u) == profile_owner_word);
    assert(mem_read32(cpu, 0x803F6A68u) == profile_global);
    if (rejected) {
        assert(host->error != NULL && host->edge_stopped && cpu->pc == 0x80240744u);
        if (strcmp(scenario, "second-reloc-export-mismatch") == 0 ||
            strcmp(scenario, "second-reloc-raw-export-mismatch") == 0)
            assert(strstr(host->error, "prelinked export") != NULL);
        if (strcmp(scenario, "second-reloc-bss") == 0)
            assert(strstr(host->error, "BSS") != NULL);
        if (strcmp(scenario, "second-bad-code-hash") == 0)
            assert(strstr(host->error, "chunk hashes") != NULL);
        if (strcmp(scenario, "second-branch-misaligned") == 0 || strcmp(scenario, "second-branch-bss") == 0 ||
            strcmp(scenario, "second-branch-no-coverage") == 0)
            assert(strstr(host->error, "executable coverage") != NULL);
        if (strcmp(scenario, "second-branch-exclusive-end") == 0)
            assert(strstr(host->error, "outside MEM1") != NULL);
        if (strcmp(scenario, "second-branch-displacement") == 0 || strcmp(scenario, "second-branch-rel14-displacement") == 0)
            assert(strstr(host->error, "displacement") != NULL);
        if (strcmp(scenario, "second-branch-valid-unverified") == 0)
            assert(strstr(host->error, "certification") != NULL);
        if (strstr(scenario, "lifecycle-") != NULL)
            assert(strstr(host->error, "lifecycle") != NULL);
        if (strcmp(scenario, "second-dependency-unlinked") == 0 || strcmp(scenario, "second-dependency-unloaded") == 0)
            assert(strstr(host->error, "live dependency") != NULL);
        if (profile_global_changed)
            assert(strstr(host->error, "publication") != NULL);
        if (strcmp(scenario, "second-scratch-full") == 0)
            assert(strstr(host->error, "no bounded unoccupied range") != NULL);
        assert(mem_read32(cpu, 0x80002110u) == 0u && host->alias_count == 1u && host->alias_bytes == 256u);
        assert(ww_host_rel_owned(host, 0x80002100u) == NULL);
        assert(mem_read8(cpu, 0xC0997700u) == 0xAAu && mem_read8(cpu, 0xC0997710u) == 0xAAu);
        assert(live_section_allocations == 1u);
        assert(get_ram_ptr(cpu, WW_REL_SCRATCH_BASE + 0x200u, 256u, NULL) == NULL);
        return;
    }
    assert(host->error == NULL && cpu->pc == cpu->lr && cpu->gpr[3] == 1u);
    assert(ww_host_rel_live(host, 1u) == &host->rel_images[0]);
    const u32 actor = mem_read32(cpu, 0x80002110u);
    assert(actor >= WW_REL_SCRATCH_BASE && (u64)actor + 256u <= WW_REL_SCRATCH_LIMIT);
    assert(actor != 0u && actor != profile.guest && host->alias_count == (dependency_live || other_constructor_owner ? 3u : 2u));
    assert(host->alias_bytes == (dependency_live || other_constructor_owner ? 768u : 512u));
    for (u32 offset = 0u; offset < 16u; ++offset)
        assert(mem_read8(cpu, 0xC0997700u + offset) == 0u);
    assert(mem_read32(cpu, 0xC0997710u) == (strncmp(scenario, "second-ctor-", 12u) == 0 ?
        (strcmp(scenario, "second-ctor-empty") == 0 ? 0u : 0xC09900FCu) :
        dependency_live ? 0xC1B224D0u : 0x80001000u));
    assert(ww_host_rel_address(host, actor + 0x80u) == 0xC09900F4u);
    assert(ww_host_rel_address(host, (actor + 0x84u) | 0x40000000u) == 0xC09900F8u);
    mem_write8(cpu, 0xC0997700u, 0xBBu);
    if (strcmp(scenario, "second-loop-live") == 0 || strcmp(scenario, "second-indirect-live") == 0) {
        const bool indirect = strcmp(scenario, "second-indirect-live") == 0;
        const u32 allocations_before = total_section_allocations;
        BwDvdIoStats before, after;
        bw_dvd_io_stats(&before);
        loop_live = true;
        loop_successor = actor + 0x80u;
        cpu->pc = indirect ? loop_successor : 0x80240744u;
        cpu->cycle_budget = indirect ? 1 : 2;
        cpu->downcount = 0;
        if (indirect)
            assert(loop_chunk_dispatch(cpu, cpu->pc) == 1);
        else
            assert(ww_host_dispatch(host, cpu->pc) == 1);
        assert(host->error == NULL && cpu->pc == 0xC09900F4u);
        assert(loop_dispatches == 1u && loop_instructions == (indirect ? 0u : 1u));
        if (indirect)
            assert(loop_chunk_dispatch(cpu, cpu->pc) == 1);
        else
            assert(ww_host_dispatch(host, cpu->pc) == 1);
        assert(host->error == NULL && cpu->pc == cpu->lr && !host->edge_stopped);
        assert(loop_dispatches == 2u && loop_instructions == (indirect ? 1u : 2u));
        bw_dvd_io_stats(&after);
        assert(after.total_read_bytes == before.total_read_bytes && host->alias_count == 2u);
        assert(live_section_allocations == 2u && mem_read8(cpu, 0xC0997700u) == 0xBBu);
        assert(total_section_allocations == allocations_before);
    } else if (strcmp(scenario, "second-hole-reuse") == 0) {
        request_rel(host, 0x80002200u, 0x80003200u, "ww34_fixture_b");
        assert(host->error == NULL && host->alias_count == 3u && host->rel_image_count == 3u);
        const u32 other = mem_read32(cpu, 0x80002210u);
        const WwRelSection* other_sections = host->rel_images[2].sections;
        mem_write8(cpu, 0xC1B224D0u, 0xCCu);
        assert(ww_host_unlink_rel(host, 0x80002100u) == NULL);
        assert(ww_host_collect_rels(host) == NULL && live_section_allocations == 2u);
        assert(host->rel_images[1].guest == 0u && host->rel_image_count == 3u);
        request_rel(host, 0x80002100u, 0x80003100u, "ww34_fixture_a");
        assert(host->error == NULL && mem_read32(cpu, 0x80002110u) == actor);
        assert(host->rel_images[1].guest == actor && host->alias_count == 3u && host->alias_bytes == 768u);
        assert(mem_read32(cpu, 0x80002210u) == other && host->rel_images[2].sections == other_sections);
        assert(mem_read8(cpu, 0xC1B224D0u) == 0xCCu && mem_read8(cpu, 0xC0997700u) == 0u);
        assert(live_section_allocations == 3u && mem_read32(cpu, 0x80002010u) == profile.guest);
    } else if (strncmp(scenario, "second-ctor-", 12u) == 0) {
        constructor_scenario = scenario;
        if (strcmp(scenario, "second-ctor-untranslated") == 0)
            ranges[1].end = 0xC09900FCu;
        if (strcmp(scenario, "second-ctor-changed-raw") == 0)
            mem_write32(cpu, actor + 0xB4u, 1u);
        cpu->gpr[1] = 0x80004000u;
        cpu->gpr[2] = 0x80300000u;
        cpu->gpr[13] = 0x80400000u;
        cpu->gpr[31] = 0x12345678u;
        cpu->fpr[1] = 1.5;
        cpu->lr = 0x80001004u;
        cpu->pc = actor + 0x80u;
        cpu->downcount = 0;
        cpu->cycle_budget = 200;
        for (u32 turn = 0u; turn < 32u && cpu->pc != 0x80001004u && host->error == NULL; ++turn)
            assert(ww_host_dispatch(host, cpu->pc) != 0);
        const bool success = strcmp(scenario, "second-ctor-order") == 0 ||
            strcmp(scenario, "second-ctor-raw") == 0 || strcmp(scenario, "second-ctor-empty") == 0 ||
            strcmp(scenario, "second-ctor-leaf") == 0;
        if (success) {
            assert(host->error == NULL && cpu->pc == 0x80001004u && cpu->lr == cpu->pc);
            assert(constructor_calls == (strcmp(scenario, "second-ctor-empty") == 0 ? 0u : 2u));
            assert(cpu->gpr[1] == 0x80004000u && cpu->gpr[31] == 0x12345678u);
            assert(cpu->gpr[2] == 0x80300000u && cpu->gpr[13] == 0x80400000u);
            if (strcmp(scenario, "second-ctor-leaf") == 0) {
                assert(constructor_leaf_calls == 2u && cpu->fpr[1] == 1.5);
                for (u32 offset = 0u; offset < 12u; offset += 4u)
                    assert(mem_read32(cpu, 0xC0997700u + offset) == 0x3FC00000u);
            }
        } else {
            assert(host->error != NULL && cpu->pc != 0x80001004u);
            assert(constructor_calls == (strcmp(scenario, "second-ctor-stale") == 0 ||
                strcmp(scenario, "second-ctor-changed-table") == 0 ? 1u : 0u));
            assert(strstr(host->error, "REL constructor") != NULL);
        }
    } else if (strcmp(scenario, "second-repeat") == 0) {
        BwDvdIoStats before, after;
        bw_dvd_io_stats(&before);
        request_rel(host, 0x80002100u, 0x80003100u, "ww34_fixture_a");
        bw_dvd_io_stats(&after);
        assert(host->error == NULL && cpu->pc == cpu->lr && cpu->gpr[3] == 1u);
        assert(after.total_read_bytes == before.total_read_bytes && host->alias_count == 2u);
        assert(mem_read8(cpu, 0xC0997700u) == 0xBBu);
    } else if (strcmp(scenario, "second-unload") == 0) {
        assert(ww_host_unlink_rel(host, 0x80002100u) == NULL);
        assert(ww_host_rel_address(host, actor + 0x80u) == actor + 0x80u);
        assert(ww_host_collect_rels(host) == NULL);
        assert(host->alias_count == 1u && host->alias_bytes == 256u);
        assert(get_ram_ptr(cpu, actor, 256u, NULL) == NULL);
        request_rel(host, 0x80002100u, 0x80003100u, "ww34_fixture_a");
        assert(host->error == NULL && mem_read32(cpu, 0x80002110u) == actor);
        assert(mem_read8(cpu, 0xC0997700u) == 0u && host->alias_count == 2u);
    } else if (strcmp(scenario, "second-unload-incomplete") == 0) {
        mem_write32(cpu, 0x80002110u, 0u);
        assert(ww_host_collect_rels(host) != NULL);
        assert(host->alias_count == 2u && get_ram_ptr(cpu, actor, 256u, NULL) != NULL);
        assert(ww_host_unlink_rel(host, 0x80002100u) != NULL);
    } else if (dependency_live) {
        const u32 dependency = mem_read32(cpu, 0x80002210u);
        mem_write8(cpu, 0xC1B224D0u, 0xCCu);
        assert(ww_host_unlink_rel(host, 0x80002200u) != NULL);
        assert(mem_read32(cpu, 0x80002210u) == dependency && mem_read8(cpu, 0xC1B224D0u) == 0xCCu);
        assert(ww_host_materialize_rel(host, 0x80002300u, "ww34_fixture_b") != NULL);
        assert(mem_read8(cpu, 0xC1B224D0u) == 0xCCu && host->alias_count == 3u);
        assert(ww_host_unlink_rel(host, 0x80002100u) == NULL);
        assert(ww_host_collect_rels(host) == NULL);
        assert(ww_host_unlink_rel(host, 0x80002200u) == NULL);
        assert(ww_host_collect_rels(host) == NULL && host->alias_count == 1u);
        assert(get_ram_ptr(cpu, actor, 256u, NULL) == NULL && get_ram_ptr(cpu, dependency, 256u, NULL) == NULL);
        request_rel(host, 0x80002200u, 0x80003200u, "ww34_fixture_b");
        assert(host->error == NULL && mem_read32(cpu, 0x80002210u) == dependency);
        assert(mem_read8(cpu, 0xC1B224D0u) == 0u);
        assert(mem_read32(cpu, 0x80002010u) == profile.guest && ww_host_rel_live(host, 1u) != NULL);
    } else if (strcmp(scenario, "second-dispatch") == 0) {
        cpu->pc = actor + 0x80u;
        assert(ww_host_dispatch(host, cpu->pc) == 1);
        assert(dispatched_address == 0xC09900F4u && cpu->pc == cpu->lr);
        assert(ww_host_unlink_rel(host, 0x80002100u) == NULL);
        cpu->pc = actor + 0x80u;
        assert(ww_host_dispatch(host, cpu->pc) == 0);
        assert(dispatched_address == actor + 0x80u);
        cpu->pc = 0xC09900F4u;
        assert(ww_host_dispatch(host, cpu->pc) == -1);
        assert(host->error != NULL && dispatched_address == actor + 0x80u);
    } else if (strcmp(scenario, "second-native-unlink") == 0) {
        cpu->gpr[3] = 0x80002100u;
        cpu->pc = 0x80240F54u;
        assert(ww_host_dispatch(host, cpu->pc) == -1);
        assert(host->error != NULL && dispatched_address == 0u);
        assert(mem_read32(cpu, 0x80002110u) == actor && host->alias_count == 2u);
    } else if (strncmp(scenario, "second-loop-", 12u) == 0 || strncmp(scenario, "second-indirect-", 16u) == 0) {
        const bool indirect = strncmp(scenario, "second-indirect-", 16u) == 0;
        loop_successor = 0xC09900F4u;
        if (strstr(scenario, "unowned") != NULL) {
            assert(ww_host_unlink_rel(host, 0x80002100u) == NULL);
        } else if (strstr(scenario, "unlink") != NULL) {
            loop_successor = 0x80240F54u;
        } else if (strstr(scenario, "offset") != NULL) {
            mem_write32(cpu, actor + 0x54u, 0x85u);
            loop_successor = actor + 0x84u;
        } else if (strstr(scenario, "flags") != NULL) {
            mem_write32(cpu, actor + 0x64u, 0xB1u);
            loop_successor = actor + 0xB0u;
        }
        if (strstr(scenario, "linked-") != NULL)
            loop_successor = 0xC09900F4u;
        cpu->gpr[3] = 0x80002100u;
        cpu->pc = indirect ? loop_successor : 0x80240744u;
        cpu->cycle_budget = 256;
        cpu->downcount = 0;
        if (indirect)
            assert(loop_chunk_dispatch(cpu, cpu->pc) == 1);
        else
            assert(ww_host_dispatch(host, cpu->pc) == 1);
        assert(host->error != NULL && loop_dispatches == 1u && loop_instructions == (indirect ? 0u : 1u));
        if (!indirect)
            assert(host->edge_stopped);
        if (strstr(scenario, "offset") != NULL || strstr(scenario, "flags") != NULL)
            assert(strstr(host->error, "section table changed") != NULL);
        if (strstr(scenario, "unowned") != NULL)
            assert(strstr(host->error, "live linked ownership") != NULL);
        if (strstr(scenario, "unlink") != NULL)
            assert(strstr(host->error, "epilog and destruction") != NULL);
        assert(mem_read32(cpu, 0x80002010u) == profile.guest && host->alias_count == 2u);
    } else if (strcmp(scenario, "second-independent") == 0) {
        request_rel(host, 0x80002200u, 0x80003200u, "ww34_fixture_b");
        assert(host->error == NULL && cpu->pc == cpu->lr && cpu->gpr[3] == 1u);
        assert(mem_read32(cpu, 0x80002210u) != actor && mem_read32(cpu, 0x80002210u) != profile.guest);
        assert(mem_read8(cpu, 0xC0997700u) == 0xBBu && mem_read8(cpu, 0xC1B224D0u) == 0u);
        assert(host->alias_count == 3u && host->alias_bytes == 768u);
    } else if (strcmp(scenario, "second-wrong-owner") == 0 || strcmp(scenario, "second-wrong-name") == 0) {
        const bool wrong_owner = strcmp(scenario, "second-wrong-owner") == 0;
        request_rel(host, wrong_owner ? 0x80002200u : 0x80002100u, 0x80003200u,
                    wrong_owner ? "ww34_fixture_a" : "ww34_fixture_b");
        assert(host->error != NULL && host->edge_stopped && host->alias_count == 2u);
        assert(mem_read32(cpu, 0x80002110u) == actor && mem_read8(cpu, 0xC0997700u) == 0xBBu);
        assert(mem_read32(cpu, 0x80002210u) == 0u);
    }
}

static int branch_validation_case(int argc, char** argv) {
    assert(argc == 9);
    const u32 patch_address = (u32)strtoul(argv[2], NULL, 0);
    const u32 target_address = (u32)strtoul(argv[3], NULL, 0);
    const u32 type = (u32)strtoul(argv[4], NULL, 0);
    const u32 word = (u32)strtoul(argv[5], NULL, 0);
    const u32 expected = (u32)strtoul(argv[6], NULL, 0);
    const bool self = strcmp(argv[8], "self") == 0;
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    u8 raw[256] = {0};
    write_be32(raw, 139u);
    write_be32(raw + 0xCu, 3u);
    write_be32(raw + 0x10u, 0x4Cu);
    write_be32(raw + 0x28u, 0xA0u);
    write_be32(raw + 0x2Cu, 8u);
    write_be32(raw + 0x54u, 0x80u);
    write_be32(raw + 0x58u, 4u);
    write_be32(raw + 0x80u, word);
    write_be32(raw + 0xA0u, self ? 139u : 0u);
    write_be32(raw + 0xA4u, 0xA8u);
    raw[0xAAu] = 202u;
    raw[0xABu] = 1u;
    raw[0xB2u] = (u8)type;
    raw[0xB3u] = self ? 2u : 0u;
    write_be32(raw + 0xB4u, self ? 0u : target_address);
    raw[0xBAu] = 203u;
    const StaticRecompRelSection sections[] = {
        {139u, 1u, patch_address, 4u}, {139u, 2u, target_address, 4u}};
    const StaticRecompRelModule rel = {.module_id = 139u, .section_count = 3u,
        .section_info_offset = 0x4Cu, .sections = sections, .num_sections = self ? 2u : 1u};
    const StaticRecompRange range = {target_address & ~3u, (target_address & ~3u) + 4u};
    u64 hash = 14695981039346656037ull;
    for (u32 at = 0u; at < 4u; ++at)
        hash *= 1099511628211ull;
    const StaticRecompModuleDesc module = {.code_ranges = &range, .num_code_ranges = 1u,
        .chunk_ranges = &range, .num_chunk_ranges = 1u, .chunk_hashes = &hash,
        .rel_modules = &rel, .num_rel_modules = 1u};
    if (self) {
        write_be32(raw + 0x5Cu, 0x85u);
        write_be32(raw + 0x60u, 4u);
    }
    u8 exported[4];
    write_be32(exported, expected);
    const BlueWakeRelData data = {139u, 1u, patch_address, 4u, exported};
    host->module = &module;
    host->rel_data = &data;
    host->rel_data_count = 1u;
    assert(ppc_guest_alias_add(patch_address, 4u, exported));
    const char* error = ww_host_validate_rel_link(host, &rel, raw, sizeof(raw));
    if (strcmp(argv[7], "ok") == 0) {
        if (error != NULL)
            fprintf(stderr, "%s\n", error);
        assert(error == NULL);
    } else {
        assert(error != NULL);
        if (strstr(error, argv[7]) == NULL)
            fprintf(stderr, "%s\n", error);
        assert(strstr(error, argv[7]) != NULL);
    }
    ww_host_shutdown(host);
    free(host);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "branch-validation") == 0)
        return branch_validation_case(argc, argv);
    assert(argc == 2);
    const char* scenario = argv[1];
    const bool second = strncmp(scenario, "second", 6u) == 0;
    const bool link_case = strncmp(scenario, "link", 4u) == 0 || second;
    const bool link_success = strcmp(scenario, "link") == 0 || strcmp(scenario, "link-repeat") == 0 || second;
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    track_sections = true;
    const bool constructors = strncmp(scenario, "second-ctor-", 12u) == 0;
    const u32 actor_code = strcmp(scenario, "second-branch-displacement") == 0 ? 0xC39900F4u : 0xC09900F4u;
    StaticRecompRange ranges[] = {{0x80240744u, 0x80240748u}, {actor_code, actor_code + 32u},
                                 {0xC1B100F4u, 0xC1B10114u}, {0x80241178u, 0x802411F8u},
                                 {0x802410B4u, 0x802410B8u}};
    const StaticRecompRange chunks[] = {{actor_code, actor_code + 32u}, {0xC1B100F4u, 0xC1B10114u}};
    const u64 hashes[] = {actor_code_hash(scenario), actor_code_hash("")};
    const StaticRecompRelSection sections[] = {
        {.module_id = 1u, .section_index = 1u, .linked_start = WW_REL_SCRATCH_BASE,
         .size = strcmp(scenario, "full") == 0 ? WW_REL_SCRATCH_LIMIT - WW_REL_SCRATCH_BASE : 0x100u},
        {.module_id = 1u, .section_index = 4u, .linked_start = 0xC1F80178u, .size = 16u},
    };
    const StaticRecompRelSection actor_sections[] = {
        {139u, 1u, actor_code, 32u}, {139u, 2u, 0xC0997700u, 16u}, {139u, 3u, 0xC0997710u, 16u}};
    const StaticRecompRelSection ship_sections[] = {
        {168u, 1u, 0xC1B100F4u, 32u}, {168u, 2u, 0xC1B224D0u, 16u}, {168u, 3u, 0xC1B224E0u, 16u}};
    const StaticRecompRelModule rels[] = {{.module_id = 1u, .version = 0u, .section_count = link_case ? 5u : 2u,
                                          .section_info_offset = 0x4Cu, .file_size = 256u,
                                          .sections = sections, .num_sections = link_case ? 2u : 1u},
                                         {.module_id = 139u, .section_count = 4u, .section_info_offset = 0x4Cu,
                                          .file_size = 256u, .sections = actor_sections, .num_sections = 3u},
                                         {.module_id = 168u, .section_count = 4u, .section_info_offset = 0x4Cu,
                                          .file_size = 256u, .sections = ship_sections, .num_sections = 3u}};
    const StaticRecompModuleDesc module = {.code_ranges = ranges,
                                          .num_code_ranges = constructors ? 5u : second && strcmp(scenario, "second-no-code") != 0 ? 3u : 1u,
                                          .rel_modules = rels, .num_rel_modules = second ? 3u : 1u,
                                          .chunk_ranges = chunks, .num_chunk_ranges = 2u, .chunk_hashes = hashes,
                                          .dispatch = constructors ? constructor_dispatch :
                                              strncmp(scenario, "second-loop-", 12u) == 0 ? composite_dispatch : rel_dispatch};
    host->module = &module;
    if (strcmp(scenario, "second-legacy-opt-in") == 0 || strcmp(scenario, "second-scratch-full") == 0) {
        static const BlueWakeShipContractV1 contract = {0};
        host->ship_contract = &contract;
    }
    const u8 profile_bytes[16] = {0x80u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u};
    const BlueWakeRelData profile = {1u, 4u, 0xC1F80178u, sizeof(profile_bytes), profile_bytes};
    const u8 dol_data[16] = {0x80u, 0u, 0x10u, 0u};
    const bool dependency_case = strncmp(scenario, "second-dependency-", 18u) == 0;
    const u8 actor_data[16] = {dependency_case ? 0xC1u : 0x80u, dependency_case ? 0xB2u : 0u,
                              dependency_case ? 0x24u : 0x10u, dependency_case ? 0xD0u : 0u};
    const u8 constructor_data[16] = {0xC0u, 0x99u, 0u, 0xFCu, 0xC0u, 0x99u, 1u, 0u};
    const u8 empty_data[16] = {0};
    const BlueWakeRelData both_data[] = {profile, {139u, 3u, 0xC0997710u, 16u,
                                        constructors ? (strcmp(scenario, "second-ctor-empty") == 0 ? empty_data : constructor_data) : actor_data},
                                       {139u, 2u, 0xC0997700u, 16u, NULL},
                                       {168u, 3u, 0xC1B224E0u, 16u, dol_data}, {168u, 2u, 0xC1B224D0u, 16u, NULL}};
    if (second) {
        host->rel_data = both_data;
        host->rel_data_count = strcmp(scenario, "second-missing-bss") == 0 ? 2u : 5u;
        for (u32 at = 1u; at < 5u; ++at) {
            assert(ppc_guest_alias_add(both_data[at].linked_start, 16u, NULL));
            memset(get_ram_ptr(&host->cpu, both_data[at].linked_start, 16u, NULL), 0xAA, 16u);
        }
    }
    if (link_case) {
        if (strcmp(scenario, "link-missing-data") != 0) {
            if (!second) {
                host->rel_data = &profile;
                host->rel_data_count = 1u;
            }
        }
        if (strcmp(scenario, "link-unmapped") != 0)
            assert(ppc_guest_alias_add(profile.linked_start, profile.size, profile.bytes));
        if (strcmp(scenario, "link-dirty-data") == 0)
            mem_write8(&host->cpu, profile.linked_start, 0u);
    }
    CPUState* cpu = &host->cpu;
    cpu->pc = 0x80240744u;
    cpu->lr = 0x80001004u;
    if (strcmp(scenario, "link-bad-return") == 0)
        cpu->lr |= 2u;
    if (strcmp(scenario, "link-wrong-global") == 0)
        mem_write32(cpu, 0x803F6A68u, 0x81231200u);
    cpu->gpr[3] = 0x80002000u;
    mem_write32(cpu, 0x8000201Cu, 0x80003000u);
    const char* name = strcmp(scenario, "not-found") == 0 ? "absent" :
                       strcmp(scenario, "case") == 0 ? "F_PC_PROFILE_LST" : "f_pc_profile_lst";
    memcpy(cpu->ram + 0x3000u, name, strlen(name) + 1u);
    fail_trace = strcmp(scenario, "trace") == 0;
    fail_alias = strcmp(scenario, "allocation") == 0;
    fail_link_trace = strcmp(scenario, "link-trace") == 0;
    if (strcmp(scenario, "overlap") == 0)
        assert(ppc_guest_alias_add(WW_REL_SCRATCH_BASE + 0x100u, 0x100u, NULL));
    if (strcmp(scenario, "bad-object") == 0)
        cpu->gpr[3] = 0x817FFFF0u;
    const bool success = strcmp(scenario, "raw") == 0 || strcmp(scenario, "compressed") == 0 ||
                         strcmp(scenario, "outer") == 0 || strcmp(scenario, "case") == 0;
    u8 object_before[0x20u];
    memcpy(object_before, cpu->ram + 0x2000u, sizeof(object_before));
    const u32 global_before = mem_read32(cpu, 0x803F6A68u);
    assert(ww_host_edge(host, cpu, cpu->pc));
    if (link_case) {
        assert(host->rel_images[0].guest == WW_REL_SCRATCH_BASE + 0x100u && host->rel_images[0].module_id == 1u);
        if (link_success) {
            assert(host->error == NULL && !host->edge_stopped && cpu->pc == cpu->lr && cpu->gpr[3] == 1u);
            assert(mem_read32(cpu, 0x80002010u) == host->rel_images[0].guest);
            assert(mem_read32(cpu, 0x803F6A68u) == profile.linked_start && host->rel_images[0].linked);
            assert(memcmp(object_before, cpu->ram + 0x2000u, 0x10u) == 0);
            assert(memcmp(object_before + 0x14u, cpu->ram + 0x2014u, 0xCu) == 0);
            if (strcmp(scenario, "link-repeat") == 0) {
                BwDvdIoStats before, after;
                bw_dvd_io_stats(&before);
                cpu->pc = 0x80240744u;
                cpu->gpr[3] = 0x80002000u;
                assert(ww_host_edge(host, cpu, cpu->pc));
                bw_dvd_io_stats(&after);
                assert(host->error == NULL && cpu->pc == cpu->lr && cpu->gpr[3] == 1u);
                assert(after.total_read_bytes == before.total_read_bytes && host->alias_count == 1u);
            }
            if (second) {
                second_module_case(host, scenario, ranges);
            }
        } else {
            assert(host->error != NULL && host->edge_stopped && !host->rel_images[0].linked);
            assert(cpu->pc == 0x80240744u && mem_read32(cpu, 0x803F6A68u) == global_before);
            assert(memcmp(object_before, cpu->ram + 0x2000u, sizeof(object_before)) == 0);
        }
        assert(cpu->ram_size == 0x01800000u);
        assert(constructors ? host->cycles.absolute_cycles <= 200u : host->cycles.absolute_cycles == 0u);
        ww_host_shutdown(host);
        assert(live_section_allocations == 0u);
        assert(host->alias_count == 0u && host->alias_bytes == 0u);
        for (u32 at = 0u; at < WW_REL_MAX_IMAGES; ++at)
            assert(host->rel_images[at].guest == 0u);
        free(host);
        return 0;
    }
    assert(host->error != NULL && host->edge_stopped && cpu->pc == 0x80240744u);
    assert(memcmp(object_before, cpu->ram + 0x2000u, sizeof(object_before)) == 0);
    assert(cpu->ram_size == 0x01800000u && host->cycles.absolute_cycles == 0u);
    if (success) {
        assert(host->rel_images[0].module_id == 1u && host->rel_images[0].bytes == 256u);
        assert(host->rel_images[0].guest == WW_REL_SCRATCH_BASE + 0x100u);
        assert(host->alias_count == 1u && host->alias_bytes == 256u);
        const u8* bytes = get_ram_ptr(cpu, host->rel_images[0].guest, 256u, NULL);
        assert(bytes != NULL && mem_read32(cpu, host->rel_images[0].guest) == 1u);
        assert(mem_read32(cpu, host->rel_images[0].guest + 0x1Cu) == 3u);
        for (u32 index = 0x80u; index < 0xA0u; ++index)
            assert(bytes[index] == (u8)(index * 7u + 3u));
        assert(get_ram_ptr(cpu, host->rel_images[0].guest | 0x40000000u, 256u, NULL) == bytes);
        assert(strstr(host->error, "link") != NULL);
    } else {
        assert(host->rel_images[0].guest == 0u && host->alias_count == 0u);
        if (strcmp(scenario, "overlap") != 0)
            assert(get_ram_ptr(cpu, WW_REL_SCRATCH_BASE + 0x100u, 256u, NULL) == NULL);
    }
    ww_host_shutdown(host);
    assert(live_section_allocations == 0u);
    assert(host->alias_count == 0u && host->alias_bytes == 0u && host->rel_images[0].guest == 0u);
    free(host);
    return 0;
}
