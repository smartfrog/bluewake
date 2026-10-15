#include <sys/stat.h>

typedef struct {
    ShipCodeInput capture;
    const char* basename;
    u32 seen;
    char path[1200];
    int published;
} ActorCapture;

static char* actor_output_path(const char* path) {
    char* resolved = realpath(path, NULL);
    if (resolved)
        return resolved;
    char* copy = strdup(path);
    if (!copy)
        return NULL;
    char* slash = strrchr(copy, '/');
    const char* basename = slash ? slash + 1 : copy;
    char* parent = NULL;
    if (basename[0] && strcmp(basename, ".") != 0 && strcmp(basename, "..") != 0) {
        if (slash) {
            *slash = '\0';
            parent = realpath(copy[0] ? copy : "/", NULL);
        } else {
            parent = realpath(".", NULL);
        }
    }
    if (parent) {
        const size_t length = strlen(parent) + strlen(basename) + 2u;
        resolved = malloc(length);
        if (resolved)
            snprintf(resolved, length, "%s/%s", parent, basename);
    }
    free(parent);
    free(copy);
    return resolved;
}

static int actor_batch_separate(const char* directory, const char* output) {
    char* capture_path = actor_output_path(directory);
    char* output_path = actor_output_path(output);
    int ok = capture_path && output_path;
    if (ok) {
        const size_t a = strlen(capture_path), b = strlen(output_path);
        if ((a <= b && strncmp(capture_path, output_path, a) == 0 && (a == b || output_path[a] == '/')) ||
            (b <= a && strncmp(capture_path, output_path, b) == 0 && (a == b || capture_path[b] == '/')))
            ok = 0;
    }
    if (!ok)
        fprintf(stderr, "error: actor capture and generated output paths collide or cannot be resolved\n");
    free(capture_path);
    free(output_path);
    return ok;
}

static int actor_batch_paths(ActorCapture* entries, u32 count, const char* directory) {
    if (!directory || directory[0] != '/' || !count || count > 8u) {
        fprintf(stderr, "error: actor batch requires 1..8 reviewed entries and an absolute private directory\n");
        return 0;
    }
    struct stat status;
    if (lstat(directory, &status) == 0 || errno != ENOENT) {
        fprintf(stderr, "error: actor batch output directory must be fresh\n");
        return 0;
    }
    for (u32 i = 0u; i < count; ++i) {
        ActorCapture* entry = &entries[i];
        if (!entry->basename || !entry->basename[0] || strchr(entry->basename, '/') ||
            strcmp(entry->basename, "complete.json") == 0 ||
            !entry->capture.magic || !entry->capture.module_id) {
            fprintf(stderr, "error: invalid reviewed actor capture specification\n");
            return 0;
        }
        for (u32 j = 0u; j < i; ++j) {
            if (entry->capture.module_id == entries[j].capture.module_id ||
                memcmp(entry->capture.magic, entries[j].capture.magic, 8u) == 0 ||
                strcmp(entry->basename, entries[j].basename) == 0) {
                fprintf(stderr, "error: actor capture ID, magic or output-path collision\n");
                return 0;
            }
        }
        const int length = snprintf(entry->path, sizeof(entry->path), "%s/%s", directory, entry->basename);
        if (length < 0 || (size_t)length >= sizeof(entry->path)) {
            fprintf(stderr, "error: actor capture path exceeds its bound\n");
            return 0;
        }
    }
    return 1;
}

static ShipCodeInput* actor_batch_match(ActorCapture* entries, u32 count, u32 id) {
    for (u32 i = 0u; i < count; ++i) {
        if (entries[i].capture.module_id == id) {
            ++entries[i].seen;
            return &entries[i].capture;
        }
    }
    return NULL;
}

static int actor_batch_complete(const ActorCapture* entries, u32 count) {
    for (u32 i = 0u; i < count; ++i) {
        if (entries[i].seen != 1u) {
            fprintf(stderr, "error: requested actor module %u must occur exactly once (seen %u)\n",
                    entries[i].capture.module_id, entries[i].seen);
            return 0;
        }
    }
    return 1;
}

static int actor_batch_publish(ActorCapture* entries, u32 count, const char* directory) {
    if (!actor_batch_complete(entries, count))
        return 0;
    if (mkdir(directory, 0700) != 0) {
        fprintf(stderr, "error: cannot exclusively create actor capture directory: %s\n", strerror(errno));
        return 0;
    }
    int ok = 0, manifest_owned = 0;
    char manifest[1300];
    const int length = snprintf(manifest, sizeof(manifest), "%s/complete.json", directory);
    if (length < 0 || (size_t)length >= sizeof(manifest))
        goto done;
    for (u32 i = 0u; i < count; ++i) {
        if (!publish_ship_code_input(entries[i].path, &entries[i].capture))
            goto done;
        entries[i].published = 1;
    }
    FILE* stream = fopen(manifest, "wbx");
    if (!stream) {
        fprintf(stderr, "error: cannot create actor capture completion receipt: %s\n", strerror(errno));
        goto done;
    }
    manifest_owned = 1;
    int written = fprintf(stream, "{\"version\":1,\"batch\":\"opening-a\",\"modules\":[") >= 0;
    for (u32 i = 0u; i < count; ++i)
        if (fprintf(stream, "%s{\"id\":%u,\"capture\":\"%s\",\"size\":%zu}",
                    i ? "," : "", entries[i].capture.module_id, entries[i].basename,
                    entries[i].capture.size) < 0)
            written = 0;
    if (fprintf(stream, "]}\n") < 0 || ferror(stream))
        written = 0;
    if (fclose(stream) != 0)
        written = 0;
    if (!written) {
        fprintf(stderr, "error: actor capture completion receipt write/close failed\n");
        goto done;
    }
    ok = 1;
done:
    if (!ok) {
        if (manifest_owned && remove(manifest) != 0)
            fprintf(stderr, "error: cannot remove owned incomplete batch receipt: %s\n", strerror(errno));
        for (u32 i = 0u; i < count; ++i)
            if (entries[i].published && remove(entries[i].path) != 0)
                fprintf(stderr, "error: cannot remove owned partial actor capture: %s\n", strerror(errno));
        if (rmdir(directory) != 0)
            fprintf(stderr, "error: cannot remove owned partial batch directory: %s\n", strerror(errno));
    }
    return ok;
}
