/* BlueWake composite module export glue (P3).
 * Adapts the ModernGekko module-template export for the DOL+REL composite.
 */
#include "generated_composite.h"
#include "StaticRecompABI.h"
#include "module_cpu_contract.h"
#include "dispatch_loop.h"
#include "module_exports.h"

extern void ppc_set_mem_write_journal(PPCMemWriteJournal fn, void* user);

/* File-gated audit of the pc->function cache in generated_composite.h. The
 * guest has no usable environment, so the gate is a fixture file. When it is
 * absent the hot path pays one load and a never-taken branch. */
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef BW_FN_CACHE_PROBE_FILE
#define BW_FN_CACHE_PROBE_FILE "ux0:data/bluewake-ww-fixture/ww05.fn-cache-probe"
#endif
#define BW_FN_CACHE_SIZE 4096u
#define BW_FN_CACHE_TARGET 0x801296E0u

int bw_fn_cache_probe_enabled = -1;

static u32 s_fn_probe_low;
static u32 s_fn_probe_high;
static unsigned s_fn_probe_hits;
static unsigned s_fn_probe_target_hits;
static unsigned s_fn_probe_fills;
static unsigned s_fn_probe_target_fills;
static unsigned s_fn_probe_calls;
static unsigned s_fn_probe_target_calls;
static u32 s_fn_probe_target_expected;
static bool s_fn_probe_target_mismatch;
static bool s_fn_probe_stopped;

static bool bw_fn_cache_probe_text(u32 value)
{
    return value >= s_fn_probe_low && value <= s_fn_probe_high;
}

static void bw_fn_cache_probe_slots(const u32* pcs, const DolRecompFunction* fns,
                                    unsigned index)
{
    for (int delta = -4; delta <= 4; ++delta) {
        const unsigned i = (index + (unsigned)delta) & (BW_FN_CACHE_SIZE - 1u);
        fprintf(stderr, "[fn-cache] slot index=0x%03X pc=%08X fn=%p\n",
                i, pcs[i], (const void*)fns[i]);
    }
}

static bool bw_fn_cache_probe_init(void)
{
    struct stat info;
    bw_fn_cache_probe_enabled =
        lstat(BW_FN_CACHE_PROBE_FILE, &info) == 0 && S_ISREG(info.st_mode);
    if (!bw_fn_cache_probe_enabled)
        return false;
    s_fn_probe_low = 0xFFFFFFFFu;
    for (unsigned i = 0; i < DOLRECOMP_CHUNK_COUNT; ++i) {
        const u32 fn = (u32)s_dolrecomp_chunk_fns[i];
        if (fn < s_fn_probe_low)
            s_fn_probe_low = fn;
        if (fn > s_fn_probe_high)
            s_fn_probe_high = fn;
        if (s_dolrecomp_chunk_starts[i] == BW_FN_CACHE_TARGET)
            s_fn_probe_target_expected = fn;
    }
    fprintf(stderr,
            "[fn-cache] enabled file=%s chunks=%u text=[%08X,%08X] target=%08X "
            "expected=%08X\n",
            BW_FN_CACHE_PROBE_FILE, DOLRECOMP_CHUNK_COUNT, s_fn_probe_low,
            s_fn_probe_high, BW_FN_CACHE_TARGET, s_fn_probe_target_expected);
    fflush(stderr);
    return true;
}

