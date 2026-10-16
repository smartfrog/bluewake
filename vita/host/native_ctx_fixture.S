    .syntax unified
    .arch armv7-a
    .fpu neon
    .text
    .arm
    .global semihost
    .type semihost, %function
semihost:
    svc 0x123456
    bx lr

    .global ww_native_fixture_invoke
    .type ww_native_fixture_invoke, %function
ww_native_fixture_invoke:
    push {r4-r8, lr}
    vpush {d8-d15}
    sub sp, sp, #16
    mov r12, r0
    ldr r0, =fixture_ctx
    ldr r0, [r0]
    mov r1, #0x11
    mov r2, #0x22
    mov r3, #0x33
    str r1, [sp]
    str r2, [sp, #4]
    str r3, [sp, #8]
    movw r4, #0x1234
    vmov d0, r1, r2
    vmov d1, r2, r3
    vmov d8, r1, r3
    vmov d9, r1, r3
    vmov d10, r1, r3
    vmov d11, r1, r3
    vmov d12, r1, r3
    vmov d13, r1, r3
    vmov d14, r1, r3
    vmov d15, r1, r3
    ldr r5, =fixture_sp
    str sp, [r5]
    blx r12
    ldr r12, =fixture_result
    stmia r12!, {r0-r4}
    vstmia r12!, {d0-d1}
    vstmia r12!, {d8}
    str sp, [r12]
    ldr r12, =fixture_vfp_saved
    vstmia r12, {d8-d15}
    add sp, sp, #16
    vpop {d8-d15}
    pop {r4-r8, pc}
    .size ww_native_fixture_invoke, .-ww_native_fixture_invoke

    .global fixture_target_arm
    .type fixture_target_arm, %function
fixture_target_arm:
    b fixture_target_common

    .thumb
    .global fixture_target_thumb
    .type fixture_target_thumb, %function
    .thumb_func
fixture_target_thumb:
    ldr r12, =fixture_args
    stmia r12!, {r0-r3}
    ldr r0, [sp]
    ldr r1, [sp, #4]
    ldr r2, [sp, #8]
    stmia r12!, {r0-r2}
    ldr r12, =fixture_clobber
    ldr r12, [r12]
    cmp r12, #0
    it ne
    movne r4, #1
    movw r0, #0xcdef
    movt r0, #0x89ab
    movw r1, #0x4567
    movt r1, #0x0123
    vadd.f64 d0, d0, d1
    bx lr

    .global fixture_tail_target
    .type fixture_tail_target, %function
    .thumb_func
fixture_tail_target:
    ldr r12, =fixture_tail
    str r0, [r12]
    str sp, [r12, #4]
    str lr, [r12, #8]
    bx lr

    .global fixture_nested_target
    .type fixture_nested_target, %function
    .thumb_func
fixture_nested_target:
    push {r5, lr}
    ldr r5, =fixture_nesting
    ldr r12, [r5]
    sub.w r12, r12, #1
    str r12, [r5]
    cmp r12, #0
    beq 1f
#if !WW05_DISPATCH_ABI_SENTINEL
    bl ww_native_spr_read
#endif
1:
    pop {r5, lr}
    b fixture_target_thumb

    .global fixture_observer_scramble
    .type fixture_observer_scramble, %function
    .thumb_func
fixture_observer_scramble:
    movs r0, #3
    vmov d0, r0, r0
    vmov d1, r0, r0
    vmov d2, r0, r0
    vmov d3, r0, r0
    bx lr

    .arm
fixture_target_common:
    ldr r12, =fixture_target_thumb
    bx r12

    .macro real name, target=fixture_target_thumb
    .global __real_\name
    .set __real_\name, \target
    .endm
    real ww_host_edge
    real ppc_guest_alias_resolve
    real func_802AD6E0, fixture_tail_target
    real sceIoPread
    real ppc_fadds
    real ppc_fcmp
    real ppc_fctiw
    real ppc_fdivs
    real ppc_fmul
    real ppc_fmuls
    real ppc_fp_available
    real ppc_frsp
    real ppc_frsqrte
    real ppc_fsub
    real ppc_fsubs
    real ppc_psq_load
    real ppc_psq_store

#if WW05_DISPATCH_ABI_SENTINEL
    .thumb
    .global fixture_abi_invoke
    .type fixture_abi_invoke, %function
    .thumb_func
fixture_abi_invoke:
    ldr r2, =fixture_abi_target
    ldr r2, [r2]
    b ww_abi_gate

    .arm
    .global fixture_abi_arm
    .type fixture_abi_arm, %function
fixture_abi_arm:
    ldr r12, =fixture_abi_thumb
    bx r12

    .thumb
    .global fixture_abi_thumb
    .type fixture_abi_thumb, %function
    .thumb_func
fixture_abi_thumb:
    ldr r12, =fixture_args
    ldr r3, [r12]
    adds r3, #1
    str r3, [r12]
    ldr r12, =fixture_clobber
    ldr r12, [r12]
    cmp r12, #4
    bne 2f
    push {r4, lr}
    bl fixture_abi_invoke
    pop {r4, lr}
    movs r0, #1
    bx lr
2:
    cmp r12, #1
    it eq
    moveq r4, #1
    cmp r12, #2
    it eq
    addeq sp, #72
    cmp r12, #3
    bne 1f
    vmov d15, r0, r0
1:
    movs r0, #1
    bx lr
#endif
    .section .note.GNU-stack,"",%progbits
