#pragma once
#include "settings.h"
#include "slot_store.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>

namespace diva {
struct RuntimeSnapshot {
    bool isGameHost=false;
    bool supported=false;
    bool inputReady=false;
    bool enabled=true;
    bool inCustomization=false;
    bool inSongSelect=false;
    bool isPlaylist=false;
    int32_t currentPvId=0;
    int32_t nativeScope=0;
    ActiveSlot activeSlot{};
    int visibleSharedSlots=6;
    int visibleSongSlots=2;
    size_t storedSharedCount=0;
    size_t hiddenSharedCount=0;
    size_t storedSongCount=0;
    size_t hiddenSongCount=0;
    bool saveDirty=false;
    bool hasPendingSaveResult=false;
    bool lastSaveOk=true;
    std::string saveFilePath;
    std::string diagnostic;
    std::string gameLanguage;
};

class NativeBridge {
public:
    std::atomic<bool> captures{false};
    std::function<void(const std::string&)> logCallback;

    void Initialize();
    void Shutdown();

    std::string CurrentLanguage() const;
    void ApplySettings(const ModSettings& settings);

    RuntimeSnapshot ReadSnapshot() const;
    bool HasStoredShared(uint32_t slotIndex) const;
    bool HasStoredSong(int32_t pvId,uint32_t slotIndex) const;

    void RequestSelectSlot(ActiveSlot slot);
    bool TriggerManualSave();

private:
    void Log(const std::string& message) const;
};

NativeBridge& Bridge();

namespace detail {
struct HookTestPointers {
    void* getSlotConfig=nullptr;
    void* getSlotConfigConst=nullptr;
    void* getSoundConfig=nullptr;
    void* cycleScope=nullptr;
    void* applySongOutfit=nullptr;
    void* pvSelSetScopeUi=nullptr;
};
HookTestPointers GetThunkEntryPointsForTest();
void SetTestOriginalsForUnitTests(const HookTestPointers& originals);
void SetTestStateForUnitTests(const ModSettings& settings,ActiveSlot slot);
}
}