void bw_fn_cache_probe_hit(u32 address, unsigned index, DolRecompFunction value,
                           const u32* pcs, const DolRecompFunction* fns)
{
    if (bw_fn_cache_probe_enabled < 0 && !bw_fn_cache_probe_init())
        return;
    if (!bw_fn_cache_probe_enabled || s_fn_probe_stopped)
        return;
    ++s_fn_probe_hits;
    const u32 raw = (u32)value;
    if (!bw_fn_cache_probe_text(raw)) {
        s_fn_probe_stopped = true;
        fprintf(stderr,
                "[fn-cache] bad-hit pc=%08X index=0x%03X raw=%08X text=0 range=[%08X,%08X]\n",
                address, index, raw, s_fn_probe_low, s_fn_probe_high);
        bw_fn_cache_probe_slots(pcs, fns, index);
        fprintf(stderr, "[fn-cache] stop bad-hit hits=%u fills=%u calls=%u\n",
                s_fn_probe_hits, s_fn_probe_fills, s_fn_probe_calls);
        fflush(stderr);
        _exit(70);
    }
    if (s_fn_probe_hits <= 8u ||
        (address == BW_FN_CACHE_TARGET && ++s_fn_probe_target_hits <= 8u))
        fprintf(stderr,
                "[fn-cache] hit n=%u pc=%08X index=0x%03X raw=%08X text=1%s\n",
                s_fn_probe_hits, address, index, raw,
                address == BW_FN_CACHE_TARGET ? " target" : "");
    if (address == BW_FN_CACHE_TARGET && raw != s_fn_probe_target_expected &&
        !s_fn_probe_target_mismatch) {
        s_fn_probe_target_mismatch = true;
        fprintf(stderr,
                "[fn-cache] target-mismatch pc=%08X index=0x%03X raw=%08X expected=%08X\n",
                address, index, raw, s_fn_probe_target_expected);
    }
    if ((s_fn_probe_hits & 0xFFFFFu) == 0u)
        fprintf(stderr, "[fn-cache] count hits=%u target=%u fills=%u calls=%u\n",
                s_fn_probe_hits, s_fn_probe_target_hits, s_fn_probe_fills,
                s_fn_probe_calls);
}

void bw_fn_cache_probe_fill(u32 address, unsigned index, DolRecompFunction value,
                            const void* ret, const u32* pcs,
                            const DolRecompFunction* fns)
{
    if (bw_fn_cache_probe_enabled < 0 && !bw_fn_cache_probe_init())
        return;
    if (!bw_fn_cache_probe_enabled || s_fn_probe_stopped)
        return;
    ++s_fn_probe_fills;
    const u32 raw = (u32)value;
    if (!bw_fn_cache_probe_text(raw)) {
        s_fn_probe_stopped = true;
        fprintf(stderr, "[fn-cache] bad-fill pc=%08X index=0x%03X raw=%08X ret=%p\n",
                address, index, raw, ret);
        bw_fn_cache_probe_slots(pcs, fns, index);
        fprintf(stderr, "[fn-cache] stop bad-fill hits=%u fills=%u calls=%u\n",
                s_fn_probe_hits, s_fn_probe_fills, s_fn_probe_calls);
        fflush(stderr);
        _exit(70);
    }
    if (s_fn_probe_fills <= 8u ||
        (address == BW_FN_CACHE_TARGET && ++s_fn_probe_target_fills <= 8u))
        fprintf(stderr, "[fn-cache] fill n=%u pc=%08X index=0x%03X raw=%08X ret=%p\n",
                s_fn_probe_fills, address, index, raw, ret);
}

void bw_fn_cache_probe_call(u32 address, DolRecompFunction value)
{
    if (bw_fn_cache_probe_enabled < 0 && !bw_fn_cache_probe_init())
        return;
    if (!bw_fn_cache_probe_enabled || s_fn_probe_stopped)
        return;
    ++s_fn_probe_calls;
    const u32 raw = (u32)value;
    if (!bw_fn_cache_probe_text(raw)) {
        s_fn_probe_stopped = true;
        fprintf(stderr, "[fn-cache] bad-call pc=%08X raw=%08X text=0\n", address, raw);
        fflush(stderr);
        _exit(70);
    }
    if (s_fn_probe_calls <= 8u ||
        (address == BW_FN_CACHE_TARGET && ++s_fn_probe_target_calls <= 8u))
        fprintf(stderr, "[fn-cache] call n=%u pc=%08X fn=%08X\n",
                s_fn_probe_calls, address, raw);
}

unsigned dolrecomp_call_depth = 0;
static BluewakeEdgeServiceFn s_edge_service;
static void* s_edge_service_user;
#if defined(BLUEWAKE_DIRECT_CALLS)
/* Use the same selected chunk table and lookup as ordinary dispatch, including
 * mod variants. The donor's direct calls must not bypass player options. */
