#pragma once

#include "hydra_ui_types.h"
#include "hydra_dsp.h"
#include <cstdint>
#include <cstddef>

namespace hydra::ui {

enum class ParamValueKind : uint8_t {
    FLOAT = 0,
    PERCENT,
    MS,
    HZ,
    BPM,
    INT,
    BOOL,
    ENUM,
    HEADS_MASK,
    ACTION,
    UNAVAILABLE
};

struct UiParamDesc {
    UiParamId id;
    int dspParamId;           // hydra_dsp_param_id or -1 if host/action
    UiGroupId group;
    const char* label;
    const char* shortLabel;
    const char* unit;
    ParamValueKind kind;
    float minVal;
    float maxVal;
    float step;
    float defaultVal;
    const char* const* enumLabels;
    uint8_t enumCount;
    bool available;
    bool allowAccel;          // Never true for BOOL, ENUM, HEADS_MASK, ACTION
};

struct UiPresetDesc {
    const char* cliName;
    const char* displayName;
    const char* subtitle;
};

class UiCatalog {
public:
    UiCatalog();

    // Sync ranges/defaults from DSP C API specs (and optional handle runtime max)
    void syncFromDspSpecs(const hydra_dsp_param_spec* specs,
                          uint32_t count,
                          float runtimeMaxDelayMs = 2000.0f);

    const UiParamDesc& get(UiParamId id) const;
    UiParamDesc& getMutable(UiParamId id);

    // Group query (returns number of items in group, fills outIds up to maxOut)
    size_t getGroupParams(UiGroupId group, UiParamId* outIds, size_t maxOut) const;

    static const char* groupName(UiGroupId group);
    static const char* semanticModeName(SemanticModeId mode);
    static const char* semanticModeShort(SemanticModeId mode);

    static size_t presetCount();
    static const UiPresetDesc& presetAt(size_t idx);
    static constexpr bool isPresetSaveAvailable() { return false; }

    static void formatValue(const UiParamDesc& desc,
                            float value,
                            char* outBuf,
                            size_t outBufSize,
                            bool delayUnitBpm = false);

private:
    UiParamDesc params_[static_cast<size_t>(UiParamId::COUNT)];
};

} // namespace hydra::ui
