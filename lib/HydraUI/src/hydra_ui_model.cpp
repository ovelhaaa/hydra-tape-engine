#include "hydra_ui_model.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hydra::ui {

namespace {

int wrapIndex(int idx, int count) {
    if (count <= 0) return 0;
    int m = idx % count;
    return (m < 0) ? (m + count) : m;
}

float stepQuantize(float val, const UiParamDesc& desc) {
    if (desc.kind == ParamValueKind::BOOL) {
        return (val > 0.5f) ? 1.0f : 0.0f;
    }
    if (desc.kind == ParamValueKind::ENUM ||
        desc.kind == ParamValueKind::INT ||
        desc.kind == ParamValueKind::HEADS_MASK) {
        float rounded = std::round(val);
        return std::max(desc.minVal, std::min(desc.maxVal, rounded));
    }
    return std::max(desc.minVal, std::min(desc.maxVal, val));
}

} // namespace

UiModel::UiModel(const UiCatalog& catalog) : catalog_(catalog) {
    reset(true);
}

void UiModel::reset(bool showSplash) {
    screen_ = showSplash ? UiScreen::SPLASH : UiScreen::HOME;
    homeFocus_ = HomeFocus::TIME;
    homeEditing_ = false;
    menuCursor_ = 0;
    for (size_t i = 0; i < static_cast<size_t>(UiGroupId::COUNT); ++i) {
        listCursors_[i] = 0;
        listScrolls_[i] = 0;
    }
    editParamId_ = UiParamId::DELAY_TIME_MS;
    presetCursor_ = 0;
    presetConfirmPending_ = false;
    statusBanner_[0] = '\0';
    statusBannerUntilUs_ = 0;
    refreshGroupCache();
}

void UiModel::dismissSplash() {
    if (screen_ == UiScreen::SPLASH) {
        screen_ = UiScreen::HOME;
    }
}

void UiModel::refreshGroupCache() {
    currentGroupCount_ = catalog_.getGroupParams(
        static_cast<UiGroupId>(menuCursor_), currentGroupIds_, kMaxGroupParams);
    if (currentGroupCount_ == 0) {
        listCursors_[menuCursor_] = 0;
        listScrolls_[menuCursor_] = 0;
    } else if (listCursors_[menuCursor_] >= currentGroupCount_) {
        listCursors_[menuCursor_] = currentGroupCount_ - 1;
    }
    ensureListScrollVisible();
}

void UiModel::ensureListScrollVisible() {
    size_t cursor = listCursors_[menuCursor_];
    size_t& scroll = listScrolls_[menuCursor_];
    if (cursor < scroll) {
        scroll = cursor;
    } else if (cursor >= scroll + kListVisibleRows) {
        scroll = cursor - kListVisibleRows + 1;
    }
}

UiParamId UiModel::currentGroupParamAt(size_t idx) const {
    if (idx >= currentGroupCount_) return UiParamId::DELAY_TIME_MS;
    return currentGroupIds_[idx];
}

void UiModel::setStatusBanner(const char* text, uint64_t nowUs, uint64_t durationUs) {
    if (!text) {
        statusBanner_[0] = '\0';
        return;
    }
    std::snprintf(statusBanner_, sizeof(statusBanner_), "%s", text);
    statusBannerUntilUs_ = nowUs + durationUs;
}

bool UiModel::hasStatusBanner(uint64_t nowUs) const {
    if (statusBanner_[0] == '\0') return false;
    if (statusBannerUntilUs_ == 0) return true;
    return nowUs < statusBannerUntilUs_;
}

UiParamId UiModel::homeFocusToParam(HomeFocus f) const {
    switch (f) {
        case HomeFocus::TIME: return UiParamId::DELAY_TIME_MS;
        case HomeFocus::FB:   return UiParamId::FEEDBACK;
        case HomeFocus::MIX:  return UiParamId::DRY_WET;
        default:              return UiParamId::DELAY_TIME_MS;
    }
}

float UiModel::editPreviewValue(const UiSnapshot& snap) const {
    return snap.values[static_cast<size_t>(editParamId_)];
}

SemanticModeId UiModel::deriveActiveMode(const UiSnapshot& snap) {
    bool dly = snap.values[static_cast<size_t>(UiParamId::DELAY_ACTIVE)] > 0.5f;
    bool rev = snap.values[static_cast<size_t>(UiParamId::REVERSE_ACTIVE)] > 0.5f;
    bool wrp = snap.values[static_cast<size_t>(UiParamId::WARP_ACTIVE)] > 0.5f;

    if (rev && wrp) return SemanticModeId::REVERSE_WARP;
    if (wrp)        return SemanticModeId::WARP;
    if (rev)        return SemanticModeId::REVERSE;
    if (dly)        return SemanticModeId::TAPE_DELAY;
    return SemanticModeId::TAPE_SATURATOR;
}

