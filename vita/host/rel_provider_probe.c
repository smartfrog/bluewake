// Direct owned-disc REL provider scenarios for test_rel_provider.py.
#include "boot_host.h"
#include "rel_archive.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail_malloc;
static size_t fail_malloc_size;
static unsigned fail_close_at;
static unsigned closes;

void* __real_malloc(size_t size);
int __real_bw_dvd_fclose(FILE* stream);

void* __wrap_malloc(size_t size) {
    if (fail_malloc && size == fail_malloc_size)
        return NULL;
    return __real_malloc(size);
}

int __wrap_bw_dvd_fclose(FILE* stream) {
    if (fail_close_at != 0u && ++closes == fail_close_at)
        return -1;
    return __real_bw_dvd_fclose(stream);
}

static const StaticRecompRelModule materialize_rel = {
    .module_id = 1u, .section_count = 2u, .section_info_offset = 0x4Cu, .file_size = 256u,
};
static const StaticRecompModuleDesc materialize_module = {
    .rel_modules = &materialize_rel, .num_rel_modules = 1u,
};

static void serve_title(WwHost* host, const char* name, const char* expected_path) {
    u8* image = NULL;
    u32 bytes = 0u;
    const char* error = ww_host_rel_image(host, name, &image, &bytes);
    assert(error == NULL && image != NULL && bytes != 0u);
    FILE* expected = fopen(expected_path, "rb");
    assert(expected != NULL);
    u8* reference = malloc(bytes);
    assert(reference != NULL);
    assert(fread(reference, 1u, bytes, expected) == bytes);
    assert(fgetc(expected) == EOF && fclose(expected) == 0);
    assert(memcmp(image, reference, bytes) == 0);
    free(reference);
    free(image);
    fprintf(stderr, "PASS served name=%s bytes=%u\n", name, bytes);
}

int main(int argc, char** argv) {
    assert(argc == 3);
    const char* scenario = argv[1];
    const char* argument = argv[2];
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    if (strcmp(scenario, "allocation") == 0) {
        fail_malloc = true;
        fail_malloc_size = (size_t)strtoul(argument, NULL, 0);
    }
    if (strcmp(scenario, "closefail") == 0)
        fail_close_at = (unsigned)strtoul(argument, NULL, 0);
    if (strcmp(scenario, "materialize") == 0) {
        host->module = &materialize_module;
        const char* error = ww_host_materialize_rel(host, 0x80002000u, "d_a_title");
        assert(error == NULL);
        fprintf(stderr, "PASS materialized\n");
    } else if (strcmp(scenario, "serve") == 0) {
        serve_title(host, "d_a_title", argument);
    } else if (strcmp(scenario, "serve-case") == 0) {
        serve_title(host, "D_A_TITLE", argument);
    } else {
        const char* name = strcmp(scenario, "unsafe") == 0 ? argument : "d_a_title";
        u8* image = NULL;
        u32 bytes = 0u;
        const char* error = ww_host_rel_image(host, name, &image, &bytes);
        assert(error != NULL && image == NULL && bytes == 0u);
        fprintf(stderr, "PASS rejected: %s\n", error);
        fail_malloc = false;
        fail_close_at = 0u;
    }
    ww_host_shutdown(host);
    free(host);
    return 0;
}
