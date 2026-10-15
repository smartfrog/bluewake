#include "native_ctx.h"

#include <assert.h>
#include <string.h>

extern int ww_native_probe_main(int argc, char** argv);
extern void ww_native_probe_set_thread(uint32_t value);
extern void ww_native_fixture_invoke(void* function);
extern void fixture_target_arm(void), fixture_target_thumb(void);
extern void fixture_nested_target(void), fixture_observer_scramble(void);
extern void ww_native_read(void), ww_native_write(void);
extern void __wrap_ppc_fcmp(void), __wrap_ppc_psq_load(void), __wrap_sceIoPread(void);
extern void __wrap_func_802AD6E0(void);

void* ww_native_targets[8];
uint32_t fixture_clobber, fixture_result[12], fixture_args[7], fixture_tail[3], fixture_sp;
uint32_t fixture_ctx, fixture_nesting;
uint32_t fixture_vfp_saved[16];
static unsigned char trace_file[WW_NATIVE_FILE_BYTES];
static uint32_t pc = 0x802b0470u;
static unsigned sink_mode;
static bool recursive;

static int sink(void* user, uint32_t offset, const void* data, uint32_t size) {
    (void)user;
    assert(offset + size <= sizeof(trace_file));
    fixture_observer_scramble();
    if (sink_mode == 1u)
        return (int)size - 1;
    if (sink_mode == 2u)
        return -1;
    if (sink_mode == 3u && !recursive) {
        recursive = true;
        ww_native_fixture_invoke(ww_native_write);
        recursive = false;
    }
    memcpy(trace_file + offset, data, size);
    return (int)size;
}

extern void semihost(uint32_t operation, const void* argument);

int puts(const char* text) {
    semihost(4u, text);
    semihost(4u, "\n");
    return 0;
}

void __assert_func(const char* file, int line, const char* function, const char* condition) {
    (void)file;
    (void)line;
    (void)function;
    puts(condition);
    uint32_t reason[2] = {0x20026u, 1u};
    semihost(0x20u, reason);
    for (;;) {}
}

void* memcpy(void* destination, const void* source, size_t size) {
    unsigned char* to = destination;
    const unsigned char* from = source;
    for (size_t i = 0; i < size; ++i)
        to[i] = from[i];
    return destination;
}

void* memset(void* destination, int value, size_t size) {
    unsigned char* to = destination;
    for (size_t i = 0; i < size; ++i)
        to[i] = (unsigned char)value;
    return destination;
}

int memcmp(const void* left, const void* right, size_t size) {
    const unsigned char* a = left;
    const unsigned char* b = right;
    for (size_t i = 0; i < size; ++i)
        if (a[i] != b[i])
            return a[i] - b[i];
    return 0;
}

int strcmp(const char* left, const char* right) {
    while (*left && *left == *right) { ++left; ++right; }
    return (unsigned char)*left - (unsigned char)*right;
}

static void init(void) {
    sink_mode = 0u;
    recursive = false;
    memset(trace_file, 0, sizeof(trace_file));
    ww_native_trace_init((void*)0x5678u, (void*)0x1234u, &pc, sink, NULL, 1u);
    ww_native_trace_arm(400u);
    fixture_clobber = 0u;
    fixture_ctx = 0x1234u;
    for (unsigned i = 0; i < 8; ++i)
        ww_native_targets[i] = fixture_target_thumb;
}

