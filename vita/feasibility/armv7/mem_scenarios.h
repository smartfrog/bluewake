#ifndef WW_MEM_SCENARIOS_H
#define WW_MEM_SCENARIOS_H

#include "fixture.h"

typedef struct MemSnapshot {
    int available;
    int result;
    int size;
    int user;
    int cdram;
    int phycont;
} MemSnapshot;

typedef struct MemOwner {
    u32 requested;
    u32 mapped;
    u32 touched;
    u32 views;
} MemOwner;

typedef struct MemReal {
    MemOwner owners[4];
    MemSnapshot snapshots[3];
    u32 peak;
    u32 live;
    int pattern;
    int alias;
    int bounds;
} MemReal;

typedef struct MemSdkApi {
    int (*allocate)(unsigned pool, u32 bytes);
    int (*base)(int uid, void** base);
    int (*info)(void* base, u32* mapped, u32* access, u32* type, u32* returned_size);
    int (*release)(int uid);
    MemSnapshot (*snapshot)(void);
    u32 info_size_required;
} MemSdkApi;

typedef struct MemSdkBlock {
    MemOwner owner;
    int uid;
    int base_result;
    int info_result;
    u32 info_size;
    u32 info_required;
    int free_result;
    int pattern;
    u32 access;
    u32 type;
    MemSnapshot snapshots[3];
} MemSdkBlock;

typedef struct MemSdk {
    MemSdkBlock blocks[3];
    u32 peak;
    u32 max_blocks;
    u32 live;
    int available;
} MemSdk;

bool mem_add(u32 left, u32 right, u32* result);
bool mem_pattern(u8* data, u32 size, u32* touched);
bool mem_verify(const u8* data, u32 size);
bool mem_snapshot_valid(MemSnapshot snapshot);
MemSnapshot mem_snapshot(void);
const MemSdkApi* mem_sdk_api(void);
int mem_real(MemReal* result);
int mem_sdk(MemSdk* result, const MemSdkApi* api);

#endif
