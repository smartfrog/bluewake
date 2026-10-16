#include "rejection_capsule.h"

#include <psp2/io/fcntl.h>

static int write_capsule(void* user, uint32_t offset, const void* bytes, uint32_t size) {
    (void)user;
    static SceUID fd = -1;
    const uint32_t bound = sizeof(WwRejectionCapsule);
    if (offset > bound || size > bound - offset)
        return -1;
    if (offset == 0u)
        fd = sceIoOpen(WW05_DIRECTORY "rejection-capsule.bin", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_EXCL, 0600);
    if (fd < 0)
        return -1;
    if (sceIoLseek(fd, offset, SCE_SEEK_SET) != offset) {
        const int closed = sceIoClose(fd);
        fd = -1;
        return closed < 0 ? closed : -1;
    }
    const int written = sceIoWrite(fd, bytes, size);
    if (written != (int)size || offset == offsetof(WwRejectionCapsule, complete)) {
        const int closed = sceIoClose(fd);
        fd = -1;
        if (closed < 0)
            return -1;
    }
    return written;
}

void ww_rejection_capsule_install_file(void) {
    ww_rejection_capsule_init(write_capsule, NULL);
}