void UiModel::onPresetLoaded(const UiSnapshot& snap) {
    const UiParamId potParams[3] = {
        UiParamId::DELAY_TIME_MS,
        UiParamId::FEEDBACK,
        UiParamId::DRY_WET
    };
    for (size_t i = 0; i < 3; ++i) {
        const UiParamDesc& d = catalog_.get(potParams[i]);
        float span = std::max(1e-5f, d.maxVal - d.minVal);
        potPickups_[i].targetNorm = (snap.values[static_cast<size_t>(potParams[i])] - d.minVal) / span;
        potPickups_[i].latched = false;
    }
}

size_t UiModel::handleEvent(const UiEvent& ev,
                            const UiSnapshot& snap,
                            UiCommand* outCmds,
                            size_t maxCmds) {
    if (ev.type == UiEventType::NONE) return 0;

    // Global future peripheral events
    if (ev.type == UiEventType::POT_CHANGED) {
        return handlePotChanged(ev, snap, outCmds, maxCmds);
    }
    if (ev.type == UiEventType::FOOTSWITCH) {
        return handleFootswitch(ev, snap, outCmds, maxCmds);
    }
    if (ev.type == UiEventType::PARAM_CHANGED) {
        return 0;
    }

    if (screen_ == UiScreen::SPLASH) {
        if (ev.type == UiEventType::CLICK || ev.type == UiEventType::LONG_PRESS || ev.type == UiEventType::ROTATE) {
            screen_ = UiScreen::HOME;
        }
        return 0;
    }

    switch (screen_) {
        case UiScreen::HOME:       return handleHome(ev, snap, outCmds, maxCmds);
        case UiScreen::MENU:       return handleMenu(ev, outCmds, maxCmds);
        case UiScreen::PARAM_LIST: return handleParamList(ev, snap, outCmds, maxCmds);
        case UiScreen::PARAM_EDIT: return handleParamEdit(ev, snap, outCmds, maxCmds);
        case UiScreen::TAP_TEMPO:  return handleTapTempo(ev, outCmds, maxCmds);
        case UiScreen::PRESETS:    return handlePresets(ev, snap, outCmds, maxCmds);
        default:                   return 0;
    }
}

size_t UiModel::handleHome(const UiEvent& ev,
                           const UiSnapshot& snap,
                           UiCommand* out,
                           size_t maxCmds) {
    if (homeEditing_) {
        UiParamId pid = homeFocusToParam(homeFocus_);
        const UiParamDesc& desc = catalog_.get(pid);
        if (ev.type == UiEventType::ROTATE && ev.delta != 0) {
            float cur = snap.values[static_cast<size_t>(pid)];
            float next = stepQuantize(cur + desc.step * static_cast<float>(ev.delta), desc);
            if (out && maxCmds > 0) {
                out[0] = UiCommand{};
                out[0].type = UiCommandType::SET_PARAM;
                out[0].paramId = pid;
                out[0].value = next;
                return 1;
            }
            return 0;
        }
        if (ev.type == UiEventType::CLICK || ev.type == UiEventType::LONG_PRESS) {
            homeEditing_ = false;
            return 0;
        }
        return 0;
    }

    // Navigating Home focus
    if (ev.type == UiEventType::ROTATE && ev.delta != 0) {
        int next = wrapIndex(static_cast<int>(homeFocus_) + ev.delta,
                             static_cast<int>(HomeFocus::COUNT));
        homeFocus_ = static_cast<HomeFocus>(next);
        return 0;
    }

    if (ev.type == UiEventType::CLICK) {
        if (homeFocus_ == HomeFocus::MENU) {
            screen_ = UiScreen::MENU;
            return 0;
        }
        homeEditing_ = true;
        return 0;
    }

    if (ev.type == UiEventType::LONG_PRESS) {
        bool nextBypass = !snap.bypassed;
        setStatusBanner(nextBypass ? "SW BYPASS ON" : "EFFECT ACTIVE", ev.timestampUs);
        if (out && maxCmds > 0) {
            out[0] = UiCommand{};
            out[0].type = UiCommandType::TOGGLE_BYPASS;
            out[0].value = nextBypass ? 1.0f : 0.0f;
            return 1;
        }
        return 0;
    }

    return 0;
}