void (**const bw_chunk_fns)(CPUState*) = s_dolrecomp_chunk_fns;
DolRecompFunction bw_find_chunk(u32 address) {
    return dolrecomp_find_original(address);
}
#endif

#ifdef BLUEWAKE_NATIVE_MATH_CACHED
#include "native_math.h"
static void native_matrix_copy(CPUState* cpu) {
    if (!bluewake_native_math_try(cpu, 0x8030D0C8u)) func_803096E0(cpu);
}
static void native_matrix_concat(CPUState* cpu) {
    if (!bluewake_native_math_try(cpu, 0x8030D0FCu)) func_803096E0(cpu);
}
static void native_matrix_vec(CPUState* cpu) {
    if (!bluewake_native_math_try(cpu, 0x8030DA44u)) func_8030D6E0(cpu);
}
static void native_matrix_array(CPUState* cpu) {
    if (!bluewake_native_math_try(cpu, 0x8030DA98u)) func_8030D6E0(cpu);
}
static DolRecompFunction bluewake_native_math_find(u32 address) {
    /* Cached wrappers always recheck the host handshake, including after disable. */
    switch (address) {
    case 0x8030D0C8u: return native_matrix_copy;
    case 0x8030D0FCu: return native_matrix_concat;
    case 0x8030DA44u: return native_matrix_vec;
    case 0x8030DA98u: return native_matrix_array;
    default: return NULL;
    }
}
#endif

#if defined(BLUEWAKE_DIRECT_CALLS)
int bw_native_call(CPUState* cpu, u32 address) {
#ifdef BLUEWAKE_NATIVE_MATH_CACHED
    return bluewake_native_math_try(cpu, address);
#else
    (void)cpu; (void)address;
    return 0;
#endif
}
#endif

/* The x86-64-v3 dispatch clones come only from DolRecomp's LLVM object
 * backend; the C backend the Builder uses emits none, so an x86-64 build (the
 * Windows port) dispatches through dolrecomp_call unless the source says it
 * carries them. */
#if defined(__x86_64__) && defined(DOLRECOMP_HAS_X86_64_V3_DISPATCH)
#define BLUEWAKE_X86_64_V3_DISPATCH 1
#else
#define BLUEWAKE_X86_64_V3_DISPATCH 0
#endif

#if BLUEWAKE_X86_64_V3_DISPATCH
static int host_has_x86_64_v3(void)
{
#if defined(__GNUC__) || defined(__clang__)
    static int supported = -1;
    if (supported < 0)
    {
        __builtin_cpu_init();
        supported = __builtin_cpu_supports("avx") &&
            __builtin_cpu_supports("avx2") &&
            __builtin_cpu_supports("fma") &&
            __builtin_cpu_supports("bmi") &&
            __builtin_cpu_supports("bmi2") &&
            __builtin_cpu_supports("movbe") &&
            __builtin_cpu_supports("lzcnt");
    }
    return supported;
#else
    return 0;
#endif
}
#endif

static int selected_dispatch(CPUState* ctx, u32 address)
{
#if BLUEWAKE_X86_64_V3_DISPATCH
    if (host_has_x86_64_v3())
        return dolrecomp_call__x86_64_v3(ctx, address);
#endif
    return dolrecomp_call(ctx, address);
}

void dolrecomp_indirect_dispatch(CPUState* ctx, u32 address)
{
    (void)selected_dispatch(ctx, address);
}

static int chassis_dispatch(CPUState* ctx, u32 address)
{
    /* The inlined form, so the loop sees selected_dispatch as a static function
     * rather than as a pointer it must reload and call indirectly at every
     * guest edge. Same body as the exported entry point. */
    return bluewake_chassis_dispatch_loop(
        ctx, address, selected_dispatch, s_edge_service,
        s_edge_service_user);
}

static void chassis_on_state_loaded(CPUState* ctx)
{
    ppc_fpscr_updated(ctx);
}

#include "module_tables.inc"
#include "rel_modules.inc"
#include "rel_data.inc"

