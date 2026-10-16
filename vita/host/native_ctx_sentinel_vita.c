#include "native_ctx.h"

#include <psp2/io/fcntl.h>

static int write_capsule(void* user, uint32_t offset, const void* bytes, uint32_t size) {
    (void)user;
    static SceUID fd = -1;
    const uint32_t bound = sizeof(WwAbiDump) + sizeof(WwAbiPair);
    if (offset > bound || size > bound - offset)
        return -1;
    if (offset == 0u)
        fd = sceIoOpen("ux0:data/bluewake-ww-fixture/dispatch-abi-capsule.bin",
                       SCE_O_WRONLY | SCE_O_CREAT | SCE_O_EXCL, 0600);
    if (fd < 0)
        return -1;
    if (sceIoLseek(fd, offset, SCE_SEEK_SET) != offset) {
        const int closed = sceIoClose(fd);
        fd = -1;
        return closed < 0 ? closed : -1;
    }
    const int written = sceIoWrite(fd, bytes, size);
    if (written != (int)size || offset == offsetof(WwAbiDump, complete)) {
        const int closed = sceIoClose(fd);
        fd = -1;
        if (closed < 0)
            return -1;
    }
    return written;
}

void ww_abi_install(void* cpu) {
    ww_abi_init((uint32_t)(uintptr_t)cpu, write_capsule, NULL);
}
