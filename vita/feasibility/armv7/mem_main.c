#define _POSIX_C_SOURCE 200809L
#include "mem_scenarios.h"
#include "mem_identity.h"
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __vita__
#define MEM_DIRECTORY "ux0:data/bluewake-ww-fixture/"
#define MEM_PLATFORM "VITA"
#else
#define MEM_DIRECTORY ""
#define MEM_PLATFORM "HOST"
#endif

static bool hex(const char* text, size_t length) {
    if (strlen(text) != length)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (!isxdigit((unsigned char)text[i]))
            return false;
    return true;
}

static int request(char* token, char* source, char* package) {
    FILE* stream = fopen(MEM_DIRECTORY "mem.request", "r");
    if (!stream)
        return 1;
    int fields = fscanf(stream, "%32s %40s %64s", token, source, package);
    char extra;
    int trailing = fscanf(stream, " %c", &extra);
    int error = ferror(stream);
    int closed = fclose(stream);
    return fields != 3 || trailing != EOF || error || closed != 0 ||
           !hex(token, 32) || !hex(source, 40) || !hex(package, 64) || strcmp(source, WW_MEM_SOURCE) != 0;
}

static bool absent(const char* path) {
    struct stat info;
    return lstat(path, &info) < 0 && errno == ENOENT;
}

static int snapshot(FILE* stream, const char* name, const char* phase, MemSnapshot value) {
    if (!value.available)
        return fprintf(stream, "SNAP %s-%s available=0 result=UNKNOWN size=UNKNOWN user=UNKNOWN cdram=UNKNOWN phycont=UNKNOWN valid=UNKNOWN\n", name, phase) < 0;
    return fprintf(stream, "SNAP %s-%s available=1 result=%d size=%d user=%d cdram=%d phycont=%d valid=%d\n",
                   name, phase, value.result, value.size, value.user, value.cdram, value.phycont,
                   mem_snapshot_valid(value)) < 0;
}

int fixture_run(int argc, char** argv) {
    (void)argv;
    char token[33], source[41], package[65];
    const char* partial = MEM_DIRECTORY "mem.partial";
    const char* complete = MEM_DIRECTORY "mem.report";
    if (argc > 1 || request(token, source, package) || !absent(partial) || !absent(complete))
        return 1;
    FILE* report = fopen(partial, "wx");
    if (!report)
        return 1;
    int error = fprintf(report, "MEM %s %s %s method=bounded-v2 phase=guest platform=%s\n",
                        token, source, package, MEM_PLATFORM) < 0;
    error |= fprintf(report, "ABI pointer=%u cpu=%u version=%u ram_offset=%u external_offset=%u\n",
                     fixture_abi.pointer_bytes, fixture_abi.cpu_bytes, fixture_abi.cpu_abi,
                     fixture_abi.ram_pointer_offset, fixture_abi.external_pointer_offset) < 0;
    error |= fprintf(report, "RESERVED heap=100663296 worker=262144 cpu_subset_worker=%u\n",
                     fixture_abi.cpu_bytes) < 0;
    error |= fprintf(report, "UNKNOWN native_highwater largest_block physical_limits residency game_peak\n") < 0;
    MemReal real;
    MemSdk sdk;
    int real_error = mem_real(&real);
    int sdk_error = mem_sdk(&sdk, mem_sdk_api());
    const char* real_names[] = {"mem1", "vm", "cache", "alias"};
    const char* phases[] = {"before", "live", "after"};
    const char* sdk_names[] = {"user", "cdram", "phycont"};
    for (unsigned i = 0; i < 4; ++i) {
        MemOwner* owner = &real.owners[i];
        error |= fprintf(report, "OWNER %s requested=%u mapped=%u touched=%u views=%u\n",
                         real_names[i], owner->requested, owner->mapped, owner->touched, owner->views) < 0;
    }
    for (unsigned i = 0; i < 3; ++i)
        error |= snapshot(report, "real", phases[i], real.snapshots[i]);
    error |= fprintf(report, "REAL peak=%u live=%u pattern=%d alias=%d bounds=%d %s\n",
                     real.peak, real.live, real.pattern, real.alias, real.bounds, real_error ? "FAIL" : "PASS") < 0;
    for (unsigned i = 0; i < 3; ++i) {
        MemSdkBlock* block = &sdk.blocks[i];
        if (!sdk.available) {
            error |= fprintf(report, "BLOCK %s requested=%u mapped=0 touched=0 views=0 available=0 uid=UNKNOWN base=UNKNOWN info=UNKNOWN info_size=UNKNOWN info_required=UNKNOWN free=UNKNOWN access=UNKNOWN type=UNKNOWN pattern=0 SKIP\n",
                             sdk_names[i], block->owner.requested) < 0;
        } else {
            bool ok = block->uid >= 0 && block->base_result >= 0 && block->info_result >= 0 &&
                      block->info_size == block->info_required && block->free_result >= 0 && block->pattern;
            for (unsigned j = 0; j < 3; ++j)
                ok = ok && block->snapshots[j].available && mem_snapshot_valid(block->snapshots[j]);
            error |= fprintf(report, "BLOCK %s requested=%u mapped=%u touched=%u views=%u available=1 uid=%d base=%d info=%d info_size=%u info_required=%u free=%d access=%u type=%u pattern=%d %s\n",
                             sdk_names[i], block->owner.requested, block->owner.mapped, block->owner.touched,
                             block->owner.views, block->uid, block->base_result, block->info_result,
                             block->info_size, block->info_required,
                             block->free_result, block->access, block->type, block->pattern, ok ? "PASS" : "FAIL") < 0;
        }
        for (unsigned j = 0; j < 3; ++j)
            error |= snapshot(report, sdk_names[i], phases[j], block->snapshots[j]);
    }
    const char* sdk_verdict = !sdk.available ? "SKIP" : (sdk_error ? "FAIL" : "PASS");
    error |= fprintf(report, "SDK peak=%u max_blocks=%u live=%u %s\n", sdk.peak, sdk.max_blocks, sdk.live, sdk_verdict) < 0;
    if (!error)
        error = fprintf(report, "END real=%s sdk=%s failures=%d\n", real_error ? "FAIL" : "PASS",
                        sdk_verdict, !!real_error + !!sdk_error) < 0;
    if (error) {
        int closed = fclose(report);
        return closed != 0 || error;
    }
    return fixture_publish_report(report, partial, complete) || real_error || sdk_error;
}
