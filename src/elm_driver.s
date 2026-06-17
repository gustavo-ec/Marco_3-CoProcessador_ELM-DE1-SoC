.syntax unified
.arm
.text
.align 4

@ =========================================================================
@ elm_driver.s - Driver Assembly para o Co-processador ELM (DE1-SoC)
@ =========================================================================

.global elm_init_asm
.global elm_close_asm
.global processar_hardware_asm
.type elm_init_asm,       %function
.type elm_close_asm,      %function
.type processar_hardware_asm, %function

@ --- Offsets dos PIOs ---
.equ PIO_DATA_IN_OFFSET,  0x30
.equ PIO_DATA_OUT_OFFSET, 0x40
.equ PIO_SIGNALS_OFFSET,  0x50

@ --- Syscalls Linux ARM EABI ---
.equ SYS_OPEN,   5
.equ SYS_CLOSE,  6
.equ SYS_MUNMAP, 91
.equ SYS_MMAP2,  192

elm_init_asm:
    push    {r4-r7, lr}
    mov     r6, r0                  
    ldr     r0, =dev_mem_path
    ldr     r1, =0x101002           
    mov     r7, #SYS_OPEN
    svc     #0
    cmp     r0, #0
    blt     init_fail               
    mov     r7, r0                  
    mov     r0, #0
    mov     r1, #0x200000
    mov     r2, #3
    mov     r3, #1
    mov     r4, r7                  
    ldr     r5, =0xFF200            
    mov     r7, #SYS_MMAP2
    svc     #0
    cmn     r0, #4096
    bhi     init_fail_close_fd
    cmp     r6, #0
    beq     init_done
    str     r4, [r6]                
init_done:
    pop     {r4-r7, pc}

init_fail_close_fd:
    mov     r0, r4                  
    mov     r7, #SYS_CLOSE
    svc     #0
init_fail:
    mov     r0, #0                  
    pop     {r4-r7, pc}

elm_close_asm:
    push    {r4-r5, lr}
    mov     r4, r0                  
    mov     r5, r1                  
    cmp     r4, #0
    beq     close_skip_munmap
    mov     r0, r4
    mov     r1, #0x200000
    mov     r7, #SYS_MUNMAP
    svc     #0
close_skip_munmap:
    cmp     r5, #0
    blt     close_done
    mov     r0, r5
    mov     r7, #SYS_CLOSE
    svc     #0
close_done:
    pop     {r4-r5, pc}

processar_hardware_asm:
    push    {r4-r11, lr}
    mov     r10, r0                 
    mov     r11, r1                 
    mov     r9,  r2                 
    mov     r8,  r3                 
    mov     r5,  #0                 
    cmp     r8, #0
    bne     base_ok
    mvn     r5, #0                  
    b       proc_ret
base_ok:
    add     r1, r8, #PIO_DATA_IN_OFFSET
    add     r2, r8, #PIO_SIGNALS_OFFSET
    add     r12, r8, #PIO_DATA_OUT_OFFSET

    cmp     r11, #0
    beq     do_image
    cmp     r11, #1
    beq     do_weights
    cmp     r11, #3
    beq     do_bias
    cmp     r11, #4
    beq     do_beta
    cmp     r11, #5
    beq     do_start
    cmp     r11, #6
    beq     do_status
    cmp     r11, #7
    beq     do_clear
    cmp     r11, #8                 @ <--- NOVA TAREFA: RESET
    beq     do_reset
    b       proc_ret                

do_image:
    mov     r4, #0
image_loop:
    ldrb    r3, [r10], #1           
    mov     r7, #0                  
    lsl     r14, r4, #3             
    orr     r7, r7, r14
    lsl     r3, r3, #13             
    orr     r7, r7, r3
    str     r7, [r1]                
    bl      pulse_hw                
    bl      clear_hw                
    add     r4, r4, #1
    cmp     r4, r9
    blt     image_loop
    b       proc_ret

do_weights:
    mov     r4, #0
weights_loop:
    ldrh    r3, [r10], #2           
    mov     r7, #0
    lsl     r14, r4, #3             
    orr     r7, r7, r14
    orr     r7, r7, #1              
    str     r7, [r1]
    bl      pulse_hw
    bl      clear_hw
    mov     r7, #0
    lsl     r3, r3, #3              
    orr     r7, r7, r3
    orr     r7, r7, #2              
    str     r7, [r1]
    bl      pulse_hw
    bl      clear_hw
    add     r4, r4, #1
    cmp     r4, r9
    blt     weights_loop
    b       proc_ret

do_bias:
    mov     r4, #0
bias_loop:
    ldrh    r3, [r10], #2
    mov     r7, #0
    lsl     r14, r4, #3             
    orr     r7, r7, r14
    lsl     r3, r3, #10             
    orr     r7, r7, r3
    orr     r7, r7, #3              
    str     r7, [r1]
    bl      pulse_hw
    bl      clear_hw
    add     r4, r4, #1
    cmp     r4, r9
    blt     bias_loop
    b       proc_ret

do_beta:
    mov     r4, #0
beta_loop:
    ldrh    r3, [r10], #2
    mov     r7, #0
    lsl     r14, r4, #3             
    orr     r7, r7, r14
    lsl     r3, r3, #14             
    orr     r7, r7, r3
    orr     r7, r7, #4              
    str     r7, [r1]
    bl      pulse_hw
    bl      clear_hw
    add     r4, r4, #1
    cmp     r4, r9
    blt     beta_loop
    b       proc_ret

do_start:
    mov     r7, #5                  
    str     r7, [r1]
    bl      pulse_hw
    bl      clear_hw               
    b       proc_ret

do_status:
    ldr     r5, [r12]               
    b       proc_ret

do_clear:
    bl      clear_hw
    b       proc_ret

@ ---------- RESET (tarefa 8) ----------
do_reset:
    bl      reset_hw
    b       proc_ret

proc_ret:
    mov     r0, r5
    pop     {r4-r11, pc}

@ =========================================================================
@ Sub-rotinas de handshake
@ =========================================================================
pulse_hw:
    push    {r3, lr}
    mov     r3, #1
    str     r3, [r2]                
    mov     r3, #200             
1:  subs    r3, r3, #1
    bne     1b
    mov     r3, #0
    str     r3, [r2]                
    pop     {r3, pc}

clear_hw:
    push    {r3, lr}
    mov     r3, #2
    str     r3, [r2]                
    mov     r3, #200
2:  subs    r3, r3, #1
    bne     2b
    mov     r3, #0
    str     r3, [r2]                
    pop     {r3, pc}

@ reset_hw: pulsa bit 2 (reset)
reset_hw:
    push    {r3, lr}
    mov     r3, #4                  @ bit 2 = 1 (reset)
    str     r3, [r2]                @ escreve em PIO_SIGNALS
    mov     r3, #200              @ delay para o clock da FPGA
3:  subs    r3, r3, #1
    bne     3b
    mov     r3, #0
    str     r3, [r2]                @ bit 2 = 0 (libera reset)
    pop     {r3, pc}

.section .rodata
dev_mem_path:
    .asciz "/dev/mem"

.section .note.GNU-stack,"",%progbits