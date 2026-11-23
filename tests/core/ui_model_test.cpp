#include "hydra_ui_catalog.h"
#include "hydra_ui_model.h"
#include "hydra_ui_renderer.h"
#include "hydra_ui_snapshot.h"
#include "hydra_dsp.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace hydra::ui;

static UiSnapshot makeDefaultSnapshot(const UiCatalog& cat) {
    UiSnapshot s{};
    for (size_t i = 0; i < static_cast<size_t>(UiParamId::COUNT); ++i) {
        s.values[i] = cat.get(static_cast<UiParamId>(i)).defaultVal;
    }
    s.effectiveBpm = 120.0f;
    return s;
}

static void testCatalogAndRuntimeMax() {
    hydra_dsp_handle* h = nullptr;
    assert(hydra_dsp_create(44100.0f, 3500.0f, &h) == 0 && h != nullptr);

    hydra_dsp_param_spec dlySpec{};
    assert(hydra_dsp_get_param_spec_for_handle(h, HYDRA_DSP_PARAM_DELAY_MS, &dlySpec) == 0);
    assert(std::fabs(dlySpec.max_value - 3500.0f) < 1e-3f);

    UiCatalog cat;
    cat.syncFromDspSpecs(hydra_dsp_get_param_specs(), hydra_dsp_get_param_count(), dlySpec.max_value);

    assert(std::fabs(cat.get(UiParamId::DELAY_TIME_MS).minVal - 10.0f) < 1e-3f);
    assert(std::fabs(cat.get(UiParamId::DELAY_TIME_MS).maxVal - 3500.0f) < 1e-3f);
    assert(std::fabs(cat.get(UiParamId::FLUTTER_DEPTH).maxVal - 45.0f) < 1e-3f);
    assert(std::fabs(cat.get(UiParamId::WOW_DEPTH).maxVal - 35.0f) < 1e-3f);
    assert(std::fabs(cat.get(UiParamId::DROPOUT).maxVal - 40.0f) < 1e-3f);

    hydra_dsp_destroy(h);
}

static void testFormatting() {
    UiCatalog cat;
    char buf[32];

    UiCatalog::formatValue(cat.get(UiParamId::ACTIVE_HEADS), 1.0f, buf, sizeof(buf));
    assert(std::strcmp(buf, "A - -") == 0);
    UiCatalog::formatValue(cat.get(UiParamId::ACTIVE_HEADS), 5.0f, buf, sizeof(buf));
    assert(std::strcmp(buf, "A - C") == 0);
    UiCatalog::formatValue(cat.get(UiParamId::ACTIVE_HEADS), 7.0f, buf, sizeof(buf));
    assert(std::strcmp(buf, "A B C") == 0);

    UiCatalog::formatValue(cat.get(UiParamId::FREEZE), 0.0f, buf, sizeof(buf));
    assert(std::strcmp(buf, "OFF") == 0);
    UiCatalog::formatValue(cat.get(UiParamId::FREEZE), 1.0f, buf, sizeof(buf));
    assert(std::strcmp(buf, "ON") == 0);

    UiCatalog::formatValue(cat.get(UiParamId::REVERSE_MODE), 0.0f, buf, sizeof(buf));
    assert(std::strcmp(buf, "RHYTHM") == 0);
    UiCatalog::formatValue(cat.get(UiParamId::REVERSE_MODE), 1.0f, buf, sizeof(buf));
    assert(std::strcmp(buf, "FREE") == 0);
    UiCatalog::formatValue(cat.get(UiParamId::REVERSE_DIVISION), 6.0f, buf, sizeof(buf));
    assert(std::strcmp(buf, "1/8.") == 0);

    UiCatalog::formatValue(cat.get(UiParamId::DELAY_TIME_MS), 500.0f, buf, sizeof(buf), false);
    assert(std::strcmp(buf, "500 ms") == 0);
    UiCatalog::formatValue(cat.get(UiParamId::DELAY_TIME_MS), 500.0f, buf, sizeof(buf), true);
    assert(std::strcmp(buf, "120 bpm") == 0);
}

