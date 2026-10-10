#include "mem_scenarios.h"
#include "gxruntime/guest_memory.h"
#include <limits.h>
#include <string.h>

bool mem_add(u32 left, u32 right, u32* result) {
    if (!result || right > UINT32_MAX - left)
        return false;
    *result = left + right;
    return true;
}

bool mem_snapshot_valid(MemSnapshot snapshot) {
    return snapshot.available == 0 || (snapshot.available == 1 && snapshot.result >= 0 && snapshot.size == 16 &&
           snapshot.user >= 0 && snapshot.cdram >= 0 && snapshot.phycont >= 0);
}

static u32 next_offset(u32 offset, u32 size) {
    return size - offset <= 4096 ? size - 64 : offset + 4096;
}

bool mem_verify(const u8* data, u32 size) {
    if (!data || size < 64 || size % 64 != 0)
        return false;
    const volatile u8* observed = data;
    for (u32 offset = 0; offset < size; offset = next_offset(offset, size)) {
        for (u32 i = 0; i < 64; ++i)
            if (observed[offset + i] != (u8)((offset >> 12) ^ i ^ 0xa5u))
                return false;
        if (offset == size - 64)
            break;
    }
    return true;
}

bool mem_pattern(u8* data, u32 size, u32* touched) {
    if (!data || !touched || size < 64 || size % 64 != 0)
        return false;
    *touched = 0;
    volatile u8* written = data;
    for (u32 offset = 0; offset < size; ) {
        for (u32 i = 0; i < 64; ++i)
            written[offset + i] = (u8)((offset >> 12) ^ i ^ 0xa5u);
        if (!mem_add(*touched, 64, touched))
            return false;
        if (offset == size - 64)
            break;
        offset = next_offset(offset, size);
    }
    return mem_verify(data, size);
}

static int acquired(MemReal* result, unsigned index, u32 size, u32 views) {
    result->owners[index].mapped = size;
    result->owners[index].views = views;
    if (!mem_add(result->live, size, &result->live))
        return 1;
    if (result->live > result->peak)
        result->peak = result->live;
    return 0;
}

int mem_real(MemReal* result) {
    if (!result)
        return 1;
    memset(result, 0, sizeof *result);
    DolGuestMemoryConfig config = dol_guest_memory_default_config();
    result->owners[0].requested = GC_MAIN_RAM_SIZE;
    result->owners[1].requested = config.vm_size;
    result->owners[2].requested = config.locked_cache_size;
    result->owners[3].requested = 64;
    result->snapshots[0] = mem_snapshot();
    CPUState cpu;
    DolGuestMemory memory = {0};
    bool cpu_ready = cpu_init(&cpu);
    int error = 1;
    if (!cpu_ready)
        goto done;
    if (acquired(result, 0, cpu.ram_size, 1) || !dol_guest_memory_init(&memory, NULL))
        goto done;
    if (acquired(result, 1, memory.config.vm_size, 1) ||
        acquired(result, 2, memory.config.locked_cache_size, 1))
        goto done;
    u8* backing = NULL;
    u8* resolved = NULL;
    if (!ppc_guest_alias_add(0xC1550000u, 64, NULL) ||
        !ppc_guest_alias_get_storage(0xC1550000u, 64, &backing))
        goto done;
    if (acquired(result, 3, 64, 1) || !ppc_guest_alias_add_shared(0x81550000u, 64, backing))
        goto done;
    result->owners[3].views = 2;
    result->snapshots[1] = mem_snapshot();
    if (!mem_pattern(cpu.ram, cpu.ram_size, &result->owners[0].touched) ||
        !mem_pattern(memory.vm, memory.config.vm_size, &result->owners[1].touched) ||
        !mem_pattern(memory.locked_cache, memory.config.locked_cache_size, &result->owners[2].touched) ||
        !mem_pattern(backing, 64, &result->owners[3].touched))
        goto done;
    result->pattern = 1;
    mem_write32(&cpu, 0x81550000u, 0x89ABCDEFu);
    if (mem_read32(&cpu, 0xC1550000u) != 0x89ABCDEFu ||
        get_ram_ptr(&cpu, 0xC1550000u, 4, NULL) != backing ||
        backing[0] != 0x89 || backing[3] != 0xEF)
        goto done;
    mem_write32(&cpu, 0xC155003Cu, 0x01234567u);
    if (mem_read32(&cpu, 0x8155003Cu) != 0x01234567u)
        goto done;
    result->alias = 1;
    DolGuestAddressResolver resolver;
    DolGuestResolvedRange range;
    dol_guest_address_resolver_init(&resolver, &memory, &cpu);
    u64 value;
    if (!ppc_guest_alias_resolve(0xC155003Cu, 4, &resolved, NULL) || resolved != backing + 60 ||
        ppc_guest_alias_resolve(0xC155003Du, 4, &resolved, NULL) ||
        dol_guest_address_resolver_resolve(&resolver, GC_RAM_BASE + cpu.ram_size - 1, 2,
            DOL_GUEST_ADDRESS_VIRTUAL, DOL_GUEST_RESOURCE_GENERIC, &range) ||
        dol_guest_memory_read(&memory, config.vm_base + config.vm_size - 1, 2, &value) ||
        dol_guest_memory_write(&memory, config.locked_cache_base + config.locked_cache_size - 1, 2, 0))
        goto done;
    result->bounds = 1;
    error = 0;
done:
    ppc_guest_alias_clear();
    dol_guest_memory_shutdown(&memory);
    if (cpu_ready)
        cpu_free(&cpu);
    result->live = 0;
    result->snapshots[2] = mem_snapshot();
    for (unsigned i = 0; i < 3; ++i)
        error |= !mem_snapshot_valid(result->snapshots[i]);
    return error;
}

