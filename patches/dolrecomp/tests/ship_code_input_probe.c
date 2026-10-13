/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/pipeline.h"
#include "frontend/decoder.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE* sidecar;
static FILE* emission;
static FILE* later_header;
static FILE* later_manifest;
static FILE* ship_chunk;
static unsigned later_closed;
static const char* fault;
static const char* target;

FILE* __real_fopen(const char*, const char*);
size_t __real_fwrite(const void*, size_t, size_t, FILE*);
int __real_fclose(FILE*);
PPCInst __real_ppc_decode(u32, u32);
bool __real_rel_apply_relocations(RELFile*, const RELModuleMap*);

FILE* __wrap_fopen(const char* path, const char* mode) {
    int is_sidecar = target && strcmp(path, target) == 0;
    if (is_sidecar && later_closed != 3u) {
        fprintf(stderr, "test: publication preceded completion of later module\n");
        errno = EINVAL;
        return NULL;
    }
    if (is_sidecar && fault && strcmp(fault, "open") == 0) {
        errno = EACCES;
        return NULL;
    }
    int write_error = fault && strstr(mode, "w") && strstr(path, "z_later_169") &&
        ((strcmp(fault, "chunk-write-error") == 0 && strstr(path, "chunks/")) ||
         (strcmp(fault, "header-write-error") == 0 && strstr(path, "generated.h")) ||
         (strcmp(fault, "manifest-write-error") == 0 && strstr(path, "generated.c") &&
          !strstr(path, "chunks/")));
    FILE* stream = __real_fopen(write_error ? "/dev/full" : path, mode);
    if (write_error && stream)
        setvbuf(stream, NULL, _IONBF, 0);
    if (is_sidecar)
        sidecar = stream;
    if (strstr(mode, "w")) {
        if (strstr(path, "z_later_169")) {
            if (strstr(path, "chunks/"))
                emission = stream;
            else if (strstr(path, "generated.h"))
                later_header = stream;
            else if (strstr(path, "generated.c"))
                later_manifest = stream;
        }
        if (strstr(path, "a_ship_168/chunks/"))
            ship_chunk = stream;
    }
    return stream;
}

size_t __wrap_fwrite(const void* data, size_t size, size_t count, FILE* stream) {
    if (stream == sidecar && fault && strcmp(fault, "write") == 0) {
        errno = ENOSPC;
        return 0;
    }
    return __real_fwrite(data, size, count, stream);
}

int __wrap_fclose(FILE* stream) {
    int fail = fault && ((stream == sidecar && strcmp(fault, "close") == 0) ||
                        (stream == emission && strcmp(fault, "emission") == 0) ||
                        (stream == later_header && strcmp(fault, "header") == 0) ||
                        (stream == later_manifest && strcmp(fault, "manifest") == 0) ||
                        (stream == ship_chunk && strcmp(fault, "ship-chunk") == 0));
    if (stream == emission || stream == later_header || stream == later_manifest)
        later_closed++;
    if (stream == sidecar)
        sidecar = NULL;
    if (stream == emission)
        emission = NULL;
    if (stream == later_header)
        later_header = NULL;
    if (stream == later_manifest)
        later_manifest = NULL;
    if (stream == ship_chunk)
        ship_chunk = NULL;
    int result = __real_fclose(stream);
    if (fail) {
        errno = ENOSPC;
        return EOF;
    }
    return result;
}

PPCInst __wrap_ppc_decode(u32 raw, u32 address) {
    printf("DECODE %08X %08X\n", address, raw);
    return __real_ppc_decode(raw, address);
}

bool __wrap_rel_apply_relocations(RELFile* rel, const RELModuleMap* map) {
    bool ok = __real_rel_apply_relocations(rel, map);
    if (ok && rel->module_id == 168u && fault) {
        if (strcmp(fault, "missing-data") == 0)
            rel->sections[1].data = NULL;
        if (strcmp(fault, "no-code") == 0)
            rel->sections[1].executable = false;
        if (strcmp(fault, "invalid-index") == 0)
            rel->sections[1].index = rel->section_count;
    }
    return ok;
}

int main(int argc, char** argv) {
    if (argc != 4)
        return 2;
    target = getenv("DOLRECOMP_SHIP_CODE_INPUT");
    fault = getenv("SHIP_TEST_FAULT");
    CliOptions options = {0};
    if (fault && strcmp(fault, "llvm") == 0)
        options.backend = DOLRECOMP_BACKEND_LLVM;
    if (strcmp(argv[3], "single") == 0) {
        RELFile rel;
        if (!rel_load(&rel, argv[1], 0xC0400000u))
            return 1;
        int ok = emit_rel_split(&rel, argv[2], DOLRECOMP_CPU_GEKKO, 1, 0, &options);
        rel_free(&rel);
        return ok ? 0 : 1;
    }
    return emit_rel_directory(argv[1], argv[2], NULL, 1,
                              DOLRECOMP_CPU_GEKKO, 1, 0xC0400000u,
                              &options) ? 0 : 1;
}
