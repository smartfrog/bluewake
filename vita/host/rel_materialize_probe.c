#include "probe_support.h"
#include "dvd_io.h"
#include "dispatch_loop.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static bool fail_trace;
static bool fail_alias;
static bool fail_link_trace;
static u32 dispatched_address;
static u32 loop_successor;
static u32 loop_dispatches;
static u32 loop_instructions;

static int loop_chunk_dispatch(CPUState* cpu, u32 address) {
    loop_dispatches++;
    if (cpu->host_call && ppc_host_call(cpu, address))
        return 1;
    loop_instructions++;
    cpu->downcount--;
    cpu->pc = loop_successor;
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

void* __wrap_calloc(size_t count, size_t size) {
    if (fail_alias && count == 1u && size == 256u)
        return NULL;
    return __real_calloc(count, size);
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

static void second_module_case(WwHost* host, const char* scenario) {
    CPUState* cpu = &host->cpu;
    const bool rejected = strstr(scenario, "bad-") != NULL || strstr(scenario, "reloc-") != NULL ||
                          strstr(scenario, "branch-") != NULL || strstr(scenario, "lifecycle-") != NULL ||
                          strcmp(scenario, "second-dependency-unlinked") == 0 ||
                          strcmp(scenario, "second-dependency-unloaded") == 0 ||
                          strcmp(scenario, "second-no-code") == 0 || strcmp(scenario, "second-missing-bss") == 0;
    const WwRelImage profile = host->rel_images[0];
    const bool dependency_live = strcmp(scenario, "second-dependency-live") == 0;
    if (dependency_live || strcmp(scenario, "second-dependency-unloaded") == 0) {
        request_rel(host, 0x80002200u, 0x80003200u, "ww34_fixture_b");
        assert(host->error == NULL && mem_read32(cpu, 0x80002210u) != 0u);
        if (!dependency_live) {
            assert(ww_host_unlink_rel(host, 0x80002200u) == NULL);
            assert(ww_host_collect_rels(host) == NULL && host->alias_count == 1u);
        }
    }
    request_rel(host, 0x80002100u, 0x80003100u, "ww34_fixture_a");
    assert(memcmp(&profile, &host->rel_images[0], sizeof(profile)) == 0);
    assert(mem_read32(cpu, 0x80002010u) == profile.guest);
    assert(mem_read32(cpu, 0x803F6A68u) == 0xC1F80178u);
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
        assert(mem_read32(cpu, 0x80002110u) == 0u && host->alias_count == 1u && host->alias_bytes == 256u);
        assert(ww_host_rel_owned(host, 0x80002100u) == NULL);
        assert(mem_read8(cpu, 0xC0997700u) == 0xAAu && mem_read8(cpu, 0xC0997710u) == 0xAAu);
        return;
    }
    assert(host->error == NULL && cpu->pc == cpu->lr && cpu->gpr[3] == 1u);
    const u32 actor = mem_read32(cpu, 0x80002110u);
    assert(actor != 0u && actor != profile.guest && host->alias_count == (dependency_live ? 3u : 2u));
    assert(host->alias_bytes == (dependency_live ? 768u : 512u));
    for (u32 offset = 0u; offset < 16u; ++offset)
        assert(mem_read8(cpu, 0xC0997700u + offset) == 0u);
    assert(mem_read32(cpu, 0xC0997710u) == (dependency_live ? 0xC1B224D0u : 0x80001000u));
    assert(ww_host_rel_address(host, actor + 0x80u) == 0xC09900F4u);
    assert(ww_host_rel_address(host, (actor + 0x84u) | 0x40000000u) == 0xC09900F8u);
    mem_write8(cpu, 0xC0997700u, 0xBBu);
    if (strcmp(scenario, "second-repeat") == 0) {
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

int main(int argc, char** argv) {
    assert(argc == 2);
    const char* scenario = argv[1];
    const bool second = strncmp(scenario, "second", 6u) == 0;
    const bool link_case = strncmp(scenario, "link", 4u) == 0 || second;
    const bool link_success = strcmp(scenario, "link") == 0 || strcmp(scenario, "link-repeat") == 0 || second;
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    const StaticRecompRange ranges[] = {{0x80240744u, 0x80240748u}, {0xC09900F4u, 0xC0990114u},
                                      {0xC1B100F4u, 0xC1B10114u}};
    const StaticRecompRange chunks[] = {{0xC09900F4u, 0xC0990114u}, {0xC1B100F4u, 0xC1B10114u}};
    const u64 hashes[] = {actor_code_hash(scenario), actor_code_hash("")};
    const StaticRecompRelSection sections[] = {
        {.module_id = 1u, .section_index = 1u, .linked_start = WW_REL_SCRATCH_BASE,
         .size = strcmp(scenario, "full") == 0 ? WW_REL_SCRATCH_LIMIT - WW_REL_SCRATCH_BASE : 0x100u},
        {.module_id = 1u, .section_index = 4u, .linked_start = 0xC1F80178u, .size = 16u},
    };
    const StaticRecompRelSection actor_sections[] = {
        {139u, 1u, 0xC09900F4u, 32u}, {139u, 2u, 0xC0997700u, 16u}, {139u, 3u, 0xC0997710u, 16u}};
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
                                          .num_code_ranges = second && strcmp(scenario, "second-no-code") != 0 ? 3u : 1u,
                                          .rel_modules = rels, .num_rel_modules = second ? 3u : 1u,
                                          .chunk_ranges = chunks, .num_chunk_ranges = 2u, .chunk_hashes = hashes,
                                          .dispatch = strncmp(scenario, "second-loop-", 12u) == 0 ? composite_dispatch : rel_dispatch};
    host->module = &module;
    const u8 profile_bytes[16] = {0x80u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u};
    const BlueWakeRelData profile = {1u, 4u, 0xC1F80178u, sizeof(profile_bytes), profile_bytes};
    const u8 dol_data[16] = {0x80u, 0u, 0x10u, 0u};
    const bool dependency_case = strncmp(scenario, "second-dependency-", 18u) == 0;
    const u8 actor_data[16] = {dependency_case ? 0xC1u : 0x80u, dependency_case ? 0xB2u : 0u,
                              dependency_case ? 0x24u : 0x10u, dependency_case ? 0xD0u : 0u};
    const BlueWakeRelData both_data[] = {profile, {139u, 3u, 0xC0997710u, 16u, actor_data},
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
                second_module_case(host, scenario);
            }
        } else {
            assert(host->error != NULL && host->edge_stopped && !host->rel_images[0].linked);
            assert(cpu->pc == 0x80240744u && mem_read32(cpu, 0x803F6A68u) == global_before);
            assert(memcmp(object_before, cpu->ram + 0x2000u, sizeof(object_before)) == 0);
        }
        assert(cpu->ram_size == 0x01800000u && host->cycles.absolute_cycles == 0u);
        ww_host_shutdown(host);
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
    assert(host->alias_count == 0u && host->alias_bytes == 0u && host->rel_images[0].guest == 0u);
    free(host);
    return 0;
}