static void testHomeAndBypass() {
    UiCatalog cat;
    UiModel model(cat);
    UiSnapshot snap = makeDefaultSnapshot(cat);
    UiCommand cmds[4]{};

    assert(model.screen() == UiScreen::SPLASH);
    model.dismissSplash();
    assert(model.screen() == UiScreen::HOME);
    assert(model.homeFocus() == HomeFocus::TIME);

    // Rotate to FB, click to edit, rotate +5, click to exit
    assert(model.handleEvent({UiEventType::ROTATE, 1}, snap, cmds, 4) == 0);
    assert(model.homeFocus() == HomeFocus::FB);
    assert(model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4) == 0);
    assert(model.isHomeEditing());

    // Long press while editing should safely exit edit, NOT toggle bypass
    assert(model.handleEvent({UiEventType::LONG_PRESS, 0}, snap, cmds, 4) == 0);
    assert(!model.isHomeEditing());

    // Re-enter edit and adjust FB
    assert(model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4) == 0);
    size_t n = model.handleEvent({UiEventType::ROTATE, 5}, snap, cmds, 4);
    assert(n == 1 && cmds[0].type == UiCommandType::SET_PARAM && cmds[0].paramId == UiParamId::FEEDBACK);
    assert(std::fabs(cmds[0].value - 45.0f) < 1e-3f);
    assert(model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4) == 0);
    assert(!model.isHomeEditing());

    // Long press on Home (not editing) toggles SW bypass
    n = model.handleEvent({UiEventType::LONG_PRESS, 0}, snap, cmds, 4);
    assert(n == 1 && cmds[0].type == UiCommandType::TOGGLE_BYPASS && cmds[0].value > 0.5f);
}

static void testMenuFocusPersistenceAndBounds() {
    UiCatalog cat;
    UiModel model(cat);
    model.dismissSplash();
    UiSnapshot snap = makeDefaultSnapshot(cat);
    UiCommand cmds[4]{};

    // Move to MENU focus and click
    model.handleEvent({UiEventType::ROTATE, 3}, snap, cmds, 4);
    assert(model.homeFocus() == HomeFocus::MENU);
    model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4);
    assert(model.screen() == UiScreen::MENU);

    // Rotate in Menu to REVERSE (index 4) - must never emit commands
    assert(model.handleEvent({UiEventType::ROTATE, 4}, snap, cmds, 4) == 0);
    assert(model.currentGroup() == UiGroupId::REVERSE);

    // Click to enter PARAM_LIST
    assert(model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4) == 0);
    assert(model.screen() == UiScreen::PARAM_LIST);
    assert(model.listCursor() == 0);

    // Move to item 1 (REVERSE_MODE) and click to edit
    assert(model.handleEvent({UiEventType::ROTATE, 1}, snap, cmds, 4) == 0);
    assert(model.listCursor() == 1);
    assert(model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4) == 0);
    assert(model.screen() == UiScreen::PARAM_EDIT);
    assert(model.editParamId() == UiParamId::REVERSE_MODE);

    // Even with delta=5, enum must only step by +1 (no acceleration)
    size_t n = model.handleEvent({UiEventType::ROTATE, 5}, snap, cmds, 4);
    assert(n == 1 && cmds[0].type == UiCommandType::SET_PARAM);
    assert(std::fabs(cmds[0].value - 1.0f) < 1e-3f);

    // Click to return to PARAM_LIST: listCursor must still be 1
    model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4);
    assert(model.screen() == UiScreen::PARAM_LIST);
    assert(model.listCursor() == 1);

    // Long press to MENU, re-enter REVERSE: listCursor still 1!
    model.handleEvent({UiEventType::LONG_PRESS, 0}, snap, cmds, 4);
    assert(model.screen() == UiScreen::MENU);
    assert(model.currentGroup() == UiGroupId::REVERSE);
    model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4);
    assert(model.listCursor() == 1);

    // Test numeric bounds clamping on DELAY_TIME_MS
    model.handleEvent({UiEventType::LONG_PRESS, 0}, snap, cmds, 4); // MENU
    model.handleEvent({UiEventType::ROTATE, -3}, snap, cmds, 4);   // DELAY (index 1)
    assert(model.currentGroup() == UiGroupId::DELAY);
    model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4);     // PARAM_LIST
    model.handleEvent({UiEventType::ROTATE, 1}, snap, cmds, 4);    // DELAY_TIME_MS
    model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4);     // PARAM_EDIT
    n = model.handleEvent({UiEventType::ROTATE, -1000}, snap, cmds, 4);
    assert(n == 1 && std::fabs(cmds[0].value - 10.0f) < 1e-3f);
    n = model.handleEvent({UiEventType::ROTATE, 1000}, snap, cmds, 4);
    assert(n == 1 && std::fabs(cmds[0].value - 2000.0f) < 1e-3f);
}

