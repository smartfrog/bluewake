#include "probe_support.h"
#include "dvd_io.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static bool fail_trace;
static bool fail_alias;
static bool fail_link_trace;

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

int main(int argc, char** argv) {
    assert(argc == 2);
    const char* scenario = argv[1];
    const bool link_case = strncmp(scenario, "link", 4u) == 0;
    const bool link_success = strcmp(scenario, "link") == 0 || strcmp(scenario, "link-repeat") == 0;
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    const StaticRecompRange ranges[] = {{0x80240744u, 0x80240748u}};
    const StaticRecompRelSection sections[] = {
        {.module_id = 1u, .section_index = 1u, .linked_start = WW_REL_SCRATCH_BASE,
         .size = strcmp(scenario, "full") == 0 ? WW_REL_SCRATCH_LIMIT - WW_REL_SCRATCH_BASE : 0x100u},
        {.module_id = 1u, .section_index = 4u, .linked_start = 0xC1F80178u, .size = 16u},
    };
    const StaticRecompRelModule rels[] = {{.module_id = 1u, .version = 0u, .section_count = link_case ? 5u : 2u,
                                         .section_info_offset = 0x4Cu, .file_size = 256u,
                                         .sections = sections, .num_sections = link_case ? 2u : 1u}};
    const StaticRecompModuleDesc module = {.code_ranges = ranges, .num_code_ranges = 1u,
                                         .rel_modules = rels, .num_rel_modules = 1u};
    host->module = &module;
    const u8 profile_bytes[16] = {0x80u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u};
    const BlueWakeRelData profile = {1u, 4u, 0xC1F80178u, sizeof(profile_bytes), profile_bytes};
    if (link_case) {
        if (strcmp(scenario, "link-missing-data") != 0) {
            host->rel_data = &profile;
            host->rel_data_count = 1u;
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
        assert(host->rel_image.guest == WW_REL_SCRATCH_BASE + 0x100u && host->rel_image.module_id == 1u);
        if (link_success) {
            assert(host->error == NULL && !host->edge_stopped && cpu->pc == cpu->lr && cpu->gpr[3] == 1u);
            assert(mem_read32(cpu, 0x80002010u) == host->rel_image.guest);
            assert(mem_read32(cpu, 0x803F6A68u) == profile.linked_start && host->rel_image.linked);
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
        } else {
            assert(host->error != NULL && host->edge_stopped && !host->rel_image.linked);
            assert(cpu->pc == 0x80240744u && mem_read32(cpu, 0x803F6A68u) == global_before);
            assert(memcmp(object_before, cpu->ram + 0x2000u, sizeof(object_before)) == 0);
        }
        assert(cpu->ram_size == 0x01800000u && host->cycles.absolute_cycles == 0u);
        ww_host_shutdown(host);
        free(host);
        return 0;
    }
    assert(host->error != NULL && host->edge_stopped && cpu->pc == 0x80240744u);
    assert(memcmp(object_before, cpu->ram + 0x2000u, sizeof(object_before)) == 0);
    assert(cpu->ram_size == 0x01800000u && host->cycles.absolute_cycles == 0u);
    if (success) {
        assert(host->rel_image.module_id == 1u && host->rel_image.bytes == 256u);
        assert(host->rel_image.guest == WW_REL_SCRATCH_BASE + 0x100u);
        assert(host->alias_count == 1u && host->alias_bytes == 256u);
        const u8* bytes = get_ram_ptr(cpu, host->rel_image.guest, 256u, NULL);
        assert(bytes != NULL && mem_read32(cpu, host->rel_image.guest) == 1u);
        assert(mem_read32(cpu, host->rel_image.guest + 0x1Cu) == 3u);
        for (u32 index = 0x80u; index < 0xA0u; ++index)
            assert(bytes[index] == (u8)(index * 7u + 3u));
        assert(get_ram_ptr(cpu, host->rel_image.guest | 0x40000000u, 256u, NULL) == bytes);
        assert(strstr(host->error, "link") != NULL);
    } else {
        assert(host->rel_image.guest == 0u && host->alias_count == 0u);
        if (strcmp(scenario, "overlap") != 0)
            assert(get_ram_ptr(cpu, WW_REL_SCRATCH_BASE + 0x100u, 256u, NULL) == NULL);
    }
    ww_host_shutdown(host);
    assert(host->alias_count == 0u && host->alias_bytes == 0u && host->rel_image.guest == 0u);
    free(host);
    return 0;
}