static const StaticRecompModuleDesc s_desc = {
#if defined(BW_GUEST_MEM1)
    BLUEWAKE_FIXED_MEM1_ABI_VERSION,
#elif defined(BLUEWAKE_FIXED_CPU)
    BLUEWAKE_FIXED_CPU_ABI_VERSION,
#else
    STATICRECOMP_ABI_VERSION,
#endif
    GXRUNTIME_CPU_ABI_VERSION,
    (u32)sizeof(CPUState),
    MODULE_GAME_ID,
    DOLRECOMP_ENTRY_POINT,
    chassis_dispatch,
    chassis_on_state_loaded,
    s_code_ranges,
    MODULE_CODE_RANGE_COUNT,
    s_smc_ranges,
    MODULE_SMC_RANGE_COUNT,
    s_chunk_ranges,
    MODULE_CHUNK_RANGE_COUNT,
    s_chunk_hashes,
    s_rel_modules,
    MODULE_REL_MODULE_COUNT,
};

#if defined(_WIN32)
#define RECOMP_MODULE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define RECOMP_MODULE_EXPORT __attribute__((visibility("default")))
#else
#define RECOMP_MODULE_EXPORT
#endif

#if defined(BLUEWAKE_NATIVE_ENTRIES)
/* Wind Waker Recomp's certified native entries (scripts/windows/native_entries.py
 * hooks them only where the translation is the one their tests compared): the
 * J3D FIFO matrix loads, two collision checks, PSMTXMultVecSR, the joint matrix
 * calculations and the actor search by name. Built only with --native-entries;
 * BLUEWAKE_NATIVE_ENTRIES=0 leaves them to the translation. */
#include "native_fifo.h"
#include "native_bg.h"
#include "native_vec.h"
#include "native_mtxcalc.h"
#include "native_search.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void bluewake_native_entries_enable(void)
{
    static int done;
    if (done) return;
    done = 1;
    const char* setting = getenv("BLUEWAKE_NATIVE_ENTRIES");
    const int on = !(setting && strcmp(setting, "0") == 0);
    bluewake_native_fifo_enabled = on;
    bluewake_native_bg_enabled = on;
    bluewake_native_vec_sr_enabled = on;
    bluewake_native_mtxcalc_enabled = on;
    bluewake_native_search_enabled = on;
    if (on) {
        atexit(bluewake_native_fifo_report);
        atexit(bluewake_native_bg_report);
        atexit(bluewake_native_vec_sr_report);
        atexit(bluewake_native_mtxcalc_report);
        atexit(bluewake_native_search_report);
        fprintf(stderr, "[native-entries] certified native entries enabled\n");
    }
}
#endif

RECOMP_MODULE_EXPORT const StaticRecompModuleDesc* staticrecomp_get_module(void)
{
#if defined(BLUEWAKE_NATIVE_ENTRIES)
    bluewake_native_entries_enable();
#endif
    return &s_desc;
}

RECOMP_MODULE_EXPORT const BlueWakeRelData* staticrecomp_get_rel_data(u32* count)
{
    if (count) *count = MODULE_REL_DATA_COUNT;
    return s_rel_data;
}

RECOMP_MODULE_EXPORT const BlueWakeRelLifecycle* staticrecomp_get_rel_lifecycle(u32* count)
{
    if (count) *count = MODULE_REL_LIFECYCLE_COUNT;
    return s_rel_lifecycle;
}

RECOMP_MODULE_EXPORT const BlueWakeRelLifecycleV2* staticrecomp_get_rel_lifecycle_v2(u32* count)
{
#ifdef MODULE_REL_LIFECYCLE_V2_COUNT
    if (count) *count = MODULE_REL_LIFECYCLE_V2_COUNT;
    return s_rel_lifecycle_v2;
#else
    if (count) *count = 0;
    return NULL;
#endif
}

RECOMP_MODULE_EXPORT const BlueWakeShipContractV1* staticrecomp_get_ship_contract_v1(void)
{
#ifdef MODULE_SHIP_CONTRACT_PRESENT
    return &s_ship_contract;
#else
    return NULL;
#endif
}

