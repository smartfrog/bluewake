#include "mem_scenarios.h"
#ifdef __vita__
#include <psp2/kernel/sysmem.h>

MemSnapshot mem_snapshot(void) {
    SceKernelFreeMemorySizeInfo info = {.size = sizeof info};
    int result = sceKernelGetFreeMemorySize(&info);
    return (MemSnapshot){1, result, info.size, info.size_user, info.size_cdram, info.size_phycont};
}

static int allocate(unsigned pool, u32 bytes) {
    const SceKernelMemBlockType types[] = {SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW};
    const char* names[] = {"ww04-user", "ww04-cdram", "ww04-phycont"};
    if (pool >= 3)
        return -1;
    return sceKernelAllocMemBlock(names[pool], types[pool], bytes, NULL);
}

static int info(void* base, u32* mapped, u32* access, u32* type) {
    SceKernelMemBlockInfo info = {.size = sizeof info};
    int result = sceKernelGetMemBlockInfoByAddr(base, &info);
    if (result >= 0) {
        *mapped = info.mappedSize;
        *access = info.access;
        *type = info.type;
    }
    return result;
}

const MemSdkApi* mem_sdk_api(void) {
    static const MemSdkApi api = {allocate, sceKernelGetMemBlockBase, info,
                                  sceKernelFreeMemBlock, mem_snapshot};
    return &api;
}
#else
MemSnapshot mem_snapshot(void) {
    return (MemSnapshot){0};
}

const MemSdkApi* mem_sdk_api(void) {
    return NULL;
}
#endif
