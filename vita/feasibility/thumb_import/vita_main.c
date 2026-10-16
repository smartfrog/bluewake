#include "probe.h"
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>

static int log_fd;

int probe_control(void) {
    const int fd = sceIoOpen("app0:eboot.bin", SCE_O_RDONLY, 0);
    if (fd < 0)
        return fd;
    return sceIoClose(fd);
}

bool probe_record(const char *text, size_t bytes) {
    return sceIoWrite(log_fd, text, bytes) == (int)bytes;
}

int main(void) {
    log_fd = sceIoOpen("ux0:data/public-thumb-import-probe.log",
                       SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0600);
    int status = log_fd < 0 ? 4 : probe_run();
    if (log_fd >= 0 && sceIoClose(log_fd) < 0)
        status = 4;
    sceKernelExitProcess(status);
    return status;
}
