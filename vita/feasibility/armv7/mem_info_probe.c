#include "mem_scenarios.h"
#include <stdlib.h>
#include <string.h>

#define main mem_info_host_main
#include "platform.c"
#undef main

#define __vita__
#define mem_sdk_api native_mem_sdk_api
#define mem_snapshot native_mem_snapshot
#include "mem_platform.c"
#undef mem_snapshot
#undef mem_sdk_api
#undef __vita__

static u8* backing;
static u32 backing_size;
static unsigned allocations;
static unsigned releases;
static unsigned written;
static bool bad_size;

SceUID sceKernelAllocMemBlock(const char* name, SceKernelMemBlockType type, SceSize size,
                             SceKernelAllocMemBlockOpt* options) {
    (void)name;
    (void)type;
    (void)options;
    if (backing)
        return -42;
    backing = aligned_alloc(size, size);
    if (!backing)
        return -43;
    backing_size = size;
    memset(backing, 0x5c, size);
    allocations++;
    return allocations;
}

int sceKernelGetMemBlockBase(SceUID uid, void** base) {
    if (uid != (SceUID)allocations || !backing)
        return -44;
    *base = backing;
    return 0;
}

int sceKernelGetMemBlockInfoByAddr(void* base, SceKernelMemBlockInfo* info) {
    if (base != backing || info->size != sizeof *info)
        return -45;
    info->size = sizeof *info - bad_size;
    info->mappedBase = base;
    info->mappedSize = backing_size;
    info->access = SCE_KERNEL_MEMORY_ACCESS_R | SCE_KERNEL_MEMORY_ACCESS_W;
    const SceKernelMemBlockType types[] = {SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW};
    info->type = types[allocations - 1];
    return 0;
}

int sceKernelFreeMemBlock(SceUID uid) {
    if (uid != (SceUID)allocations || !backing)
        return -46;
    for (u32 i = 0; i < backing_size; ++i)
        written += backing[i] != 0x5c;
    free(backing);
    backing = NULL;
    releases++;
    return 0;
}

int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo* info) {
    if (info->size != sizeof *info)
        return -47;
    info->size_user = 12345;
    info->size_cdram = 23456;
    info->size_phycont = 34567;
    return 0;
}

const MemSdkApi* __wrap_mem_sdk_api(void) {
    return native_mem_sdk_api();
}

int fixture_run(int argc, char** argv);

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    bad_size = strcmp(argv[1], "valid") != 0;
    if (strcmp(argv[1], "report") == 0) {
        int result = fixture_run(1, argv);
        printf("API result=0 returned=%zu required=%zu allocations=%u releases=%u written=%u live=%u exit=%d\n",
               sizeof(SceKernelMemBlockInfo) - 1, sizeof(SceKernelMemBlockInfo),
               allocations, releases, written, backing != NULL, result);
        return result;
    }
    MemSdk result;
    int error = mem_sdk(&result, native_mem_sdk_api());
    printf("API result=%d returned=%zu required=%zu allocations=%u releases=%u written=%u live=%u semantic=%d\n",
           result.blocks[0].info_result, sizeof(SceKernelMemBlockInfo) - bad_size,
           sizeof(SceKernelMemBlockInfo), allocations, releases, written, result.live, error);
    if (!bad_size)
        return error || allocations != 3 || releases != 3 || !written || result.live;
    return !error || result.blocks[0].info_result != 0 || allocations != 1 || releases != 1 ||
           written || result.live || result.blocks[0].owner.touched;
}
