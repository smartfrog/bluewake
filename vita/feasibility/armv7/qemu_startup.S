/*
 * Minimal bare-metal startup for the VITA-WW-11 QEMU harness.
 *
 * QEMU vexpress-a9 reset leaves the stack pointer and VFP disabled, so this
 * startup sets SP to the top of the 128 MiB vexpress RAM (base 0x60000000, see
 * the runner's -m 128M), enables coprocessor 10/11 access (CPACR) and the VFP
 * unit (FPEXC.EN), then calls main and terminates through semihosting.
 */

    .syntax unified
    .arm
    .global _start
    .text
_start:
    ldr sp, =0x67ff0000
    mrc p15, 0, r0, c1, c0, 2
    orr r0, r0, #(0xf << 20)
    mcr p15, 0, r0, c1, c0, 2
    mov r0, #(1 << 30)
    vmsr fpexc, r0
    bl main
    mov r0, #0x18
    ldr r1, =0x20026
    svc 0x123456
1:  b 1b
