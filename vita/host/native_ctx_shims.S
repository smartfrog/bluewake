    .syntax unified
    .arch armv7-a
    .fpu neon
    .thumb
    .text

    .equ FRAME, 336
    .equ ENTRY, 16
    .equ RETURNED, 176

    .macro capture base, original_sp
    str r12, [sp, #(\base + 48)]
    add.w r12, sp, #\base
    stmia r12, {r0-r11}
    .if \original_sp
    add.w r0, sp, #FRAME
    .else
    mov r0, sp
    .endif
    str r0, [sp, #(\base + 52)]
    str lr, [sp, #(\base + 56)]
    mrs r0, apsr
    str r0, [sp, #(\base + 60)]
    add.w r0, sp, #(\base + 64)
    vstmia r0, {d0-d7}
    vmrs r0, fpscr
    str r0, [sp, #(\base + 128)]
    .endm

    .macro restore base
    ldr r0, [sp, #(\base + 128)]
    vmsr fpscr, r0
    add.w r0, sp, #(\base + 64)
    vldmia r0, {d0-d7}
    ldr r0, [sp, #(\base + 60)]
    msr APSR_nzcvq, r0
    add.w r12, sp, #\base
    ldmia r12, {r0-r12}
    .endm

/* Each signature declares exactly its stack words: never read unused caller memory. */
    .macro target_address target, slot
    .if \slot >= 0
    ldr r12, =ww_native_targets
    ldr r12, [r12, #(\slot * 4)]
    .else
    ldr r12, =\target
    .endif
    .endm

    .macro shim name, target, surface, words=0, slot=-1, tail=0
    .global \name
    .type \name, %function
    .thumb_func
\name:
    push {r12, lr}
    mrs r12, apsr
    push {r12, lr}
    ldr r12, =ww_native_active
    ldr r12, [r12]
    cmp r12, #0
    beq.w 90f
    pop {r12, lr}
    msr APSR_nzcvq, r12
    pop {r12, lr}
    sub.w sp, sp, #FRAME
    capture ENTRY, 1
    target_address \target, \slot
    str r12, [sp, #(ENTRY + 132)]
    movw r0, #\surface
    str r0, [sp, #(ENTRY + 136)]
    ldr r0, =\name
    str r0, [sp, #(ENTRY + 156)]
    movs r0, #0
    str r0, [sp, #(ENTRY + 140)]
    str r0, [sp, #(ENTRY + 144)]
    str r0, [sp, #(ENTRY + 148)]
    str r0, [sp, #(ENTRY + 152)]
    .if \words > 0
    ldr r0, [sp, #FRAME]
    str r0, [sp, #(ENTRY + 144)]
    .endif
    .if \words > 1
    ldr r0, [sp, #(FRAME + 4)]
    str r0, [sp, #(ENTRY + 148)]
    .endif
    .if \words > 2
    ldr r0, [sp, #(FRAME + 8)]
    str r0, [sp, #(ENTRY + 152)]
    .endif
    add.w r0, sp, #ENTRY
    bl ww_native_trace_enter
    .if \tail
    restore ENTRY
    ldr lr, [sp, #(ENTRY + 56)]
    ldr r12, [sp, #(ENTRY + 132)]
    add.w sp, sp, #FRAME
    bx r12
    .else
    .if \words > 0
    ldr r0, [sp, #(ENTRY + 144)]
    str r0, [sp]
    .endif
    .if \words > 1
    ldr r0, [sp, #(ENTRY + 148)]
    str r0, [sp, #4]
    .endif
    .if \words > 2
    ldr r0, [sp, #(ENTRY + 152)]
    str r0, [sp, #8]
    .endif
    restore ENTRY
    ldr r12, [sp, #(ENTRY + 132)]
    blx r12
    str r4, [sp, #(RETURNED + 16)]
    capture RETURNED, 0
    add.w r0, sp, #ENTRY
    add.w r1, sp, #RETURNED
    bl ww_native_trace_return
    restore RETURNED
    ldr lr, [sp, #(ENTRY + 56)]
    add.w sp, sp, #FRAME
    bx lr
    .endif
90:
    pop {r12, lr}
    msr APSR_nzcvq, r12
    pop {r12, lr}
    target_address \target, \slot
    bx r12
    .size \name, .-\name
    .ltorg
    .endm

    shim ww_native_read, 0, 1, 0, 0
    shim ww_native_write, 0, 2, 1, 1
    shim ww_native_spr_read, 0, 3, 0, 2
    shim ww_native_spr_write, 0, 4, 0, 3
    shim ww_native_cache, 0, 5, 0, 4
    shim ww_native_fallback, 0, 6, 0, 5
    shim ww_native_host_call, 0, 7, 0, 6
    shim __wrap_ww_host_edge, __real_ww_host_edge, 8
    shim __wrap_ppc_guest_alias_resolve, __real_ppc_guest_alias_resolve, 9
    shim __wrap_func_802AD6E0, __real_func_802AD6E0, 10, 0, -1, 1
    shim __wrap_sceIoPread, __real_sceIoPread, 11, 2
    shim __wrap_ppc_fadds, __real_ppc_fadds, 12
    shim __wrap_ppc_fcmp, __real_ppc_fcmp, 13
    shim __wrap_ppc_fctiw, __real_ppc_fctiw, 14
    shim __wrap_ppc_fdivs, __real_ppc_fdivs, 15
    shim __wrap_ppc_fmul, __real_ppc_fmul, 16
    shim __wrap_ppc_fmuls, __real_ppc_fmuls, 17
    shim __wrap_ppc_fp_available, __real_ppc_fp_available, 18
    shim __wrap_ppc_frsp, __real_ppc_frsp, 19
    shim __wrap_ppc_frsqrte, __real_ppc_frsqrte, 20
    shim __wrap_ppc_fsub, __real_ppc_fsub, 21
    shim __wrap_ppc_fsubs, __real_ppc_fsubs, 22
    shim __wrap_ppc_psq_load, __real_ppc_psq_load, 23, 3
    shim __wrap_ppc_psq_store, __real_ppc_psq_store, 24, 3
    shim ww_native_journal, 0, 25, 0, 7

    .section .note.GNU-stack,"",%progbits
