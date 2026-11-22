; x64 MASM LTCG volatile-register-preserving thunks for DivaExtraModuleSets.
; Matches DivaModLoader's register preservation convention for LTCG-compiled
; DivaMegaMix.exe functions: saves/restores rcx, rdx, r8, r9, r10, r11 and
; xmm0..xmm5 across the C++ hook implementation while preserving rax/al return.

OPTION CASEMAP:NONE

EXTERN HookGetSlotConfigImpl:PROC
EXTERN HookGetSlotConfigConstImpl:PROC
EXTERN HookGetSoundConfigImpl:PROC
EXTERN HookCycleCustomizeScopeImpl:PROC
EXTERN HookApplySongOutfitImpl:PROC
EXTERN HookPvSelSetScopeUiImpl:PROC

.code

DEFINE_PRESERVE_THUNK MACRO ThunkName, TargetImpl
ThunkName PROC FRAME
    push rcx
    .pushreg rcx
    push rdx
    .pushreg rdx
    push r8
    .pushreg r8
    push r9
    .pushreg r9
    push r10
    .pushreg r10
    push r11
    .pushreg r11
    sub rsp, 88h
    .allocstack 88h
    .endprolog

    movaps xmmword ptr [rsp+20h], xmm0
    movaps xmmword ptr [rsp+30h], xmm1
    movaps xmmword ptr [rsp+40h], xmm2
    movaps xmmword ptr [rsp+50h], xmm3
    movaps xmmword ptr [rsp+60h], xmm4
    movaps xmmword ptr [rsp+70h], xmm5

    call TargetImpl

    movaps xmm5, xmmword ptr [rsp+70h]
    movaps xmm4, xmmword ptr [rsp+60h]
    movaps xmm3, xmmword ptr [rsp+50h]
    movaps xmm2, xmmword ptr [rsp+40h]
    movaps xmm1, xmmword ptr [rsp+30h]
    movaps xmm0, xmmword ptr [rsp+20h]

    add rsp, 88h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    ret
ThunkName ENDP
ENDM

DEFINE_PRESERVE_THUNK ThunkGetSlotConfig, HookGetSlotConfigImpl
DEFINE_PRESERVE_THUNK ThunkGetSlotConfigConst, HookGetSlotConfigConstImpl
DEFINE_PRESERVE_THUNK ThunkGetSoundConfig, HookGetSoundConfigImpl
DEFINE_PRESERVE_THUNK ThunkCycleCustomizeScope, HookCycleCustomizeScopeImpl
DEFINE_PRESERVE_THUNK ThunkApplySongOutfit, HookApplySongOutfitImpl
DEFINE_PRESERVE_THUNK ThunkPvSelSetScopeUi, HookPvSelSetScopeUiImpl

END
