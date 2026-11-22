#include "native_hooks.h"
#include "native_layout.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#define CHECK(expr) do { \
    if(!(expr)) { \
        std::cerr<<"CHECK failed at line "<<__LINE__<<": "<<#expr<<"\n"; \
        std::abort(); \
    } \
} while(0)

using namespace diva;

namespace {
#pragma pack(push,1)
struct XmmBytes {
    uint64_t lo=0;
    uint64_t hi=0;
    bool operator==(const XmmBytes&) const=default;
};

struct SentinelState {
    uint64_t rax=0;
    uint64_t rcx=0;
    uint64_t rdx=0;
    uint64_t r8=0;
    uint64_t r9=0;
    uint64_t r10=0;
    uint64_t r11=0;
    XmmBytes xmm[6]{};
};
#pragma pack(pop)
static_assert(sizeof(SentinelState)==152,"SentinelState 布局必须与 thunk_test_helper.asm 精确对齐");

extern "C" uint64_t __fastcall CallWithSentinels(void* fn,uint64_t a1,uint64_t a2,uint64_t a3,SentinelState* outAfter);
extern "C" uint64_t __fastcall ClobberVolatilesAndReturn(uint64_t retVal);

struct ReentrancyCounters {
    int pvSelCalls=0;
    int applyOutfitCalls=0;
    int getSlotConstCalls=0;
    int getSoundCalls=0;
    void* slotConstPtr=nullptr;
    void* soundPtr=nullptr;
    std::array<uint8_t,0x1000> fakeSave{};
    std::array<uint8_t,0x1000> fakeWork{};
    std::array<uint8_t,0x1000> fakePanel{};
} g_ctx;

void* __fastcall MockGetSlotConfig(uintptr_t,int32_t,uint32_t) {
    return reinterpret_cast<void*>(ClobberVolatilesAndReturn(0x1010101010101010ULL));
}

void* __fastcall MockGetSlotConfigConst(uintptr_t,int32_t,int32_t) {
    ++g_ctx.getSlotConstCalls;
    return reinterpret_cast<void*>(ClobberVolatilesAndReturn(0x2020202020202020ULL));
}

void* __fastcall MockGetSoundConfig(uintptr_t,int32_t,char) {
    ++g_ctx.getSoundCalls;
    return reinterpret_cast<void*>(ClobberVolatilesAndReturn(0x3030303030303030ULL));
}

uint8_t __fastcall MockCycleScope() {
    return static_cast<uint8_t>(ClobberVolatilesAndReturn(1ULL));
}

void __fastcall MockApplySongOutfit(uintptr_t,int32_t pvId,uintptr_t) {
    ++g_ctx.applyOutfitCalls;
    const auto thunks=detail::GetThunkEntryPointsForTest();
    using GetSlotConstFn=void*(__fastcall*)(uintptr_t,int32_t,int32_t);
    using GetSoundFn=void*(__fastcall*)(uintptr_t,int32_t,char);
    g_ctx.slotConstPtr=reinterpret_cast<GetSlotConstFn>(thunks.getSlotConfigConst)(
        reinterpret_cast<uintptr_t>(g_ctx.fakeSave.data()),pvId,0);
    g_ctx.soundPtr=reinterpret_cast<GetSoundFn>(thunks.getSoundConfig)(
        reinterpret_cast<uintptr_t>(g_ctx.fakeSave.data()),pvId,'\0');
    ClobberVolatilesAndReturn(0);
}

void __fastcall MockPvSelSetScopeUi(uintptr_t,int32_t) {
    ++g_ctx.pvSelCalls;
    const auto thunks=detail::GetThunkEntryPointsForTest();
    using ApplyOutfitFn=void(__fastcall*)(uintptr_t,int32_t,uintptr_t);
    reinterpret_cast<ApplyOutfitFn>(thunks.applySongOutfit)(
        reinterpret_cast<uintptr_t>(g_ctx.fakeWork.data()),637,0);
    ClobberVolatilesAndReturn(0);
}

void FillSentinelXmm(SentinelState& s) {
    for(int i=0;i<6;++i) {
        s.xmm[i].lo=0x0102030405060700ULL+static_cast<uint64_t>(i);
        s.xmm[i].hi=0xF1F2F3F4F5F6F700ULL+static_cast<uint64_t>(i);
    }
}

void AssertVolatilesPreserved(void* fn,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t expectedRet,bool checkRet) {
    SentinelState state{};
    FillSentinelXmm(state);
    const SentinelState expectedXmm=state;

    const uint64_t ret=CallWithSentinels(fn,a1,a2,a3,&state);
    CHECK(state.rcx==a1);
    CHECK(state.rdx==a2);
    CHECK(state.r8==a3);
    CHECK(state.r9==0x9999999999999999ULL);
    CHECK(state.r10==0xA0A0A0A0A0A0A0A0ULL);
    CHECK(state.r11==0xB1B1B1B1B1B1B1B1ULL);
    for(int i=0;i<6;++i) CHECK(state.xmm[i]==expectedXmm.xmm[i]);
    if(checkRet) CHECK(ret==expectedRet);
}
}

