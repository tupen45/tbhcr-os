.section .text
    .globl sched_ctx_switch

/* void sched_ctx_switch(uint32_t *old_esp, uint32_t new_esp) */
sched_ctx_switch:
    mov 4(%esp), %eax   /* old_esp pointer */
    mov 8(%esp), %edx   /* new_esp value */
    push %ebp
    push %ebx
    push %esi
    push %edi
    mov %esp, (%eax)    /* save current stack pointer */
    mov %edx, %esp      /* switch to target stack pointer */
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret
