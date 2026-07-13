/**
  ******************************************************************************
  * @file      sfu_se_mpu.s
  * @brief     SE interface assembly stubs — STM32F072 (Cortex-M0 / Thumb-1).
  *            Rewritten from the STM32H7 reference: ARMv6-M has no IT blocks,
  *            no MRSEQ/MRSNE, no LDR into SP and no barrel-shifter immediate
  *            operands, so every sequence below uses plain Thumb-1 encodings.
  *            Provides:
  *              SVC_Handler        - Redirects SVC calls to MPU_SVC_Handler
  *              jump_to_function   - Initialise SP and jump via PC
  *              launch_application - Exception-return into user application
  ******************************************************************************
  */

  .section  .text,"ax",%progbits
  .syntax unified
  .cpu cortex-m0
  .thumb

/**
  * @brief  SVC_Handler - forward to MPU_SVC_Handler with the caller's stacked
  *         frame as first argument.
  * @note   EXC_RETURN bit 2 (in LR on exception entry) tells which stack the
  *         exception frame was pushed onto: 0 = MSP (privileged caller),
  *         1 = PSP (unprivileged caller).
  */
  .weak   MPU_SVC_Handler
  .global SVC_Handler
  .type   SVC_Handler, %function
SVC_Handler:
  MOV   R1, LR
  MOVS  R2, #4
  TST   R1, R2
  BEQ   .Lsvc_msp
  MRS   R0, PSP
  B     .Lsvc_dispatch
.Lsvc_msp:
  MRS   R0, MSP
.Lsvc_dispatch:
  /* Thumb-1 B has only +/-2KB range: dispatch through a register so the
   * handler can live anywhere in the bootloader image. LR still holds
   * EXC_RETURN, so MPU_SVC_Handler's own return performs the exception exit. */
  LDR   R1, =MPU_SVC_Handler
  BX    R1
  .ltorg
  .size SVC_Handler, .-SVC_Handler

/**
  * @brief  jump_to_function - Load SP and PC from a vector table entry
  *         R0 = pointer to {SP, PC} pair
  * @note   Kept in .text (executed in place from flash): this port's startup
  *         does not copy .RamFunc into RAM.
  */
  .global jump_to_function
  .type   jump_to_function, %function
jump_to_function:
  LDR   R1, [R0]        /* initial SP value */
  MOV   SP, R1
  LDR   R1, [R0, #4]    /* reset handler */
  BX    R1
  .size jump_to_function, .-jump_to_function

/**
  * @brief  launch_application - Use an exception return to branch into the user
  *         application in Thread mode (only used in the unprivileged-boot model;
  *         the app-privileged model uses the direct jump in sfu_fwimg_common.c).
  *         R0 = application vector-table base address
  *         R1 = jump_to_function address (kept for call-site compatibility; unused)
  * @note   Runs in Handler mode (from the SVC handler): builds a basic exception
  *         frame on the CURRENT stack (MSP) and returns with EXC_RETURN=0xFFFFFFF9
  *         (Thread mode, MSP). The stacked PC is the application reset handler;
  *         the application startup sets its own SP as its first instruction.
  */
  .global launch_application
  .type   launch_application, %function
launch_application:
  LDR   R2, [R0, #4]    /* R2 = application reset handler = vector[1] */
  MOVS  R3, #1
  ORRS  R2, R2, R3      /* ensure Thumb bit set */
  MOVS  R3, #1
  LSLS  R3, R3, #24     /* R3 = 0x01000000 : xPSR Thumb bit */
  PUSH  {R3}            /* xPSR */
  PUSH  {R2}            /* PC  = application reset handler */
  MOVS  R3, #0          /* zero the remaining context registers */
  PUSH  {R3}            /* LR  */
  PUSH  {R3}            /* R12 */
  PUSH  {R3}            /* R3  */
  PUSH  {R3}            /* R2  */
  PUSH  {R3}            /* R1  */
  PUSH  {R3}            /* R0  */
  MOVS  R3, #6
  MVNS  R3, R3          /* R3 = 0xFFFFFFF9 : Thread mode, MSP, basic frame */
  MOV   LR, R3
  BX    LR              /* exception return into the application */
  .size launch_application, .-launch_application

  .end