int main() {
    const auto thunks=detail::GetThunkEntryPointsForTest();
    detail::SetTestOriginalsForUnitTests(detail::HookTestPointers{
        reinterpret_cast<void*>(MockGetSlotConfig),
        reinterpret_cast<void*>(MockGetSlotConfigConst),
        reinterpret_cast<void*>(MockGetSoundConfig),
        reinterpret_cast<void*>(MockCycleScope),
        reinterpret_cast<void*>(MockApplySongOutfit),
        reinterpret_cast<void*>(MockPvSelSetScopeUi),
    });

    // 1. 验证原版回退模式（enabled=false）下 6 个 LTCG 跳板完整保留全部易失寄存器。
    ModSettings disabledSettings{};
    disabledSettings.enabled=false;
    detail::SetTestStateForUnitTests(disabledSettings,{SlotKind::Shared,0});

    const uintptr_t saveAddr=reinterpret_cast<uintptr_t>(g_ctx.fakeSave.data());
    const uintptr_t workAddr=reinterpret_cast<uintptr_t>(g_ctx.fakeWork.data());
    const uintptr_t panelAddr=reinterpret_cast<uintptr_t>(g_ctx.fakePanel.data());

    AssertVolatilesPreserved(thunks.getSlotConfig,saveAddr,637,0,0x1010101010101010ULL,true);
    AssertVolatilesPreserved(thunks.getSlotConfigConst,saveAddr,637,1,0x2020202020202020ULL,true);
    AssertVolatilesPreserved(thunks.getSoundConfig,saveAddr,637,0,0x3030303030303030ULL,true);
    AssertVolatilesPreserved(thunks.cycleScope,0xCAFE1ULL,0xCAFE2ULL,0xCAFE3ULL,1ULL,true);
    AssertVolatilesPreserved(thunks.applySongOutfit,workAddr,637,0,0ULL,false);
    AssertVolatilesPreserved(thunks.pvSelSetScopeUi,panelAddr,2,0xDEAD3ULL,0ULL,false);

    // 2. 验证扩展槽位激活（enabled=true，Song 2）时 PvSelSetScopeUi -> ApplySongOutfit -> GetSlotConfigConst + GetSoundConfig
    //    同线程重入不死锁，且所有易失寄存器依旧完整保留，同时 +0xA8C 被刷新为 -1。
    ModSettings enabledSettings{};
    enabledSettings.enabled=true;
    enabledSettings.sharedSlots=6;
    enabledSettings.songSlots=4;
    detail::SetTestStateForUnitTests(enabledSettings,{SlotKind::Song,1});

    g_ctx.pvSelCalls=0;
    g_ctx.applyOutfitCalls=0;
    g_ctx.getSlotConstCalls=0;
    g_ctx.getSoundCalls=0;
    g_ctx.slotConstPtr=nullptr;
    g_ctx.soundPtr=nullptr;
    *reinterpret_cast<int32_t*>(g_ctx.fakePanel.data()+native::pv_selector::kPanelLastScope)=2;

    AssertVolatilesPreserved(thunks.pvSelSetScopeUi,panelAddr,3,0xABCDULL,0ULL,false);
    CHECK(g_ctx.pvSelCalls==1);
    CHECK(g_ctx.applyOutfitCalls==1);
    // 扩展槽位命中内部 SlotStore，不回退到原版 GetSlotConfigConst / GetSoundConfig。
    CHECK(g_ctx.getSlotConstCalls==0);
    CHECK(g_ctx.getSoundCalls==0);
    CHECK(g_ctx.slotConstPtr!=nullptr);
    CHECK(g_ctx.soundPtr!=nullptr);
    CHECK(*reinterpret_cast<int32_t*>(g_ctx.fakePanel.data()+native::pv_selector::kPanelLastScope)==-1);

    // 3. 验证扩展槽位启用但处于原版 A..C 槽位时，PvSelSetScopeUi 重入链回退原版也不死锁。
    detail::SetTestStateForUnitTests(enabledSettings,{SlotKind::Shared,1});
    AssertVolatilesPreserved(thunks.pvSelSetScopeUi,panelAddr,1,0xBEEFULL,0ULL,false);
    CHECK(g_ctx.pvSelCalls==2);
    CHECK(g_ctx.applyOutfitCalls==2);
    CHECK(g_ctx.getSlotConstCalls==1);
    CHECK(g_ctx.getSoundCalls==1);
    CHECK(g_ctx.slotConstPtr==reinterpret_cast<void*>(0x2020202020202020ULL));
    CHECK(g_ctx.soundPtr==reinterpret_cast<void*>(0x3030303030303030ULL));

    // 4. 验证自定义界面 F4（CycleCustomizeScope）在扩展模式下切换并保护全部易失寄存器。
    AssertVolatilesPreserved(thunks.cycleScope,0x1111ULL,0x2222ULL,0x3333ULL,1ULL,true);

    std::cout<<"ThunkRegressionTests: all register-preservation and re-entrancy assertions passed.\n";
    return 0;
}
