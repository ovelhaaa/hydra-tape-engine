#pragma once

#include "hydra_ui_types.h"
#include "hydra_ui_catalog.h"
#include "hydra_ui_snapshot.h"
#include <cstddef>
#include <cstdint>

namespace hydra::ui {

class UiModel {
public:
    static constexpr size_t kMaxCommandsPerEvent = 4;
    static constexpr size_t kMaxGroupParams = 16;
    static constexpr size_t kListVisibleRows = 5;

    explicit UiModel(const UiCatalog& catalog);

    void reset(bool showSplash = true);
    void dismissSplash();

    // Process an input event against the current snapshot; outputs 0..N commands
    size_t handleEvent(const UiEvent& ev,
                       const UiSnapshot& snap,
                       UiCommand* outCmds,
                       size_t maxCmds);

    // Notify UI that a preset was loaded (unlatches pot pickup for soft takeover)
    void onPresetLoaded(const UiSnapshot& snap);

    // State accessors for renderer & tests
    UiScreen screen() const { return screen_; }
    HomeFocus homeFocus() const { return homeFocus_; }
    bool isHomeEditing() const { return homeEditing_; }
    uint8_t menuCursor() const { return menuCursor_; }
    UiGroupId currentGroup() const { return static_cast<UiGroupId>(menuCursor_); }

    size_t currentGroupParamCount() const { return currentGroupCount_; }
    UiParamId currentGroupParamAt(size_t idx) const;
    size_t listCursor() const { return listCursors_[menuCursor_]; }
    size_t listScroll() const { return listScrolls_[menuCursor_]; }

    UiParamId editParamId() const { return editParamId_; }
    float editPreviewValue(const UiSnapshot& snap) const;

    size_t presetCursor() const { return presetCursor_; }
    bool isPresetConfirmPending() const { return presetConfirmPending_; }

    // Ostensive status banner (e.g. SW Bypass toggled, Preset loaded, etc.)
    const char* statusBanner() const { return statusBanner_; }
    bool hasStatusBanner(uint64_t nowUs) const;
    void clearStatusBanner() { statusBanner_[0] = '\0'; }

    // Derive active semantic mode label from snapshot
    static SemanticModeId deriveActiveMode(const UiSnapshot& snap);

    // Potentiometer pickup state access (future hardware)
    const PotPickupState& potPickup(size_t idx) const { return potPickups_[idx]; }

private:
    const UiCatalog& catalog_;

    UiScreen screen_ = UiScreen::SPLASH;
    HomeFocus homeFocus_ = HomeFocus::TIME;
    bool homeEditing_ = false;

    uint8_t menuCursor_ = 0;
    size_t listCursors_[static_cast<size_t>(UiGroupId::COUNT)]{};
    size_t listScrolls_[static_cast<size_t>(UiGroupId::COUNT)]{};

    UiParamId currentGroupIds_[kMaxGroupParams]{};
    size_t currentGroupCount_ = 0;

    UiParamId editParamId_ = UiParamId::DELAY_TIME_MS;

    size_t presetCursor_ = 0;
    bool presetConfirmPending_ = false;

    char statusBanner_[32]{};
    uint64_t statusBannerUntilUs_ = 0;

    // Soft-takeover state for 3 future pots (0=Time, 1=Feedback, 2=Mix)
    PotPickupState potPickups_[3]{};

    void refreshGroupCache();
    void ensureListScrollVisible();
    void setStatusBanner(const char* text, uint64_t nowUs, uint64_t durationUs = 1500000ULL);
    UiParamId homeFocusToParam(HomeFocus f) const;

    size_t handleHome(const UiEvent& ev, const UiSnapshot& snap, UiCommand* out, size_t maxCmds);
    size_t handleMenu(const UiEvent& ev, UiCommand* out, size_t maxCmds);
    size_t handleParamList(const UiEvent& ev, const UiSnapshot& snap, UiCommand* out, size_t maxCmds);
    size_t handleParamEdit(const UiEvent& ev, const UiSnapshot& snap, UiCommand* out, size_t maxCmds);
    size_t handleTapTempo(const UiEvent& ev, UiCommand* out, size_t maxCmds);
    size_t handlePresets(const UiEvent& ev, const UiSnapshot& snap, UiCommand* out, size_t maxCmds);
    size_t handlePotChanged(const UiEvent& ev, const UiSnapshot& snap, UiCommand* out, size_t maxCmds);
    size_t handleFootswitch(const UiEvent& ev, const UiSnapshot& snap, UiCommand* out, size_t maxCmds);
};

} // namespace hydra::ui