int mem_sdk(MemSdk* result, const MemSdkApi* api) {
    if (!result)
        return 1;
    memset(result, 0, sizeof *result);
    const u32 sizes[] = {4096, 262144, 1048576};
    const u32 types[] = {0x0C20D060u, 0x09408060u, 0x0C80D060u};
    for (unsigned i = 0; i < 3; ++i) {
        result->blocks[i].owner.requested = sizes[i];
        result->blocks[i].uid = -1;
        result->blocks[i].base_result = result->blocks[i].info_result = -1;
        result->blocks[i].info_required = api ? api->info_size_required : 0;
        result->blocks[i].free_result = -1;
    }
    if (!api)
        return 0;
    if (!api->allocate || !api->base || !api->info || !api->release || !api->snapshot || !api->info_size_required)
        return 1;
    result->available = 1;
    int error = 0;
    for (unsigned i = 0; i < 3; ++i) {
        MemSdkBlock* block = &result->blocks[i];
        block->snapshots[0] = api->snapshot();
        block->uid = api->allocate(i, sizes[i]);
        if (block->uid < 0) {
            error = 1;
        } else {
            result->live = sizes[i];
            result->max_blocks = 1;
            if (result->live > result->peak)
                result->peak = result->live;
            void* base = NULL;
            block->base_result = api->base(block->uid, &base);
            if (block->base_result < 0 || !base || (uintptr_t)base % sizes[i] != 0) {
                error = 1;
            } else {
                block->info_result = api->info(base, &block->owner.mapped, &block->access, &block->type, &block->info_size);
                if (block->info_result < 0 || block->info_size != block->info_required || block->owner.mapped != sizes[i] ||
                    (block->access & 6u) != 6u || block->type != types[i]) {
                    error = 1;
                } else {
                    block->owner.views = 1;
                    block->pattern = mem_pattern(base, sizes[i], &block->owner.touched);
                    error |= !block->pattern;
                }
            }
            block->snapshots[1] = api->snapshot();
            block->free_result = api->release(block->uid);
            if (block->free_result < 0) {
                error = 1;
                block->snapshots[2] = api->snapshot();
                break;
            }
            result->live = 0;
        }
        block->snapshots[2] = api->snapshot();
        for (unsigned j = 0; j < 3; ++j)
            error |= !block->snapshots[j].available || !mem_snapshot_valid(block->snapshots[j]);
        if (error)
            break;
    }
    return error;
}
