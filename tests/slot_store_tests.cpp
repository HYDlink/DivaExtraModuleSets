#include "settings.h"
#include "slot_store.h"
#include <Windows.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#define CHECK(expr) do { \
    if(!(expr)) { \
        std::cerr<<"CHECK failed at line "<<__LINE__<<": "<<#expr<<"\n"; \
        std::abort(); \
    } \
} while(0)

namespace fs=std::filesystem;
using namespace diva;

int main() {
    // 1. CycleSlot 正向与反向循环测试（Shared 6 + Song 2）
    {
        ActiveSlot s{SlotKind::Shared,0};
        for(int i=0;i<5;++i) s=CycleSlot(s,+1,6,2);
        CHECK(s==ActiveSlot({SlotKind::Shared,5}));
        s=CycleSlot(s,+1,6,2);
        CHECK(s==ActiveSlot({SlotKind::Song,0}));
        s=CycleSlot(s,+1,6,2);
        CHECK(s==ActiveSlot({SlotKind::Song,1}));
        s=CycleSlot(s,+1,6,2);
        CHECK(s==ActiveSlot({SlotKind::Shared,0}));

        // 反向循环
        s=CycleSlot(s,-1,6,2);
        CHECK(s==ActiveSlot({SlotKind::Song,1}));
        s=CycleSlot(s,-1,6,2);
        CHECK(s==ActiveSlot({SlotKind::Song,0}));
        s=CycleSlot(s,-1,6,2);
        CHECK(s==ActiveSlot({SlotKind::Shared,5}));
    }

    // 2. NativeScopeFor 永不越界（永远在 0..3）
    for(int i=0;i<26;++i) {
        const int sc=NativeScopeFor({SlotKind::Shared,i});
        CHECK(sc>=0 && sc<=2);
        if(i<=2) CHECK(sc==i);
        else CHECK(sc==2);
    }
    for(int i=0;i<10;++i) {
        CHECK(NativeScopeFor({SlotKind::Song,i})==3);
    }

    // 3. 非破坏式隐藏 + .dat 序列化 / 反序列化 + 隐藏槽位恢复读取
    const fs::path tempDir=fs::temp_directory_path()/L"dems_unit_tests";
    fs::create_directories(tempDir);
    const fs::path datPath=tempDir/L"DivaExtraModuleSets.dat";
    std::error_code ec;
    fs::remove(datPath,ec);

    {
        SlotStore store;
        uint8_t defaultSound[CustomizeSet::kSoundBytes]{};
        defaultSound[0]=7;
        store.SetDefaultSoundTemplate(defaultSound);

        auto* mD=store.GetOrCreateSharedModule(3,0);
        mD->moduleId=114; mD->items[0]=201; mD->flags=1;

        auto* mF=store.GetOrCreateSharedModule(5,1);
        mF->moduleId=30; mF->items[2]=405; mF->flags=4;

        auto* s2=store.GetOrCreateSongModule(637,1,0);
        s2->moduleId=222; s2->items[4]=99; s2->flags=16;

        auto* s4=store.GetOrCreateSongModule(637,3,0);
        s4->moduleId=333;

        auto* snd=store.GetOrCreateSongSound(637,1);
        snd[0]=42;

        store.SetActiveSlot({SlotKind::Shared,5});
        CHECK(store.SaveAtomic(datPath));
        CHECK(!store.IsDirty());
    }

    // 缩小可见槽位数（模拟玩家将配置从 6+4 调小到 3+1，然后再存盘）
    {
        SlotStore store;
        uint8_t defaultSound[CustomizeSet::kSoundBytes]{};
        defaultSound[0]=7;
        store.SetDefaultSoundTemplate(defaultSound);
        CHECK(store.LoadFromFile(datPath));

        const ActiveSlot clamped=ClampActiveSlot(store.GetActiveSlot(),3,1);
        CHECK(clamped==ActiveSlot({SlotKind::Shared,2}));
        store.SetActiveSlot(clamped);

        const auto stats=store.GetStats(3,1);
        CHECK(stats.storedSharedCount==2);
        CHECK(stats.hiddenSharedCount==2);
        CHECK(stats.storedSongCount==2);
        CHECK(stats.hiddenSongCount==2);

        // 缩小后再次写盘，不得丢弃高编号槽位！
        CHECK(store.SaveAtomic(datPath));
    }

    // 再次调大配置，所有高编号槽位必须完整读回
    {
        SlotStore store;
        uint8_t defaultSound[CustomizeSet::kSoundBytes]{};
        defaultSound[0]=7;
        store.SetDefaultSoundTemplate(defaultSound);
        CHECK(store.LoadFromFile(datPath));

        const auto stats=store.GetStats(6,4);
        CHECK(stats.storedSharedCount==2);
        CHECK(stats.hiddenSharedCount==0);
        CHECK(stats.storedSongCount==2);
        CHECK(stats.hiddenSongCount==0);

        const auto* mD=store.FindSharedModule(3,0);
        CHECK(mD && mD->moduleId==114 && mD->items[0]==201 && mD->flags==1);
        const auto* mF=store.FindSharedModule(5,1);
        CHECK(mF && mF->moduleId==30 && mF->items[2]==405 && mF->flags==4);
        const auto* s2=store.FindSongModule(637,1,0);
        CHECK(s2 && s2->moduleId==222 && s2->items[4]==99 && s2->flags==16);
        const auto* s4=store.FindSongModule(637,3,0);
        CHECK(s4 && s4->moduleId==333);
        const auto* snd=store.FindSongSound(637,1);
        CHECK(snd && snd[0]==42);

        // 篡改字节测试 CRC32 校验拦截
        auto bytes=store.Serialize();
        bytes.back()^=0xFF;
        SlotStore corrupted;
        CHECK(!corrupted.Deserialize(bytes));
    }

    // 4. settings.ini 往返读写与注释保留测试
    {
        const fs::path iniPath=tempDir/L"settings.ini";
        {
            std::ofstream out(iniPath,std::ios::binary);
            out<<"# comment line\r\nenabled=true\r\nshared_slots=99\r\nsong_slots=0\r\nhud_mode=extended_only\r\n";
        }
        ModSettings loaded=LoadSettings(iniPath);
        CHECK(loaded.enabled==true);
        CHECK(loaded.sharedSlots==kMaxSharedSlots);
        CHECK(loaded.songSlots==kMinSongSlots);
        CHECK(loaded.hudMode==HudMode::ExtendedOnly);

        loaded.enabled=false;
        loaded.sharedSlots=8;
        loaded.songSlots=5;
        loaded.hotkey="Ctrl+Shift+F9";
        CHECK(SaveSettings(iniPath,loaded));

        const ModSettings reloaded=LoadSettings(iniPath);
        CHECK(reloaded.enabled==false);
        CHECK(reloaded.sharedSlots==8);
        CHECK(reloaded.songSlots==5);
        CHECK(reloaded.hotkey=="Ctrl+Shift+F9");
        const auto hk=ParseHotkey(reloaded.hotkey);
        CHECK(hk.valid && hk.ctrl && hk.shift && !hk.alt && hk.vk==VK_F9);
    }

    // 5. 默认 ModuleEquip、每曲默认角色模组（如 Luka=42）以及旧版 -1 .dat 清洗测试
    {
        ModuleEquip defEquip;
        CHECK(defEquip.moduleId==0);
        CHECK(defEquip.flags==0x1Fu);
        for(int32_t item:defEquip.items) CHECK(item==-1);
        CHECK(defEquip.IsDefault());

        SlotStore store;
        auto* untouchedShared=store.GetOrCreateSharedModule(4,0);
        CHECK(untouchedShared && untouchedShared->moduleId==0);
        for(uint32_t p=0;p<CustomizeSet::kPerformerSlots;++p) {
            const auto* m=store.FindSharedModule(4,p);
            CHECK(m && m->moduleId==0);
        }
        CHECK(!store.HasStoredSharedData(4));
        CHECK(store.GetStats(10,5).storedSharedCount==0);

        const SlotStore::DefaultModules pv44Defaults{0,42,0,0,0,0};
        store.SetSongDefaultModules(44,pv44Defaults);
        auto* pv44P1=store.GetOrCreateSongModule(44,1,1);
        CHECK(pv44P1 && pv44P1->moduleId==42);
        CHECK(!store.HasStoredSongData(44,1));
        CHECK(store.GetStats(10,5).storedSongCount==0);

        // 修改副唱（从 Luka 默认 42 改为 Miku 0）必须被判定为“已修改”，且序列化后再读回不丢失
        pv44P1->moduleId=0;
        CHECK(store.HasStoredSongData(44,1));
        const auto bytes=store.Serialize();

        SlotStore reloaded;
        reloaded.SetSongDefaultModules(44,pv44Defaults);
        CHECK(reloaded.Deserialize(bytes));
        const auto* reloadedP1=reloaded.FindSongModule(44,1,1);
        CHECK(reloadedP1 && reloadedP1->moduleId==0);
        CHECK(reloaded.HasStoredSongData(44,1));
        CHECK(reloaded.GetStats(10,5).storedSongCount==1);
        const auto bytes2=reloaded.Serialize();
        CHECK(bytes==bytes2);

        // 旧版 .dat（moduleId = -1）反序列化清洗验证
#pragma pack(push,1)
        struct RawFileHeader {
            char magic[4]{'D','E','M','S'};
            uint32_t version=SlotStore::kFormatVersion;
            uint32_t headerSize=40;
            uint32_t flags=0;
            uint32_t crc32=0;
            uint8_t activeKind=0;
            uint8_t pad[3]{};
            int32_t activeIndex=3;
            uint32_t sharedCount=1;
            uint32_t songCount=1;
            uint32_t setByteSize=sizeof(CustomizeSet);
        };
        struct RawSharedRec { uint32_t slotIndex=4; CustomizeSet set{}; };
        struct RawSongRec { int32_t pvId=809; uint32_t slotIndex=2; CustomizeSet set{}; };
#pragma pack(pop)
        RawSharedRec sh{};
        sh.set.modules[0].moduleId=87;
        for(size_t p=1;p<CustomizeSet::kPerformerSlots;++p) sh.set.modules[p].moduleId=-1;
        RawSongRec sg{};
        for(size_t p=0;p<CustomizeSet::kPerformerSlots;++p) sg.set.modules[p].moduleId=-1;

        std::vector<uint8_t> legacyBytes(sizeof(RawFileHeader)+sizeof(RawSharedRec)+sizeof(RawSongRec));
        std::memcpy(legacyBytes.data()+sizeof(RawFileHeader),&sh,sizeof(sh));
        std::memcpy(legacyBytes.data()+sizeof(RawFileHeader)+sizeof(sh),&sg,sizeof(sg));
        RawFileHeader hdr{};
        hdr.crc32=ComputeCrc32(legacyBytes.data()+sizeof(RawFileHeader),sizeof(sh)+sizeof(sg));
        std::memcpy(legacyBytes.data(),&hdr,sizeof(hdr));

        SlotStore sanitized;
        CHECK(sanitized.Deserialize(legacyBytes));
        for(uint32_t p=0;p<CustomizeSet::kPerformerSlots;++p) {
            const auto* mSh=sanitized.FindSharedModule(4,p);
            CHECK(mSh && mSh->moduleId==(p==0?87:0));
            const auto* mSg=sanitized.FindSongModule(809,2,p);
            CHECK(mSg && mSg->moduleId==0);
        }
    }

    fs::remove_all(tempDir,ec);
    std::cout<<"SlotStoreTests: all assertions passed.\n";
    return 0;
}