size_t UiModel::handleMenu(const UiEvent& ev, UiCommand* out, size_t maxCmds) {
    (void)out;
    (void)maxCmds;
    constexpr int kGroupCount = static_cast<int>(UiGroupId::COUNT);

    if (ev.type == UiEventType::ROTATE && ev.delta != 0) {
        menuCursor_ = static_cast<uint8_t>(wrapIndex(static_cast<int>(menuCursor_) + ev.delta, kGroupCount));
        return 0;
    }

    if (ev.type == UiEventType::CLICK) {
        UiGroupId grp = static_cast<UiGroupId>(menuCursor_);
        if (grp == UiGroupId::PRESETS) {
            presetConfirmPending_ = false;
            screen_ = UiScreen::PRESETS;
        } else {
            refreshGroupCache();
            screen_ = UiScreen::PARAM_LIST;
        }
        return 0;
    }

    if (ev.type == UiEventType::LONG_PRESS) {
        screen_ = UiScreen::HOME;
        homeEditing_ = false;
        return 0;
    }

    return 0;
}

size_t UiModel::handleParamList(const UiEvent& ev,
                                const UiSnapshot& snap,
                                UiCommand* out,
                                size_t maxCmds) {
    if (ev.type == UiEventType::LONG_PRESS) {
        screen_ = UiScreen::MENU;
        return 0;
    }

    if (currentGroupCount_ == 0) {
        return 0;
    }

    if (ev.type == UiEventType::ROTATE && ev.delta != 0) {
        size_t& cur = listCursors_[menuCursor_];
        cur = static_cast<size_t>(wrapIndex(static_cast<int>(cur) + ev.delta,
                                            static_cast<int>(currentGroupCount_)));
        ensureListScrollVisible();
        return 0;
    }

    if (ev.type == UiEventType::CLICK) {
        UiParamId pid = currentGroupIds_[listCursors_[menuCursor_]];
        const UiParamDesc& desc = catalog_.get(pid);

        if (!desc.available || desc.kind == ParamValueKind::UNAVAILABLE) {
            setStatusBanner("INDISPONIVEL", ev.timestampUs);
            return 0;
        }

        if (pid == UiParamId::ACTION_TAP_TEMPO) {
            screen_ = UiScreen::TAP_TEMPO;
            return 0;
        }

        if (pid == UiParamId::ACTION_RESET_DSP) {
            setStatusBanner("BUFFER RESET", ev.timestampUs);
            if (out && maxCmds > 0) {
                out[0] = UiCommand{};
                out[0].type = UiCommandType::RESET_DSP;
                return 1;
            }
            return 0;
        }

        // Semantic modes in Modo group
        if (pid >= UiParamId::MODE_TAPE_DELAY && pid <= UiParamId::MODE_REVERSE_WARP) {
            SemanticModeId mode = static_cast<SemanticModeId>(
                static_cast<uint16_t>(pid) - static_cast<uint16_t>(UiParamId::MODE_TAPE_DELAY));
            setStatusBanner(UiCatalog::semanticModeName(mode), ev.timestampUs);
            if (out && maxCmds > 0) {
                out[0] = UiCommand{};
                out[0].type = UiCommandType::APPLY_MODE;
                out[0].modeId = mode;
                return 1;
            }
            return 0;
        }

        // Enter Parameter Editor (for BOOL, ENUM, HEADS_MASK, FLOAT, MS, HZ, BPM, INT)
        (void)snap;
        editParamId_ = pid;
        screen_ = UiScreen::PARAM_EDIT;
        return 0;
    }

    return 0;
}

size_t UiModel::handleParamEdit(const UiEvent& ev,
                                const UiSnapshot& snap,
                                UiCommand* out,
                                size_t maxCmds) {
    if (ev.type == UiEventType::CLICK || ev.type == UiEventType::LONG_PRESS) {
        screen_ = UiScreen::PARAM_LIST;
        return 0;
    }

    if (ev.type == UiEventType::ROTATE && ev.delta != 0) {
        const UiParamDesc& desc = catalog_.get(editParamId_);
        if (!desc.available) return 0;

        int d = ev.delta;
        // Never apply acceleration to booleans, enums, or head masks
        if (!desc.allowAccel) {
            d = (d > 0) ? 1 : -1;
        }

        float cur = snap.values[static_cast<size_t>(editParamId_)];
        float next = stepQuantize(cur + desc.step * static_cast<float>(d), desc);
        if (out && maxCmds > 0) {
            out[0] = UiCommand{};
            out[0].type = UiCommandType::SET_PARAM;
            out[0].paramId = editParamId_;
            out[0].value = next;
            return 1;
        }
        return 0;
    }

    return 0;
}

