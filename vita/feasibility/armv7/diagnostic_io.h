#ifndef WW_DIAGNOSTIC_IO_H
#define WW_DIAGNOSTIC_IO_H
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static bool diagnostic_hex(const char* text, size_t length) {
    if (strlen(text) != length)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (!isxdigit((unsigned char)text[i]))
            return false;
    return true;
}

static int diagnostic_request(const char* path, const char* identity,
                              char* token, char* source, char* package) {
    struct stat info;
    if (lstat(path, &info) != 0 || !S_ISREG(info.st_mode))
        return 1;
    FILE* stream = fopen(path, "r");
    if (!stream)
        return 1;
    int fields = fscanf(stream, "%32s %40s %64s", token, source, package);
    char extra;
    int trailing = fscanf(stream, " %c", &extra);
    int error = ferror(stream);
    int closed = fclose(stream);
    return fields != 3 || trailing != EOF || error || closed != 0 ||
           !diagnostic_hex(token, 32) || !diagnostic_hex(source, 40) ||
           !diagnostic_hex(package, 64) || strcmp(source, identity) != 0;
}

static bool diagnostic_absent(const char* path) {
    struct stat info;
    return lstat(path, &info) < 0 && errno == ENOENT;
}
#endif
