#include "mem_scenarios.h"
#include <stdlib.h>
#include <string.h>

void* __real_calloc(size_t count, size_t size);
void __real_free(void* pointer);
static unsigned live;
static unsigned calls;
static unsigned fail_at;
static unsigned pool_index;
static unsigned sdk_live;
static unsigned sdk_allocations;
static const char* scenario;
static void* sdk_backing;
static u32 sdk_size;

static bool fails(const char* operation) {
    char expected[32];
    snprintf(expected, sizeof expected, "sdk-%s-%u", operation, pool_index);
    return strcmp(scenario, expected) == 0;
}

static int allocate(unsigned pool, u32 bytes) {
    pool_index = pool;
    if (sdk_live || fails("alloc"))
        return -42;
    sdk_backing = aligned_alloc(bytes, bytes);
    if (!sdk_backing)
        return -43;
    sdk_live++;
    sdk_allocations++;
    sdk_size = bytes;
    return 100 + pool;
}

static int base(int uid, void** pointer) {
    if (uid != 100 + (int)pool_index || fails("base"))
        return -44;
    *pointer = sdk_backing;
    return 0;
}

static int info(void* pointer, u32* mapped, u32* access, u32* type, u32* returned_size) {
    if (pointer != sdk_backing || fails("info"))
        return -45;
    *mapped = sdk_size;
    *returned_size = 0x18u;
    *access = 6;
    const u32 types[] = {0x0C20D060u, 0x09408060u, 0x0C80D060u};
    *type = types[pool_index];
    return 0;
}

static int release(int uid) {
    if (uid != 100 + (int)pool_index || fails("free"))
        return -46;
    __real_free(sdk_backing);
    sdk_backing = NULL;
    sdk_live--;
    return 0;
}

static MemSnapshot snapshot(void) {
    if (strcmp(scenario, "sdk-snapshot-unknown") == 0)
        return (MemSnapshot){0};
    return (MemSnapshot){1, 0, 16, 12345, 23456, 34567};
}

void* __wrap_calloc(size_t count, size_t size) {
    if (++calls == fail_at)
        return NULL;
    void* pointer = __real_calloc(count, size);
    if (pointer)
        live++;
    return pointer;
}

void __wrap_free(void* pointer) {
    if (pointer)
        live--;
    __real_free(pointer);
}

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    scenario = argv[1];
    if (strncmp(scenario, "sdk-", 4) == 0) {
        const MemSdkApi api = {allocate, base, info, release, snapshot, 0x18u};
        MemSdk result;
        int error = mem_sdk(&result, &api);
        if (strcmp(scenario, "sdk-healthy") == 0)
            return error || sdk_live || result.live || result.peak != 1048576 ||
                   result.max_blocks != 1 || sdk_allocations != 3;
        if (strncmp(scenario, "sdk-free-", 9) == 0) {
            int failed = !error || sdk_live != 1 || result.live != sdk_size ||
                         sdk_allocations != pool_index + 1;
            __real_free(sdk_backing);
            return failed;
        }
        return !error || sdk_live || result.live;
    }
    if (strncmp(argv[1], "allocate-", 9) == 0) {
        fail_at = (unsigned)atoi(argv[1] + 9);
        MemReal result;
        return !mem_real(&result) || live || result.live;
    }
    if (strcmp(argv[1], "overflow") == 0) {
        u32 output = 7;
        return mem_add(UINT32_MAX, 1, &output) || output != 7 ||
               mem_add(0, 1, NULL);
    }
    if (strcmp(argv[1], "pattern-invalid") == 0) {
        u8 data[64] = {0};
        u32 touched = 0;
        return mem_pattern(NULL, 64, &touched) || mem_pattern(data, 63, &touched) ||
               mem_pattern(data, 64, NULL);
    }
    if (strcmp(argv[1], "pattern-corrupt") == 0) {
        u8 data[4096];
        u32 touched;
        if (!mem_pattern(data, sizeof data, &touched) || touched != 128)
            return 1;
        data[4095] ^= 1;
        return mem_verify(data, sizeof data);
    }
    if (strcmp(argv[1], "snapshot-invalid") == 0)
        return mem_snapshot_valid((MemSnapshot){2, 0, 16, 1, 2, 3});
    if (strcmp(argv[1], "snapshot-error") == 0)
        return mem_snapshot_valid((MemSnapshot){1, -42, 16, 1, 2, 3});
    if (strcmp(argv[1], "snapshot-negative") == 0)
        return mem_snapshot_valid((MemSnapshot){1, 0, 16, 1, -2, 3});
    if (strcmp(argv[1], "snapshot-size") == 0)
        return mem_snapshot_valid((MemSnapshot){1, 0, 4, 1, 2, 3});
    if (strcmp(argv[1], "real") != 0)
        return 1;
    MemReal result;
    return mem_real(&result) || live || result.live || result.peak != 58736704u ||
           result.owners[3].views != 2 || result.owners[3].mapped != 64;
}
