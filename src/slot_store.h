#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>

namespace diva {
namespace fs=std::filesystem;

#pragma pack(push,1)
struct ModuleEquip {
    int32_t moduleId=0;
    int32_t items[5]{-1,-1,-1,-1,-1};
    uint32_t flags=0x1Fu;

    bool IsDefault(int32_t defaultModuleId=0) const {
        const int32_t expected=defaultModuleId>=0?defaultModuleId:0;
        if(moduleId!=expected && moduleId>=0) return false;
        if(flags!=0x1Fu && flags!=0xFFFFFFFFu && flags!=0u) return false;
        for(int32_t item:items) if(item!=-1) return false;
        return true;
    }
    bool operator==(const ModuleEquip&) const=default;
};
static_assert(sizeof(ModuleEquip)==0x1C,"ModuleEquip 必须精确为 0x1C 字节");

struct CustomizeSet {
    static constexpr size_t kPerformerSlots=6;
    static constexpr size_t kSoundBytes=0x188;

    ModuleEquip modules[kPerformerSlots]{};
    uint8_t reserved[8]{};
    uint8_t sound[kSoundBytes]{};

    bool IsDefault(const uint8_t* defaultSound=nullptr,const int32_t* defaultModules=nullptr) const {
        for(size_t i=0;i<kPerformerSlots;++i) {
            if(!modules[i].IsDefault(defaultModules?defaultModules[i]:0)) return false;
        }
        if(!defaultSound) {
            for(uint8_t b:sound) if(b!=0) return false;
            return true;
        }
        return std::memcmp(sound,defaultSound,kSoundBytes)==0;
    }
    static CustomizeSet CreateDefault(const uint8_t* defaultSound=nullptr,const int32_t* defaultModules=nullptr) {
        CustomizeSet set{};
        if(defaultModules) {
            for(size_t i=0;i<kPerformerSlots;++i) {
                set.modules[i].moduleId=defaultModules[i]>=0?defaultModules[i]:0;
            }
        }
        if(defaultSound) std::memcpy(set.sound,defaultSound,kSoundBytes);
        return set;
    }
};
static_assert(sizeof(CustomizeSet)==0x238,"CustomizeSet 必须精确为 0x238 字节");
#pragma pack(pop)

enum class SlotKind : uint8_t { Shared=0, Song=1 };

struct ActiveSlot {
    SlotKind kind=SlotKind::Shared;
    int index=0;

    bool IsExtended() const {
        return kind==SlotKind::Shared?index>=3:index>=1;
    }
    bool operator==(const ActiveSlot&) const=default;
};

struct SongSlotKey {
    int32_t pvId=0;
    uint32_t slotIndex=0;

    bool operator<(const SongSlotKey& other) const {
        if(pvId!=other.pvId) return pvId<other.pvId;
        return slotIndex<other.slotIndex;
    }
    bool operator==(const SongSlotKey&) const=default;
};

// 槽位循环与非破坏式边界裁剪。
ActiveSlot ClampActiveSlot(ActiveSlot current,int sharedSlots,int songSlots);
ActiveSlot CycleSlot(ActiveSlot current,int delta,int sharedSlots,int songSlots);
int NativeScopeFor(ActiveSlot slot);
ActiveSlot FromNativeScope(int scope);

uint32_t ComputeCrc32(const uint8_t* data,size_t size);

class SlotStore {
public:
    static constexpr char kMagic[4]{'D','E','M','S'};
    static constexpr uint32_t kFormatVersion=1;
    static constexpr const wchar_t* kDefaultFileName=L"DivaExtraModuleSets.dat";
    using DefaultModules=std::array<int32_t,CustomizeSet::kPerformerSlots>;

    void Clear();

    ActiveSlot GetActiveSlot() const;
    void SetActiveSlot(ActiveSlot slot);

    void SetDefaultSoundTemplate(const uint8_t* soundBytes);
    static bool IsCorruptedSoundBlock(const uint8_t* soundBytes);
    static bool SanitizeSoundBlock(uint8_t* soundBytes,const uint8_t* defaultTemplate);
    bool SanitizeSoundInPlace(uint8_t* soundBytes) const;
    void SetSongDefaultModules(int32_t pvId,const DefaultModules& defaultModules);
    std::vector<int32_t> ListStoredSongPvIds() const;

    ModuleEquip* GetOrCreateSharedModule(uint32_t slotIndex,uint32_t performer);
    const ModuleEquip* FindSharedModule(uint32_t slotIndex,uint32_t performer) const;
    uint8_t* GetOrCreateSharedSound(uint32_t slotIndex);
    const uint8_t* FindSharedSound(uint32_t slotIndex) const;

    ModuleEquip* GetOrCreateSongModule(int32_t pvId,uint32_t slotIndex,uint32_t performer);
    const ModuleEquip* FindSongModule(int32_t pvId,uint32_t slotIndex,uint32_t performer) const;
    uint8_t* GetOrCreateSongSound(int32_t pvId,uint32_t slotIndex);
    const uint8_t* FindSongSound(int32_t pvId,uint32_t slotIndex) const;

    bool HasStoredSharedData(uint32_t slotIndex) const;
    bool HasStoredSongData(int32_t pvId,uint32_t slotIndex) const;

    struct Stats {
        size_t storedSharedCount=0;
        size_t hiddenSharedCount=0;
        size_t storedSongCount=0;
        size_t hiddenSongCount=0;
    };
    Stats GetStats(int visibleSharedSlots,int visibleSongSlots) const;

    bool IsDirty() const;
    void MarkDirty();

    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& bytes);

    bool SaveAtomic(const fs::path& path);
    bool LoadFromFile(const fs::path& path);

private:
    const int32_t* FindSongDefaultModulesLocked(int32_t pvId) const;
    CustomizeSet& EnsureSharedLocked(uint32_t slotIndex);
    CustomizeSet& EnsureSongLocked(int32_t pvId,uint32_t slotIndex);

    mutable std::mutex mutex_;
    ActiveSlot active_{SlotKind::Shared,0};
    bool hasDefaultSound_=false;
    std::array<uint8_t,CustomizeSet::kSoundBytes> defaultSound_{};
    std::map<int32_t,DefaultModules> songDefaultModules_;
    std::map<uint32_t,CustomizeSet> sharedSets_;
    std::map<SongSlotKey,CustomizeSet> songSets_;
    bool dirty_=false;
};
}
