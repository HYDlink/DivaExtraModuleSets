; Test helper for verifying LTCG volatile-register preservation across MASM thunks.

OPTION CASEMAP:NONE

.code

; uint64_t CallWithSentinels(void* fn, uint64_t a1, uint64_t a2, uint64_t a3, SentinelState* outAfter)
CallWithSentinels PROC FRAME
    push rbx
    .pushreg rbx
    push rsi
    .pushreg rsi
    sub rsp, 28h
    .allocstack 28h
    .endprolog

    mov rbx, rcx
    mov rsi, qword ptr [rsp+60h]

    mov rcx, rdx
    mov rdx, r8
    mov r8, r9
    mov r9, 09999999999999999h
    mov r10, 0A0A0A0A0A0A0A0A0h
    mov r11, 0B1B1B1B1B1B1B1B1h

    movups xmm0, xmmword ptr [rsi+56]
    movups xmm1, xmmword ptr [rsi+72]
    movups xmm2, xmmword ptr [rsi+88]
    movups xmm3, xmmword ptr [rsi+104]
    movups xmm4, xmmword ptr [rsi+120]
    movups xmm5, xmmword ptr [rsi+136]

    call rbx

    mov qword ptr [rsi+0], rax
    mov qword ptr [rsi+8], rcx
    mov qword ptr [rsi+16], rdx
    mov qword ptr [rsi+24], r8
    mov qword ptr [rsi+32], r9
    mov qword ptr [rsi+40], r10
    mov qword ptr [rsi+48], r11
    movups xmmword ptr [rsi+56], xmm0
    movups xmmword ptr [rsi+72], xmm1
    movups xmmword ptr [rsi+88], xmm2
    movups xmmword ptr [rsi+104], xmm3
    movups xmmword ptr [rsi+120], xmm4
    movups xmmword ptr [rsi+136], xmm5

    add rsp, 28h
    pop rsi
    pop rbx
    ret
CallWithSentinels ENDP

; uint64_t ClobberVolatilesAndReturn(uint64_t retVal)
ClobberVolatilesAndReturn PROC
    mov rax, rcx
    mov rcx, 01111111111111111h
    mov rdx, 02222222222222222h
    mov r8, 03333333333333333h
    mov r9, 04444444444444444h
    mov r10, 05555555555555555h
    mov r11, 06666666666666666h
    pxor xmm0, xmm0
    pcmpeqd xmm1, xmm1
    pcmpeqd xmm2, xmm2
    pxor xmm3, xmm3
    pcmpeqd xmm4, xmm4
    pxor xmm5, xmm5
    ret
ClobberVolatilesAndReturn ENDP

END
