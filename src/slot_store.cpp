#include "slot_store.h"
#include "settings.h"
#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <iterator>

namespace diva {
namespace {
#pragma pack(push,1)
struct FileHeader {
    char magic[4]{'D','E','M','S'};
    uint32_t version=SlotStore::kFormatVersion;
    uint32_t headerSize=sizeof(FileHeader);
    uint32_t flags=0;
    uint32_t crc32=0;
    uint8_t activeKind=0;
    uint8_t pad[3]{};
    int32_t activeIndex=0;
    uint32_t sharedCount=0;
    uint32_t songCount=0;
    uint32_t setByteSize=sizeof(CustomizeSet);
};
static_assert(sizeof(FileHeader)==40,"FileHeader 大小需对齐");

struct SharedRecordHeader {
    uint32_t slotIndex=0;
};
struct SongRecordHeader {
    int32_t pvId=0;
    uint32_t slotIndex=0;
};
#pragma pack(pop)
}

uint32_t ComputeCrc32(const uint8_t* data,size_t size) {
    uint32_t crc=0xFFFFFFFFu;
    for(size_t i=0;i<size;++i) {
        crc^=data[i];
        for(int bit=0;bit<8;++bit) {
            const uint32_t mask=static_cast<uint32_t>(-static_cast<int32_t>(crc&1u));
            crc=(crc>>1)^(0xEDB88320u&mask);
        }
    }
    return ~crc;
}

ActiveSlot ClampActiveSlot(ActiveSlot current,int sharedSlots,int songSlots) {
    sharedSlots=std::clamp(sharedSlots,kMinSharedSlots,kMaxSharedSlots);
    songSlots=std::clamp(songSlots,kMinSongSlots,kMaxSongSlots);
    if(current.kind==SlotKind::Shared) {
        current.index=std::clamp(current.index,0,sharedSlots-1);
    } else {
        current.index=std::clamp(current.index,0,songSlots-1);
    }
    return current;
}

ActiveSlot CycleSlot(ActiveSlot current,int delta,int sharedSlots,int songSlots) {
    sharedSlots=std::clamp(sharedSlots,kMinSharedSlots,kMaxSharedSlots);
    songSlots=std::clamp(songSlots,kMinSongSlots,kMaxSongSlots);
    const int total=sharedSlots+songSlots;
    if(total<=0) return {SlotKind::Shared,0};
    current=ClampActiveSlot(current,sharedSlots,songSlots);
    int linear=current.kind==SlotKind::Shared?current.index:(sharedSlots+current.index);
    const int step=delta>=0?1:-1;
    linear=(linear+step+total)%total;
    if(linear<sharedSlots) return {SlotKind::Shared,linear};
    return {SlotKind::Song,linear-sharedSlots};
}

int NativeScopeFor(ActiveSlot slot) {
    if(slot.kind==SlotKind::Song) return 3;
    return std::clamp(slot.index,0,2);
}

ActiveSlot FromNativeScope(int scope) {
    if(scope==3) return {SlotKind::Song,0};
    if(scope>=0 && scope<=2) return {SlotKind::Shared,scope};
    return {SlotKind::Shared,0};
}

void SlotStore::Clear() {
    std::lock_guard lock(mutex_);
    sharedSets_.clear();
    songSets_.clear();
    songDefaultModules_.clear();
    active_={SlotKind::Shared,0};
    dirty_=false;
}

ActiveSlot SlotStore::GetActiveSlot() const {
    std::lock_guard lock(mutex_);
    return active_;
}

void SlotStore::SetActiveSlot(ActiveSlot slot) {
    std::lock_guard lock(mutex_);
    if(active_!=slot) {
        active_=slot;
        dirty_=true;
    }
}

void SlotStore::SetDefaultSoundTemplate(const uint8_t* soundBytes) {
    if(!soundBytes) return;
    std::lock_guard lock(mutex_);
    std::memcpy(defaultSound_.data(),soundBytes,CustomizeSet::kSoundBytes);
    hasDefaultSound_=true;
}

void SlotStore::SetSongDefaultModules(int32_t pvId,const DefaultModules& defaultModules) {
    if(pvId<=0) return;
    DefaultModules sanitized=defaultModules;
    for(int32_t& id:sanitized) {
        if(id<0) id=0;
    }
    std::lock_guard lock(mutex_);
    songDefaultModules_[pvId]=sanitized;
    // If an untouched legacy slot had -1/0 before song defaults were resolved, keep it consistent?
    // Actually only untouched defaults matter when serialized or created; existing stored entries retain user choices.
}

std::vector<int32_t> SlotStore::ListStoredSongPvIds() const {
    std::lock_guard lock(mutex_);
    std::vector<int32_t> pvIds;
    int32_t lastPv=-1;
    for(const auto& [key,_]:songSets_) {
        if(key.pvId>0 && key.pvId!=lastPv) {
            pvIds.push_back(key.pvId);
            lastPv=key.pvId;
        }
    }
    return pvIds;
}

const int32_t* SlotStore::FindSongDefaultModulesLocked(int32_t pvId) const {
    const auto it=songDefaultModules_.find(pvId);
    return it!=songDefaultModules_.end()?it->second.data():nullptr;
}

CustomizeSet& SlotStore::EnsureSharedLocked(uint32_t slotIndex) {
    auto it=sharedSets_.find(slotIndex);
    if(it!=sharedSets_.end()) return it->second;
    const auto [inserted,_]=sharedSets_.emplace(slotIndex,CustomizeSet::CreateDefault(hasDefaultSound_?defaultSound_.data():nullptr));
    return inserted->second;
}

CustomizeSet& SlotStore::EnsureSongLocked(int32_t pvId,uint32_t slotIndex) {
    const SongSlotKey key{pvId,slotIndex};
    auto it=songSets_.find(key);
    if(it!=songSets_.end()) return it->second;
    const auto [inserted,_]=songSets_.emplace(
        key,
        CustomizeSet::CreateDefault(hasDefaultSound_?defaultSound_.data():nullptr,FindSongDefaultModulesLocked(pvId)));
    return inserted->second;
}

ModuleEquip* SlotStore::GetOrCreateSharedModule(uint32_t slotIndex,uint32_t performer) {
    if(performer>=CustomizeSet::kPerformerSlots) return nullptr;
    std::lock_guard lock(mutex_);
    dirty_=true;
    return &EnsureSharedLocked(slotIndex).modules[performer];
}

const ModuleEquip* SlotStore::FindSharedModule(uint32_t slotIndex,uint32_t performer) const {
    if(performer>=CustomizeSet::kPerformerSlots) return nullptr;
    std::lock_guard lock(mutex_);
    auto it=sharedSets_.find(slotIndex);
    if(it==sharedSets_.end()) return nullptr;
    return &it->second.modules[performer];
}

uint8_t* SlotStore::GetOrCreateSharedSound(uint32_t slotIndex) {
    std::lock_guard lock(mutex_);
    dirty_=true;
    return EnsureSharedLocked(slotIndex).sound;
}

const uint8_t* SlotStore::FindSharedSound(uint32_t slotIndex) const {
    std::lock_guard lock(mutex_);
    auto it=sharedSets_.find(slotIndex);
    if(it==sharedSets_.end()) return nullptr;
    return it->second.sound;
}

ModuleEquip* SlotStore::GetOrCreateSongModule(int32_t pvId,uint32_t slotIndex,uint32_t performer) {
    if(pvId<=0 || performer>=CustomizeSet::kPerformerSlots) return nullptr;
    std::lock_guard lock(mutex_);
    dirty_=true;
    return &EnsureSongLocked(pvId,slotIndex).modules[performer];
}

const ModuleEquip* SlotStore::FindSongModule(int32_t pvId,uint32_t slotIndex,uint32_t performer) const {
    if(pvId<=0 || performer>=CustomizeSet::kPerformerSlots) return nullptr;
    std::lock_guard lock(mutex_);
    auto it=songSets_.find(SongSlotKey{pvId,slotIndex});
    if(it==songSets_.end()) return nullptr;
    return &it->second.modules[performer];
}

uint8_t* SlotStore::GetOrCreateSongSound(int32_t pvId,uint32_t slotIndex) {
    if(pvId<=0) return nullptr;
    std::lock_guard lock(mutex_);
    dirty_=true;
    return EnsureSongLocked(pvId,slotIndex).sound;
}

const uint8_t* SlotStore::FindSongSound(int32_t pvId,uint32_t slotIndex) const {
    if(pvId<=0) return nullptr;
    std::lock_guard lock(mutex_);
    auto it=songSets_.find(SongSlotKey{pvId,slotIndex});
    if(it==songSets_.end()) return nullptr;
    return it->second.sound;
}

bool SlotStore::HasStoredSharedData(uint32_t slotIndex) const {
    std::lock_guard lock(mutex_);
    auto it=sharedSets_.find(slotIndex);
    if(it==sharedSets_.end()) return false;
    return !it->second.IsDefault(hasDefaultSound_?defaultSound_.data():nullptr);
}

bool SlotStore::HasStoredSongData(int32_t pvId,uint32_t slotIndex) const {
    if(pvId<=0) return false;
    std::lock_guard lock(mutex_);
    auto it=songSets_.find(SongSlotKey{pvId,slotIndex});
    if(it==songSets_.end()) return false;
    return !it->second.IsDefault(hasDefaultSound_?defaultSound_.data():nullptr,FindSongDefaultModulesLocked(pvId));
}

SlotStore::Stats SlotStore::GetStats(int visibleSharedSlots,int visibleSongSlots) const {
    std::lock_guard lock(mutex_);
    Stats stats{};
    const uint8_t* soundTpl=hasDefaultSound_?defaultSound_.data():nullptr;
    for(const auto& [slotIdx,set]:sharedSets_) {
        if(slotIdx<3 || set.IsDefault(soundTpl)) continue;
        ++stats.storedSharedCount;
        if(static_cast<int>(slotIdx)>=visibleSharedSlots) ++stats.hiddenSharedCount;
    }
    for(const auto& [key,set]:songSets_) {
        if(key.slotIndex<1 || set.IsDefault(soundTpl,FindSongDefaultModulesLocked(key.pvId))) continue;
        ++stats.storedSongCount;
        if(static_cast<int>(key.slotIndex)>=visibleSongSlots) ++stats.hiddenSongCount;
    }
    return stats;
}

bool SlotStore::IsDirty() const {
    std::lock_guard lock(mutex_);
    return dirty_;
}

void SlotStore::MarkDirty() {
    std::lock_guard lock(mutex_);
    dirty_=true;
}

std::vector<uint8_t> SlotStore::Serialize() const {
    std::lock_guard lock(mutex_);
    const uint8_t* soundTpl=hasDefaultSound_?defaultSound_.data():nullptr;
    std::vector<std::pair<uint32_t,const CustomizeSet*>> nonEmptyShared;
    for(const auto& [slotIdx,set]:sharedSets_) {
        if(slotIdx>=3 && !set.IsDefault(soundTpl)) nonEmptyShared.emplace_back(slotIdx,&set);
    }
    std::vector<std::pair<SongSlotKey,const CustomizeSet*>> nonEmptySong;
    for(const auto& [key,set]:songSets_) {
        if(key.slotIndex>=1 && !set.IsDefault(soundTpl,FindSongDefaultModulesLocked(key.pvId))) nonEmptySong.emplace_back(key,&set);
    }

    FileHeader header{};
    header.activeKind=static_cast<uint8_t>(active_.kind);
    header.activeIndex=active_.index;
    header.sharedCount=static_cast<uint32_t>(nonEmptyShared.size());
    header.songCount=static_cast<uint32_t>(nonEmptySong.size());

    const size_t payloadSize=
        nonEmptyShared.size()*(sizeof(SharedRecordHeader)+sizeof(CustomizeSet))+
        nonEmptySong.size()*(sizeof(SongRecordHeader)+sizeof(CustomizeSet));
    std::vector<uint8_t> buffer(sizeof(FileHeader)+payloadSize);
    size_t cursor=sizeof(FileHeader);
    for(const auto& [slotIdx,set]:nonEmptyShared) {
        SharedRecordHeader rh{slotIdx};
        std::memcpy(buffer.data()+cursor,&rh,sizeof(rh));
        cursor+=sizeof(rh);
        std::memcpy(buffer.data()+cursor,set,sizeof(CustomizeSet));
        cursor+=sizeof(CustomizeSet);
    }
    for(const auto& [key,set]:nonEmptySong) {
        SongRecordHeader rh{key.pvId,key.slotIndex};
        std::memcpy(buffer.data()+cursor,&rh,sizeof(rh));
        cursor+=sizeof(rh);
        std::memcpy(buffer.data()+cursor,set,sizeof(CustomizeSet));
        cursor+=sizeof(CustomizeSet);
    }
    header.crc32=payloadSize?ComputeCrc32(buffer.data()+sizeof(FileHeader),payloadSize):0;
    std::memcpy(buffer.data(),&header,sizeof(header));
    return buffer;
}

bool SlotStore::Deserialize(const std::vector<uint8_t>& bytes) {
    if(bytes.size()<sizeof(FileHeader)) return false;
    FileHeader header{};
    std::memcpy(&header,bytes.data(),sizeof(header));
    if(std::memcmp(header.magic,kMagic,4)!=0) return false;
    if(header.version==0 || header.headerSize<sizeof(FileHeader) || header.headerSize>bytes.size()) return false;
    if(header.setByteSize==0 || header.setByteSize>4096) return false;

    const size_t payloadOffset=header.headerSize;
    const size_t payloadSize=bytes.size()-payloadOffset;
    const uint32_t expectedCrc=payloadSize?ComputeCrc32(bytes.data()+payloadOffset,payloadSize):0;
    if(expectedCrc!=header.crc32) return false;

    const size_t sharedStride=sizeof(SharedRecordHeader)+header.setByteSize;
    const size_t songStride=sizeof(SongRecordHeader)+header.setByteSize;
    const uint64_t requiredBytes=
        static_cast<uint64_t>(header.sharedCount)*sharedStride+
        static_cast<uint64_t>(header.songCount)*songStride;
    if(requiredBytes>payloadSize) return false;

    std::map<uint32_t,CustomizeSet> loadedShared;
    std::map<SongSlotKey,CustomizeSet> loadedSong;
    size_t cursor=payloadOffset;
    const size_t copyBytes=std::min<size_t>(header.setByteSize,sizeof(CustomizeSet));
    const auto sanitizeLegacyModules=[](CustomizeSet& set) {
        for(auto& m:set.modules) {
            if(m.moduleId<0) m.moduleId=0;
        }
    };
    for(uint32_t i=0;i<header.sharedCount;++i) {
        SharedRecordHeader rh{};
        std::memcpy(&rh,bytes.data()+cursor,sizeof(rh));
        cursor+=sizeof(rh);
        CustomizeSet set=CustomizeSet::CreateDefault(hasDefaultSound_?defaultSound_.data():nullptr);
        std::memcpy(&set,bytes.data()+cursor,copyBytes);
        sanitizeLegacyModules(set);
        cursor+=header.setByteSize;
        if(rh.slotIndex>=3 && rh.slotIndex<kMaxSharedSlots) loadedShared.emplace(rh.slotIndex,set);
    }
    for(uint32_t i=0;i<header.songCount;++i) {
        SongRecordHeader rh{};
        std::memcpy(&rh,bytes.data()+cursor,sizeof(rh));
        cursor+=sizeof(rh);
        CustomizeSet set=CustomizeSet::CreateDefault(hasDefaultSound_?defaultSound_.data():nullptr);
        std::memcpy(&set,bytes.data()+cursor,copyBytes);
        sanitizeLegacyModules(set);
        cursor+=header.setByteSize;
        if(rh.pvId>0 && rh.slotIndex>=1 && rh.slotIndex<kMaxSongSlots) {
            loadedSong.emplace(SongSlotKey{rh.pvId,rh.slotIndex},set);
        }
    }

    std::lock_guard lock(mutex_);
    sharedSets_=std::move(loadedShared);
    songSets_=std::move(loadedSong);
    active_.kind=header.activeKind==static_cast<uint8_t>(SlotKind::Song)?SlotKind::Song:SlotKind::Shared;
    active_.index=std::clamp(header.activeIndex,0,active_.kind==SlotKind::Shared?kMaxSharedSlots-1:kMaxSongSlots-1);
    dirty_=false;
    return true;
}

bool SlotStore::SaveAtomic(const fs::path& path) {
    if(path.empty()) return false;
    const auto bytes=Serialize();
    std::error_code ec;
    if(!path.parent_path().empty()) fs::create_directories(path.parent_path(),ec);
    const fs::path tmp=fs::path(path).concat(L".tmp");
    {
        std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
        if(!out.is_open()) return false;
        if(!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if(!out.good()) return false;
    }
    if(MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)==0) return false;
    std::lock_guard lock(mutex_);
    dirty_=false;
    return true;
}

bool SlotStore::LoadFromFile(const fs::path& path) {
    if(path.empty()) return false;
    std::error_code ec;
    if(!fs::exists(path,ec)) return false;
    std::ifstream in(path,std::ios::binary);
    if(!in.is_open()) return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    return Deserialize(bytes);
}
}