static void testTapTempoAndPresetsAndPeripherals() {
    UiCatalog cat;
    UiModel model(cat);
    model.dismissSplash();
    UiSnapshot snap = makeDefaultSnapshot(cat);
    UiCommand cmds[4]{};

    // Enter MENU -> TEMPO (index 7)
    model.handleEvent({UiEventType::ROTATE, 3}, snap, cmds, 4);
    model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4);
    model.handleEvent({UiEventType::ROTATE, 7}, snap, cmds, 4);
    assert(model.currentGroup() == UiGroupId::TEMPO);
    model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4);
    // Item 1 in TEMPO is ACTION_TAP_TEMPO
    model.handleEvent({UiEventType::ROTATE, 1}, snap, cmds, 4);
    model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4);
    assert(model.screen() == UiScreen::TAP_TEMPO);

    UiEvent tapEv{};
    tapEv.type = UiEventType::CLICK;
    tapEv.timestampUs = 1234567ULL;
    size_t n = model.handleEvent(tapEv, snap, cmds, 4);
    assert(n == 1 && cmds[0].type == UiCommandType::TAP_TEMPO);
    assert(cmds[0].timestampUs == 1234567ULL);

    // Long press exits Tap Tempo
    model.handleEvent({UiEventType::LONG_PRESS, 0}, snap, cmds, 4);
    assert(model.screen() == UiScreen::PARAM_LIST);

    // Presets confirmation
    model.handleEvent({UiEventType::LONG_PRESS, 0}, snap, cmds, 4); // MENU
    model.handleEvent({UiEventType::ROTATE, 1}, snap, cmds, 4);    // PRESETS (index 8)
    model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4);
    assert(model.screen() == UiScreen::PRESETS);

    assert(model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4) == 0);
    assert(model.isPresetConfirmPending());
    // Rotate cancels confirm
    model.handleEvent({UiEventType::ROTATE, 1}, snap, cmds, 4);
    assert(!model.isPresetConfirmPending());
    // Click twice confirms load
    model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4);
    n = model.handleEvent({UiEventType::CLICK, 0}, snap, cmds, 4);
    assert(n == 1 && cmds[0].type == UiCommandType::LOAD_PRESET && cmds[0].presetIndex == 1);

    // Pot soft-takeover
    snap.values[static_cast<size_t>(UiParamId::FEEDBACK)] = 80.0f;
    model.onPresetLoaded(snap);
    assert(!model.potPickup(1).latched);
    UiEvent potEv{};
    potEv.type = UiEventType::POT_CHANGED;
    potEv.controlId = 1; // FB
    potEv.normValue = 0.2f; // far from 0.8
    assert(model.handleEvent(potEv, snap, cmds, 4) == 0);
    assert(!model.potPickup(1).latched);
    potEv.normValue = 0.82f; // crosses 0.8
    n = model.handleEvent(potEv, snap, cmds, 4);
    assert(model.potPickup(1).latched);
    assert(n == 1 && cmds[0].type == UiCommandType::SET_PARAM && cmds[0].paramId == UiParamId::FEEDBACK);
}

static void testRendererSmoke() {
    UiCatalog cat;
    UiModel model(cat);
    UiSnapshot snap = makeDefaultSnapshot(cat);
    UiRenderer renderer(false);
    std::vector<uint16_t> fb(UiRenderer::kWidth * UiRenderer::kHeight, 0);

    renderer.render(fb.data(), model, cat, snap, 0);
    model.dismissSplash();
    renderer.render(fb.data(), model, cat, snap, 1000000ULL);

    bool hasOrange = false;
    for (uint16_t px : fb) {
        if (px == UiRenderer::kColOrange) {
            hasOrange = true;
            break;
        }
    }
    assert(hasOrange);
}

int main() {
    testCatalogAndRuntimeMax();
    testFormatting();
    testHomeAndBypass();
    testMenuFocusPersistenceAndBounds();
    testTapTempoAndPresetsAndPeripherals();
    testRendererSmoke();
    std::printf("ui_model_test: ALL PASSED\n");
    return 0;
}
