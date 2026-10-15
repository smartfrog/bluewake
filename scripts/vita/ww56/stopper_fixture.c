#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ucontext.h>

extern void ww123_read(void *, uint32_t);
extern void ww123_fault(void *);
static uint32_t invalid_address = 0xd71;
static int native_fault;
static volatile sig_atomic_t native_fault_seen;

__asm__(
    ".text\n"
    ".global ww123_read\n"
    ".type ww123_read,%function\n"
    "ww123_read:\n"
    "sub sp,sp,#0x440\n"
    "stp x29,x30,[sp]\n"
    "mov x29,sp\n"
    "stp x19,x20,[sp,#16]\n"
    "stp x23,x24,[sp,#48]\n"
    "mov x24,x0\n"
    "str w1,[sp,#108]\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "stp x21,x22,[sp,#32]\n"
    "ldp x29,x30,[sp]\n"
    "ldp x19,x20,[sp,#16]\n"
    "ldp x23,x24,[sp,#48]\n"
    "add sp,sp,#0x440\n"
    "ret\n"
    ".size ww123_read,.-ww123_read\n"
    ".global ww123_fault\n"
    ".type ww123_fault,%function\n"
    "ww123_fault:\n"
    "ldr w0,[x0]\n"
    "ret\n"
    ".size ww123_fault,.-ww123_fault\n");

static void handle_native_fault(int sig, siginfo_t *info, void *context) {
    if (sig != SIGSEGV || info->si_addr != (void *)(uintptr_t)0xd71) _exit(4);
    ((ucontext_t *)context)->uc_mcontext.pc += 4;
    native_fault_seen = 1;
}

static void *worker(void *unused) {
    (void)unused;
    sleep(1);
    if (native_fault) {
        ww123_fault((void *)(uintptr_t)0xd71);
        if (!native_fault_seen) return (void *)1;
        puts("NATIVE_FAULT_HANDLED");
    }
    ww123_read(NULL, invalid_address);
    puts("AFTER_TRIGGER");
    return NULL;
}

int main(int argc, char **argv) {
    pthread_t thread;
    struct sigaction action = {.sa_sigaction = handle_native_fault, .sa_flags = SA_SIGINFO};
    if (sigaction(SIGSEGV, &action, NULL) != 0) return 4;
    native_fault = argc > 1 && (argv[1][0] == 'f' || argv[1][0] == 's');
    setbuf(stdout, NULL);
    puts("READY");
    sleep(1);
    if (argc > 1 && argv[1][0] == 't') {
        sleep(5);
    } else if (argc > 1 && argv[1][0] == 's') {
        worker(NULL);
    } else {
        if (argc > 1 && argv[1][0] == 'm') invalid_address = 0xd75;
        if (pthread_create(&thread, NULL, worker, NULL) != 0) return 2;
        if (pthread_join(thread, NULL) != 0) return 3;
    }
    puts("DONE");
    return 0;
}
