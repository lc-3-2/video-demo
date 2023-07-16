    .section .startup, "awx"
    .global _start
_start:

    ; Initialize the registers
    ; Set them all to zero for safety
    AND R0, R0, 0
    AND R1, R1, 0
    AND R2, R2, 0
    AND R3, R3, 0
    AND GP, GP, 0
    AND FP, FP, 0
    AND LR, LR, 0

    ; Initialize the stack pointer
    ; Recall that the argument is 32-bit *signed*, so we have to flip the bits
    PSEUDO.LOADCONSTW R6, ~0x0fffffff

    ; Call main and put the return value in R0
    ; Pass argc and argv just in case
    ADD SP, SP, -8
    STW R0, SP, 0
    STW R0, SP, 1
    JSR main
    LDW R0, SP, 0
    ADD SP, SP, 12

    ; Die
    HALT