int main(void) {
    const char* scenarios[] = {"preserve", "clobber", "ctx-one", "nesting", "overflow", "ring",
        "pinning", "short-write", "full-disk", "recursion", "concurrency", "interrupted", "unarmed", "markers"};
    for (unsigned i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) {
        char* argv[] = {"probe", (char*)scenarios[i]};
        assert(ww_native_probe_main(2, argv) == 0);
    }
    for (unsigned clobber = 0; clobber < 2; ++clobber) {
        init();
        fixture_clobber = clobber;
#ifdef WW_NATIVE_BASELINE
        ww_native_fixture_invoke(fixture_target_thumb);
#else
        ww_native_fixture_invoke(ww_native_read);
#endif
        assert(fixture_result[4] == (clobber ? 1u : 0x1234u));
        assert(fixture_result[0] == 0x89abcdefu && fixture_result[1] == 0x01234567u);
        WwNativeRecord record;
        memcpy(&record, trace_file + (clobber ? 384u : 4352u), sizeof(record));
        assert(ww_native_record_valid(&record));
        assert(record.r[4] == fixture_result[4]);
        puts(clobber ? "assembly-clobber-capture-forward PASS" : "assembly-preserve-u64 PASS");
    }
    init();
    ww_native_fixture_invoke(ww_native_write);
    assert(fixture_args[0] == 0x1234u && fixture_args[1] == 0x11u);
    assert(fixture_args[2] == 0x22u && fixture_args[3] == 0x33u && fixture_args[4] == 0x11u);
    puts("assembly-write-u64-stack PASS");
    init();
    ww_native_fixture_invoke(__wrap_ppc_psq_load);
    assert(fixture_args[4] == 0x11u && fixture_args[5] == 0x22u && fixture_args[6] == 0x33u);
    puts("assembly-three-stack-args PASS");
    init();
    ww_native_fixture_invoke(__wrap_sceIoPread);
    assert(fixture_args[4] == 0x11u && fixture_args[5] == 0x22u);
    puts("assembly-pread-offset PASS");
    init();
    ww_native_targets[0] = fixture_target_arm;
    ww_native_fixture_invoke(ww_native_read);
    assert(fixture_result[0] == 0x89abcdefu);
    puts("assembly-arm-thumb-interworking PASS");
    init();
    ww_native_fixture_invoke(__wrap_ppc_fcmp);
    assert(fixture_result[5] == 0x33u && fixture_result[6] == 0x55u);
    assert(fixture_result[9] == 0x11u && fixture_result[10] == 0x33u);
    for (unsigned i = 0; i < 16; ++i)
        assert(fixture_vfp_saved[i] == (i % 2 == 0 ? 0x11u : 0x33u));
    puts("assembly-hard-float-result-vfp-saved PASS");
    init();
    ww_native_fixture_invoke(__wrap_func_802AD6E0);
    assert(fixture_tail[0] == 0x1234u && fixture_tail[1] == fixture_sp);
    WwNativeRecord entry;
    memcpy(&entry, trace_file + 4224u, sizeof(entry));
    assert(entry.stage == WW_NATIVE_CHUNK && entry.continuation == fixture_tail[2]);
    assert(entry.sp == fixture_sp && fixture_result[11] == fixture_sp);
    puts("assembly-tail-r0-sp-lr PASS");
    for (unsigned mode = 1; mode <= 3; ++mode) {
        init();
        sink_mode = mode;
        fixture_clobber = 1u;
        ww_native_fixture_invoke(ww_native_write);
        assert(fixture_result[4] == 1u && fixture_result[0] == 0x89abcdefu);
        assert(fixture_args[2] == 0x22u && fixture_args[3] == 0x33u && fixture_args[4] == 0x11u);
        assert(ww_native_trace_failures() & (mode == 3u ? WW_NATIVE_REENTRANCY : WW_NATIVE_IO_FAILURE));
    }
    puts("assembly-short-write-full-disk-recursion-forward PASS");
    for (unsigned depth = 2; depth <= 18; depth += 16) {
        init();
        fixture_nesting = depth;
        fixture_clobber = 1u;
        ww_native_targets[0] = fixture_nested_target;
        ww_native_targets[2] = fixture_nested_target;
        ww_native_fixture_invoke(ww_native_read);
        assert(fixture_result[4] == 1u && fixture_result[0] == 0x89abcdefu);
        assert(!(ww_native_trace_failures() & WW_NATIVE_PAIR_FAILURE));
        assert(!!(ww_native_trace_failures() & WW_NATIVE_DEPTH_FAILURE) == (depth == 18u));
    }
    puts("assembly-nesting-exhaustion-forward PASS");
    init();
    fixture_ctx = 1u;
    ww_native_fixture_invoke(ww_native_read);
    memcpy(&entry, trace_file + 512u, sizeof(entry));
    assert(entry.r[0] == 1u && (entry.flags & WW_NATIVE_CTX_MISMATCH));
    assert(!(entry.flags & WW_NATIVE_PC_VALID));
    puts("assembly-invalid-ctx-forward PASS");
    init();
    fixture_clobber = 1u;
    ww_native_fixture_invoke(ww_native_read);
    unsigned char pinned[640];
    memcpy(pinned, trace_file, sizeof(pinned));
    fixture_clobber = 0u;
    for (unsigned i = 0; i < 4200; ++i)
        ww_native_fixture_invoke(ww_native_read);
    assert(memcmp(pinned, trace_file, sizeof(pinned)) == 0);
    WwNativeRecord post;
    memcpy(&post, trace_file + 4096u + 4096u * 128u, sizeof(post));
    assert(ww_native_record_valid(&post) && post.sequence > 512u);
    puts("assembly-ring-first-divergence-pinning PASS");
    init();
    ww_native_probe_set_thread(2u);
    fixture_clobber = 1u;
    ww_native_fixture_invoke(ww_native_write);
    assert(fixture_result[4] == 1u && fixture_args[4] == 0x11u);
    assert(ww_native_trace_failures() & WW_NATIVE_CONCURRENCY);
    ww_native_probe_set_thread(1u);
    ww_native_trace_boundary((void*)0x1234u, WW_NATIVE_DISPATCH_ENTER);
    memcpy(&entry, trace_file + 640u, sizeof(entry));
    assert(ww_native_record_valid(&entry) && (entry.flags & WW_NATIVE_CONCURRENCY));
    puts("assembly-unsupported-concurrency-forward PASS");
    init();
    ww_native_trace_end();
    fixture_clobber = 1u;
    ww_native_fixture_invoke(ww_native_write);
    assert(fixture_result[4] == 1u && fixture_args[4] == 0x11u);
    assert(trace_file[4224] == 0u);
    puts("assembly-unarmed-tail-forward PASS");
    puts("NATIVE-CTX fixtures PASS");
    return 0;
}
