#ifndef BLUEWAKE_REL_ARCHIVE_H
#define BLUEWAKE_REL_ARCHIVE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline uint32_t bluewake_archive_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static inline uint8_t* yaz0_decode(const uint8_t* in, size_t in_size, size_t* out_size) {
    if (in_size < 16 || memcmp(in, "Yaz0", 4) != 0)
        return NULL;
    const size_t total = bluewake_archive_be32(in + 4);
    if (total == 0 || total > (64u << 20))
        return NULL;
    uint8_t* out = (uint8_t*)malloc(total);
    if (out == NULL)
        return NULL;
    size_t s = 16, d = 0;
    while (d < total) {
        if (s >= in_size)
            goto bad;
        uint8_t code = in[s++];
        for (int bit = 0; bit < 8 && d < total; ++bit, code <<= 1) {
            if (code & 0x80) {
                if (s >= in_size)
                    goto bad;
                out[d++] = in[s++];
                continue;
            }
            if (s + 1 >= in_size)
                goto bad;
            const uint8_t b1 = in[s], b2 = in[s + 1];
            s += 2;
            const size_t dist = ((size_t)(b1 & 0x0F) << 8 | b2) + 1;
            size_t count = b1 >> 4;
            if (count == 0) {
                if (s >= in_size)
                    goto bad;
                count = (size_t)in[s++] + 0x12;
            } else {
                count += 2;
            }
            if (dist > d || d + count > total)
                goto bad;
            for (size_t i = 0; i < count; ++i, ++d)
                out[d] = out[d - dist];
        }
    }
    *out_size = total;
    return out;
bad:
    free(out);
    return NULL;
}

typedef struct BluewakeRelArchive {
    const uint8_t* bytes;
    size_t size;
    uint32_t count;
    size_t entries;
    size_t strings;
    size_t strings_size;
    size_t data;
} BluewakeRelArchive;

static inline int bluewake_rel_archive_init(BluewakeRelArchive* view, const uint8_t* bytes, size_t size) {
    if (size < 0x40u || memcmp(bytes, "RARC", 4u) != 0)
        return -1;
    const uint64_t entries = (uint64_t)bluewake_archive_be32(bytes + 0x2Cu) + 0x20u;
    const uint64_t strings = (uint64_t)bluewake_archive_be32(bytes + 0x34u) + 0x20u;
    const uint64_t data = (uint64_t)bluewake_archive_be32(bytes + 0xCu) + 0x20u;
    const uint32_t count = bluewake_archive_be32(bytes + 0x28u);
    const uint32_t strings_size = bluewake_archive_be32(bytes + 0x30u);
    if (entries + (uint64_t)count * 0x14u > size || strings + strings_size > size ||
        strings_size == 0u || data > size)
        return -1;
    *view = (BluewakeRelArchive){bytes, size, count, (size_t)entries, (size_t)strings,
                                strings_size, (size_t)data};
    return 0;
}

static inline int bluewake_rel_archive_member(const BluewakeRelArchive* view, uint32_t index,
                                              const char** name, const uint8_t** bytes, size_t* size) {
    if (index >= view->count)
        return -1;
    const uint8_t* entry = view->bytes + view->entries + (size_t)index * 0x14u;
    const uint32_t type_name = bluewake_archive_be32(entry + 4u);
    if (((type_name >> 24u) & 1u) == 0u)
        return 0;
    const size_t string = type_name & 0xFFFFu;
    if (string >= view->strings_size ||
        memchr(view->bytes + view->strings + string, 0, view->strings_size - string) == NULL)
        return -1;
    const uint64_t offset = (uint64_t)view->data + bluewake_archive_be32(entry + 8u);
    const size_t length = bluewake_archive_be32(entry + 12u);
    if (offset + length > view->size)
        return -1;
    *name = (const char*)view->bytes + view->strings + string;
    *bytes = view->bytes + (size_t)offset;
    *size = length;
    return 1;
}

#endif