RECOMP_MODULE_EXPORT const BlueWakeRelInitContractSetV1* staticrecomp_get_rel_init_contracts_v1(void)
{
#ifdef MODULE_REL_INIT_CONTRACTS_PRESENT
    return &s_rel_init_contract_set;
#else
    return NULL;
#endif
}

/* Host-only diagnostics need to reach the runtime instance embedded here. */
RECOMP_MODULE_EXPORT void bluewake_set_mem_write_journal(
    PPCMemWriteJournal fn, void* user)
{
    ppc_set_mem_write_journal(fn, user);
}

RECOMP_MODULE_EXPORT void bluewake_set_edge_service(
    BluewakeEdgeServiceFn fn, void* user)
{
    s_edge_service = fn;
    s_edge_service_user = user;
}

/* Mods (scripts/mods/build_mod_variants.py). A mod is compiled in as variant
 * chunks, extra chunks and data writes; the host enables mods once, at boot,
 * before the first dispatch fills the pc cache. The types are declared in
 * generated_composite.h, whose dispatcher consults the extra chunks. */
typedef void (*BlueWakeModWriteFn)(void* user, u32 address, const u8* bytes, u32 size);
#include "mod_variants.inc"
static u32 s_mod_enabled_mask;

static DolRecompFunction bluewake_mod_extra_find(u32 address)
{
#if MODULE_MOD_COUNT > 0
    for (u32 i = 0; i < MODULE_MOD_EXTRA_CHUNK_COUNT; ++i) {
        const BlueWakeModExtraChunk* c = &s_mod_extra_chunks[i];
        if ((s_mod_enabled_mask & (1u << c->mod)) != 0u && c->start <= address &&
            address < c->end && ((address - c->start) & 3u) == 0u)
            return c->fn;
    }
#else
    (void)address;
#endif
    return NULL;
}

RECOMP_MODULE_EXPORT u32 bluewake_composite_mod_count(void)
{
    return MODULE_MOD_COUNT;
}

RECOMP_MODULE_EXPORT const char* bluewake_composite_mod_name(u32 index)
{
#if MODULE_MOD_COUNT > 0
    return index < MODULE_MOD_COUNT ? s_mod_names[index] : NULL;
#else
    (void)index;
    return NULL;
#endif
}

/* Points the chunk table at the enabled mods' variants; returns how many
 * chunks were replaced. A variant applies when every mod it requires is
 * enabled; the table lists combined variants last, so they win. */
RECOMP_MODULE_EXPORT u32 bluewake_composite_apply_mods(u32 mask)
{
    u32 replaced = 0;
    s_mod_enabled_mask = mask;
#if MODULE_MOD_COUNT > 0
    for (u32 i = 0; i < MODULE_MOD_CHUNK_COUNT; ++i) {
        const BlueWakeModChunk* c = &s_mod_chunks[i];
        if ((mask & c->mask) != c->mask)
            continue;
        for (u32 k = 0; k < DOLRECOMP_CHUNK_COUNT; ++k) {
            if (s_dolrecomp_chunk_starts[k] == c->start) {
                s_dolrecomp_chunk_fns[k] = c->fn;
                replaced++;
                break;
            }
        }
    }
#else
    (void)mask;
#endif
    return replaced;
}

/* Hands the enabled mods' data writes to the host, which writes guest RAM:
 * every write at boot (per_frame_only 0), the per-frame ones (a Gecko code's
 * data writes) at every retrace (per_frame_only 1). */
RECOMP_MODULE_EXPORT u32 bluewake_composite_mod_writes(
    u32 mask, u32 per_frame_only, BlueWakeModWriteFn fn, void* user)
{
    u32 count = 0;
#if MODULE_MOD_COUNT > 0
    for (u32 i = 0; i < MODULE_MOD_WRITE_COUNT; ++i) {
        const BlueWakeModWrite* w = &s_mod_writes[i];
        if ((mask & (1u << w->mod)) == 0u)
            continue;
        if (per_frame_only && !w->every_frame)
            continue;
        if (fn)
            fn(user, w->address, w->bytes, w->size);
        count++;
    }
#else
    (void)mask;
    (void)per_frame_only;
    (void)fn;
    (void)user;
#endif
    return count;
}