size_t UiModel::handleTapTempo(const UiEvent& ev, UiCommand* out, size_t maxCmds) {
    if (ev.type == UiEventType::LONG_PRESS) {
        screen_ = UiScreen::PARAM_LIST;
        return 0;
    }

    if (ev.type == UiEventType::CLICK) {
        if (out && maxCmds > 0) {
            out[0] = UiCommand{};
            out[0].type = UiCommandType::TAP_TEMPO;
            out[0].timestampUs = ev.timestampUs;
            return 1;
        }
        return 0;
    }

    return 0;
}

size_t UiModel::handlePresets(const UiEvent& ev,
                              const UiSnapshot& snap,
                              UiCommand* out,
                              size_t maxCmds) {
    (void)snap;
    const int count = static_cast<int>(UiCatalog::presetCount());

    if (ev.type == UiEventType::ROTATE && ev.delta != 0) {
        presetConfirmPending_ = false;
        presetCursor_ = static_cast<size_t>(wrapIndex(static_cast<int>(presetCursor_) + ev.delta, count));
        return 0;
    }

    if (ev.type == UiEventType::CLICK) {
        if (!presetConfirmPending_) {
            presetConfirmPending_ = true;
            return 0;
        }
        presetConfirmPending_ = false;
        setStatusBanner("PRESET CARREGADO", ev.timestampUs);
        screen_ = UiScreen::HOME;
        homeEditing_ = false;
        if (out && maxCmds > 0) {
            out[0] = UiCommand{};
            out[0].type = UiCommandType::LOAD_PRESET;
            out[0].presetIndex = static_cast<uint8_t>(presetCursor_);
            return 1;
        }
        return 0;
    }

    if (ev.type == UiEventType::LONG_PRESS) {
        if (presetConfirmPending_) {
            presetConfirmPending_ = false;
            return 0;
        }
        screen_ = UiScreen::MENU;
        return 0;
    }

    return 0;
}

size_t UiModel::handlePotChanged(const UiEvent& ev,
                                 const UiSnapshot& snap,
                                 UiCommand* out,
                                 size_t maxCmds) {
    if (ev.controlId >= 3) return 0;
    const UiParamId potParams[3] = {
        UiParamId::DELAY_TIME_MS,
        UiParamId::FEEDBACK,
        UiParamId::DRY_WET
    };
    UiParamId pid = potParams[ev.controlId];
    const UiParamDesc& desc = catalog_.get(pid);
    PotPickupState& st = potPickups_[ev.controlId];

    float norm = std::max(0.0f, std::min(1.0f, ev.normValue));
    if (!st.initialized) {
        st.initialized = true;
        st.lastRawNorm = norm;
    }

    if (!st.latched) {
        constexpr float kPickupDeadband = 0.03f;
        bool crossed = ((st.lastRawNorm <= st.targetNorm && norm >= st.targetNorm) ||
                        (st.lastRawNorm >= st.targetNorm && norm <= st.targetNorm) ||
                        std::fabs(norm - st.targetNorm) <= kPickupDeadband);
        st.lastRawNorm = norm;
        if (!crossed) {
            return 0;
        }
        st.latched = true;
    } else {
        st.lastRawNorm = norm;
    }

    float val = desc.minVal + norm * (desc.maxVal - desc.minVal);
    val = stepQuantize(val, desc);
    if (std::fabs(val - snap.values[static_cast<size_t>(pid)]) < 1e-4f) {
        return 0;
    }

    if (out && maxCmds > 0) {
        out[0] = UiCommand{};
        out[0].type = UiCommandType::SET_PARAM;
        out[0].paramId = pid;
        out[0].value = val;
        return 1;
    }
    return 0;
}

size_t UiModel::handleFootswitch(const UiEvent& ev,
                                 const UiSnapshot& snap,
                                 UiCommand* out,
                                 size_t maxCmds) {
    if (!ev.pressed) return 0;
    if (ev.controlId == 0) {
        // Bypass footswitch
        bool nextBypass = !snap.bypassed;
        setStatusBanner(nextBypass ? "SW BYPASS ON" : "EFFECT ACTIVE", ev.timestampUs);
        if (out && maxCmds > 0) {
            out[0] = UiCommand{};
            out[0].type = UiCommandType::TOGGLE_BYPASS;
            out[0].value = nextBypass ? 1.0f : 0.0f;
            return 1;
        }
    } else if (ev.controlId == 1) {
        // Tap tempo footswitch
        if (out && maxCmds > 0) {
            out[0] = UiCommand{};
            out[0].type = UiCommandType::TAP_TEMPO;
            out[0].timestampUs = ev.timestampUs;
            return 1;
        }
    }
    return 0;
}

} // namespace hydra::ui
