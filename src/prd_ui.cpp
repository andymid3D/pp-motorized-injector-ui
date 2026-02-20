#include "prd_ui.h"

#include "display_comms.h"
#include "storage.h"
#include "ui/eez-flow.h"
#include "ui/screens.h"

#include <Arduino.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <lvgl.h>
#include <vector>

namespace {

constexpr lv_coord_t LEFT_X = 8;
constexpr lv_coord_t LEFT_WIDTH = 114;
constexpr lv_coord_t RIGHT_X = 130;
constexpr lv_coord_t RIGHT_WIDTH = 350;
constexpr lv_coord_t SCREEN_WIDTH = 480;
constexpr lv_coord_t SCREEN_HEIGHT = 800;
constexpr int MAX_MOULD_PROFILES = 16;
constexpr uint32_t DOUBLE_TAP_MS = 420;

const char *COMMON_FIELD_NAMES[] = {
    "Trap Accel",          "Compress Torque",  "Micro Interval (ms)",
    "Micro Duration (ms)", "Purge Up",         "Purge Down",
    "Purge Current",       "Antidrip Vel",     "Antidrip Current",
    "Release Dist",        "Release Trap Vel", "Release Current",
    "Contactor Cycles",    "Contactor Limit",
};

const bool COMMON_FIELD_IS_INTEGER[] = {false, false, true,  true,  false,
                                        false, false, false, false, false,
                                        false, false, true,  true};

constexpr int COMMON_FIELD_COUNT =
    sizeof(COMMON_FIELD_NAMES) / sizeof(COMMON_FIELD_NAMES[0]);

const char *MOULD_FIELD_NAMES[] = {
    "Name",         "Fill Volume",  "Fill Speed",    "Fill Pressure",
    "Pack Volume",  "Pack Speed",   "Pack Pressure", "Pack Time",
    "Cooling Time", "Fill Accel",   "Fill Decel",    "Pack Accel",
    "Pack Decel",   "Mode (2D/3D)", "Inject Torque",
};

// 0=String, 1=Float, 2=Integer (none currently)
constexpr int MOULD_FIELD_TYPE[] = {0, 1, 1, 1, 1, 1, 1, 1,
                                    1, 1, 1, 1, 1, 0, 1};

constexpr int MOULD_FIELD_COUNT =
    sizeof(MOULD_FIELD_NAMES) / sizeof(MOULD_FIELD_NAMES[0]);

struct RefillBlock {
  float volume; // cm3
  uint32_t addedMs;
  bool active;

  RefillBlock() : volume(0), addedMs(0), active(false) {}
  RefillBlock(float v, uint32_t a, bool act)
      : volume(v), addedMs(a), active(act) {}
};

struct UiState {
  bool initialized = false;

  lv_obj_t *rightPanelMain = nullptr;
  lv_obj_t *rightPanelMould = nullptr;
  lv_obj_t *rightPanelCommon = nullptr;

  lv_obj_t *posLabelMain = nullptr;
  lv_obj_t *tempLabelMain = nullptr;
  lv_obj_t *posLabelMould = nullptr;
  lv_obj_t *tempLabelMould = nullptr;
  lv_obj_t *posLabelCommon = nullptr;
  lv_obj_t *tempLabelCommon = nullptr;

  lv_obj_t *stateValue = nullptr;
  lv_obj_t *stateAction1 = nullptr;
  lv_obj_t *stateAction2 = nullptr;
  lv_obj_t *mainErrorLabel = nullptr;

  lv_obj_t *mouldList = nullptr;
  lv_obj_t *mouldNotice = nullptr;

  lv_obj_t *mouldButtonBack = nullptr;
  lv_obj_t *mouldButtonSend = nullptr;
  lv_obj_t *mouldButtonEdit = nullptr;
  lv_obj_t *mouldButtonSave = nullptr;
  lv_obj_t *mouldButtonNew = nullptr;
  lv_obj_t *mouldButtonDelete = nullptr;
  lv_obj_t *mouldDeleteOverlay = nullptr;
  lv_obj_t *mouldProfileButtons[MAX_MOULD_PROFILES] = {};
  DisplayComms::MouldParams mouldProfiles[MAX_MOULD_PROFILES] = {};
  int mouldProfileCount = 0;
  int selectedMould = -1;
  int lastTappedMould = -1;
  uint32_t lastTapMs = 0;

  lv_obj_t *commonScroll = nullptr;
  lv_obj_t *commonNotice = nullptr;
  lv_obj_t *commonButtonBack = nullptr;
  lv_obj_t *commonButtonSend = nullptr;
  lv_obj_t *commonInputs[COMMON_FIELD_COUNT] = {};
  lv_obj_t *commonDiscardOverlay = nullptr;
  bool commonDirty = false;
  bool suppressCommonEvents = false;

  lv_obj_t *sharedKeyboard = nullptr;
  char lastMouldName[32] = {0};
  lv_obj_t *lastMainScreen = nullptr;
  lv_obj_t *lastMouldScreen = nullptr;
  lv_obj_t *lastCommonScreen = nullptr;
  lv_obj_t *lastActiveScreen = nullptr;
  lv_obj_t *activeScrollContainer = nullptr;

  lv_obj_t *rightPanelMouldEdit = nullptr;
  lv_obj_t *mouldEditScroll = nullptr;
  lv_obj_t *mouldEditInputs[MOULD_FIELD_COUNT] = {};
  bool mouldEditDirty = false;
  bool inMouldEditPopulation = false;

  RefillBlock refillBlocks[16];
  int blockCount = 0;
  char lastState[24] = "";
  float startRefillPos = 0;
  float lastFramePos = 0;
  bool isRefilling = false;
  bool refillSequenceActive =
      false; // New flag for strictly tracking REFILL -> ... -> READY sequence

  bool mockEnabled = false;
  float mockPos = 0;
  char mockState[24] = "";
};

UiState ui;

// Forward declaration
void createMouldEditPanel();

inline bool isObjReady(lv_obj_t *obj) { return obj && lv_obj_is_valid(obj); }

inline void uiYield() { delay(0); }

inline void hideIfPresent(lv_obj_t *obj) {
  if (isObjReady(obj)) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

static void async_scroll_to_view(void *target) {
  if (isObjReady((lv_obj_t *)target)) {
    Serial.println("PRD_UI: ASYNC Scrolling to view.");
    lv_obj_scroll_to_view((lv_obj_t *)target, LV_ANIM_OFF);
  }
}

float turnsToCm3(float turns) {
  // Correct Mapping based on 22->360 Refill:
  // 22.53 = Full (Volume 0 relative to stroke start? No, Volume 0 of
  // accumulated shot). 360.5 = Empty (Volume 338 of accumulated shot). So
  // Volume = (turns - 22.53). If turns < 22.53, Vol = 0.
  float vol = turns - 22.53f;
  if (vol < 0)
    vol = 0;
  return vol;
}
void setButtonEnabled(lv_obj_t *button, bool enabled) {
  if (!button) {
    return;
  }
  if (enabled) {
    lv_obj_clear_state(button, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(button, LV_STATE_DISABLED);
  }
}

void setLabelTextIfChanged(lv_obj_t *label, const char *text) {
  if (!label || !text) {
    return;
  }
  if (lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) {
    return;
  }
  const char *current = lv_label_get_text(label);
  if (!current || strcmp(current, text) != 0) {
    lv_label_set_text(label, text);
  }
}

void setTextareaTextIfChanged(lv_obj_t *textarea, const char *text) {
  if (!textarea || !text) {
    return;
  }
  const char *current = lv_textarea_get_text(textarea);
  if (!current || strcmp(current, text) != 0) {
    lv_textarea_set_text(textarea, text);
  }
}

void setNotice(lv_obj_t *label, const char *text,
               lv_color_t color = lv_color_hex(0xffd6d6d6)) {
  if (!label) {
    return;
  }
  lv_obj_set_style_text_color(label, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  setLabelTextIfChanged(label, text ? text : "");
}

lv_obj_t *createButton(lv_obj_t *parent, const char *text, lv_coord_t x,
                       lv_coord_t y, lv_coord_t w, lv_coord_t h,
                       lv_event_cb_t cb, void *userData = nullptr) {
  if (!isObjReady(parent))
    return nullptr;

  lv_obj_t *button = lv_button_create(parent);
  if (!button) {
    Serial.println("PRD_UI: FAILED to create button!");
    return nullptr;
  }

  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, w, h);
  lv_obj_set_style_radius(button, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x1f5ea8),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER,
                          LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(button, lv_color_hex(0xffffff),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  if (cb) {
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, userData);
  }

  lv_obj_t *label = lv_label_create(button);
  if (label) {
    lv_label_set_text(label, text);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
  }
  return button;
}

void hideLegacyWidgets() {
  // Keep main screen from the known-good demo for now.
  // Replace only Mould/Common in this step.
  hideIfPresent(objects.obj3);
  hideIfPresent(objects.obj4);
  hideIfPresent(objects.button_to_mould_settings_1);

  // Main screen legacy
  hideIfPresent(objects.obj1);
  hideIfPresent(objects.button_to_mould_settings);
  hideIfPresent(objects.button_to_mould_settings_3);

  // Common settings
  hideIfPresent(objects.obj6);
  hideIfPresent(objects.button_to_mould_settings_2);

  // Redundant numeric labels over plungers
  hideIfPresent(objects.plunger_tip__obj0);
  hideIfPresent(objects.obj0__obj0);
  hideIfPresent(objects.obj2__obj0);
  hideIfPresent(objects.obj5__obj0);
}

lv_obj_t *createRightPanel(lv_obj_t *screen) {
  if (!isObjReady(screen)) {
    Serial.println("PRD_UI: createRightPanel skipped (invalid screen)");
    return nullptr;
  }
  lv_obj_t *panel = lv_obj_create(screen);
  if (!panel) {
    Serial.println(
        "PRD_UI: createRightPanel FAILED - lv_obj_create returned NULL");
    return nullptr;
  }
  lv_obj_set_pos(panel, RIGHT_X, 0);
  lv_obj_set_size(panel, RIGHT_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x11151a),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  return panel;
}

void hideKeyboard() {
  if (isObjReady(ui.sharedKeyboard)) {
    Serial.println("PRD_UI: hideKeyboard called.");
    lv_keyboard_set_textarea(ui.sharedKeyboard, nullptr);
    lv_obj_add_flag(ui.sharedKeyboard, LV_OBJ_FLAG_HIDDEN);

    // Reset background scroll if we were tracking a container
    if (isObjReady(ui.activeScrollContainer)) {
      Serial.println("PRD_UI: Resetting scroll container.");
      lv_obj_scroll_to_y(ui.activeScrollContainer, 0,
                         LV_ANIM_OFF); // Off for snappier return
      ui.activeScrollContainer = nullptr;
    }

    // Restore navigation responsiveness
    if (isObjReady(ui.commonButtonBack))
      lv_obj_move_foreground(ui.commonButtonBack);
    if (isObjReady(ui.mouldButtonBack))
      lv_obj_move_foreground(ui.mouldButtonBack);
  }
}

void onKeyboardEvent(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    hideKeyboard();
  }
}

lv_obj_t *getSharedKeyboard(lv_obj_t *parent) {
  // IGNORE parent. Always use top layer to prevent scrolling with panels.
  lv_obj_t *top = lv_layer_top();
  if (!isObjReady(ui.sharedKeyboard)) {
    Serial.println("PRD_UI: Creating shared keyboard on TOP LAYER.");
    ui.sharedKeyboard = lv_keyboard_create(top);
    lv_obj_set_size(ui.sharedKeyboard, SCREEN_WIDTH, 300);

    // Position it fixed on the top layer.
    // Trying even higher up - maybe screen origin is different or 80 is not
    // enough. Alignment to TOP_MID means Y=0 is the top edge.
    lv_obj_align(ui.sharedKeyboard, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_add_flag(ui.sharedKeyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ui.sharedKeyboard, onKeyboardEvent, LV_EVENT_ALL,
                        nullptr);

    // Clean style
    lv_obj_set_style_border_color(ui.sharedKeyboard, lv_color_hex(0xffec18),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(ui.sharedKeyboard, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui.sharedKeyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.sharedKeyboard, lv_color_hex(0x1a222b),
                              LV_PART_MAIN);
  } else {
    // Ensure it stays on top layer if it was moved
    if (lv_obj_get_parent(ui.sharedKeyboard) != top) {
      lv_obj_set_parent(ui.sharedKeyboard, top);
    }
  }
  lv_obj_move_foreground(ui.sharedKeyboard);
  return ui.sharedKeyboard;
}

void showKeyboard(lv_obj_t *textarea, lv_obj_t *scrollContainer,
                  lv_keyboard_mode_t mode) {
  if (!textarea)
    return;
  lv_obj_t *kb = getSharedKeyboard(nullptr);
  if (!kb) {
    Serial.println("PRD_UI: FAILED to get shared keyboard");
    return;
  }

  // GUARD: If already focused on this textarea, don't re-align (prevents
  // re-entry hangs)
  if (lv_keyboard_get_textarea(kb) == textarea &&
      !lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN)) {
    return;
  }

  Serial.print("PRD_UI: showKeyboard for textarea: ");
  Serial.println((uintptr_t)textarea, HEX);

  // Track this container for reset on hide
  ui.activeScrollContainer = scrollContainer;

  if (!kb) {
    Serial.println("PRD_UI: FAILED to get shared keyboard");
    return;
  }

  // Position check
  lv_area_t area;
  lv_obj_get_coords(textarea, &area);
  int32_t screen_y = area.y1;

  // If upper half of screen, put keyboard at bottom. Else put at top.
  // Screen is 800 high. Let's use 350 as threshold.
  if (screen_y < 350) {
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -20);
  } else {
    lv_obj_align(kb, LV_ALIGN_TOP_MID, 0, 50);
  }

  lv_keyboard_set_textarea(kb, textarea);
  lv_keyboard_set_mode(kb, mode);
  lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(kb);

  if (isObjReady(scrollContainer)) {
    Serial.println("PRD_UI: Scheduling ASYNC scroll.");
    lv_async_call(async_scroll_to_view, textarea);
  }
  Serial.println("PRD_UI: showKeyboard DONE.");
}

void navigateTo(int screen_id) {
  Serial.printf("PRD_UI: navigateTo %d\n", screen_id);
  hideKeyboard();
  eez_flow_set_screen(screen_id, LV_SCR_LOAD_ANIM_NONE, 0, 0);
}

void purgeMouldPanels() {
  Serial.println("PRD_UI: Purging Mould Panels (async)");
  if (isObjReady(ui.sharedKeyboard)) {
    lv_obj_add_flag(ui.sharedKeyboard, LV_OBJ_FLAG_HIDDEN);
  }

  if (isObjReady(ui.rightPanelMould))
    lv_obj_delete_async(ui.rightPanelMould);
  if (isObjReady(ui.rightPanelMouldEdit))
    lv_obj_delete_async(ui.rightPanelMouldEdit);
  if (isObjReady(ui.mouldDeleteOverlay))
    lv_obj_delete_async(ui.mouldDeleteOverlay);
  if (isObjReady(ui.posLabelMould))
    lv_obj_delete_async(ui.posLabelMould);
  if (isObjReady(ui.tempLabelMould))
    lv_obj_delete_async(ui.tempLabelMould);

  ui.rightPanelMould = nullptr;
  ui.rightPanelMouldEdit = nullptr;
  ui.mouldList = nullptr;
  ui.mouldNotice = nullptr;
  ui.mouldButtonBack = nullptr;
  ui.mouldButtonSend = nullptr;
  ui.mouldButtonEdit = nullptr;
  ui.mouldButtonSave = nullptr;
  ui.mouldButtonNew = nullptr;
  ui.mouldButtonDelete = nullptr;
  ui.mouldDeleteOverlay = nullptr;
  ui.posLabelMould = nullptr;
  ui.tempLabelMould = nullptr;
  ui.mouldEditScroll = nullptr;
  for (int i = 0; i < MAX_MOULD_PROFILES; i++)
    ui.mouldProfileButtons[i] = nullptr;
  for (int i = 0; i < MOULD_FIELD_COUNT; i++)
    ui.mouldEditInputs[i] = nullptr;
}

void purgeCommonPanel() {
  Serial.println("PRD_UI: Purging Common Panel (async)");
  if (isObjReady(ui.sharedKeyboard)) {
    lv_obj_add_flag(ui.sharedKeyboard, LV_OBJ_FLAG_HIDDEN);
  }

  if (isObjReady(ui.rightPanelCommon))
    lv_obj_delete_async(ui.rightPanelCommon);
  if (isObjReady(ui.posLabelCommon))
    lv_obj_delete_async(ui.posLabelCommon);
  if (isObjReady(ui.tempLabelCommon))
    lv_obj_delete_async(ui.tempLabelCommon);

  ui.rightPanelCommon = nullptr;
  ui.commonScroll = nullptr;
  ui.commonNotice = nullptr;
  ui.commonButtonBack = nullptr;
  ui.commonButtonSend = nullptr;
  ui.commonDiscardOverlay = nullptr;
  ui.posLabelCommon = nullptr;
  ui.tempLabelCommon = nullptr;
  for (int i = 0; i < COMMON_FIELD_COUNT; i++)
    ui.commonInputs[i] = nullptr;
}

void purgeMainPanel() {
  if (isObjReady(ui.rightPanelMain))
    lv_obj_delete_async(ui.rightPanelMain);
  if (isObjReady(ui.posLabelMain))
    lv_obj_delete_async(ui.posLabelMain);
  if (isObjReady(ui.tempLabelMain))
    lv_obj_delete_async(ui.tempLabelMain);

  ui.rightPanelMain = nullptr;
  ui.posLabelMain = nullptr;
  ui.tempLabelMain = nullptr;
  ui.stateValue = nullptr;
  ui.stateAction1 = nullptr;
  ui.stateAction2 = nullptr;
  ui.mainErrorLabel = nullptr;
}

void updateErrorFrames(const DisplayComms::Status &status) {
  bool hasError = (status.errorCode != 0) || (status.errorMsg[0] != '\0');
  lv_obj_t *panels[] = {ui.rightPanelMain, ui.rightPanelMould,
                        ui.rightPanelCommon, ui.rightPanelMouldEdit};

  for (lv_obj_t *panel : panels) {
    if (!panel) {
      continue;
    }
    lv_obj_set_style_border_width(panel, hasError ? 4 : 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xffc62828),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  if (ui.mainErrorLabel) {
    if (hasError) {
      char text[120];
      if (status.errorMsg[0] != '\0') {
        snprintf(text, sizeof(text), "ERROR 0x%X: %s", status.errorCode,
                 status.errorMsg);
      } else {
        snprintf(text, sizeof(text), "ERROR 0x%X", status.errorCode);
      }
      setLabelTextIfChanged(ui.mainErrorLabel, text);
      lv_obj_clear_flag(ui.mainErrorLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
      setLabelTextIfChanged(ui.mainErrorLabel, "");
      lv_obj_add_flag(ui.mainErrorLabel, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void updateLeftReadouts(const DisplayComms::Status &status) {
  char posText[40];
  char tempText[40];

  snprintf(posText, sizeof(posText), "%.2f", status.encoderTurns);
  snprintf(tempText, sizeof(tempText), "%.1f C", status.tempC);

  setLabelTextIfChanged(ui.posLabelMain, posText);
  setLabelTextIfChanged(ui.posLabelMould, posText);
  setLabelTextIfChanged(ui.posLabelCommon, posText);

  setLabelTextIfChanged(ui.tempLabelMain, tempText);
  setLabelTextIfChanged(ui.tempLabelMould, tempText);
  setLabelTextIfChanged(ui.tempLabelCommon, tempText);
}

void onNavigate(lv_event_t *event) {
  intptr_t target = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  navigateTo(static_cast<int>(target));
}

void onStateActionQueryState(lv_event_t *) { DisplayComms::sendQueryState(); }

void onStateActionQueryError(lv_event_t *) { DisplayComms::sendQueryError(); }

void onMouldProfileSelect(lv_event_t *event) {
  int index = static_cast<int>(
      reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (index < 0 || index >= ui.mouldProfileCount) {
    return;
  }

  uint32_t now = millis();
  bool isDoubleTap =
      (ui.lastTappedMould == index) && ((now - ui.lastTapMs) <= DOUBLE_TAP_MS);
  ui.lastTappedMould = index;
  ui.lastTapMs = now;
  ui.selectedMould = index;

  for (int i = 0; i < ui.mouldProfileCount; i++) {
    if (!ui.mouldProfileButtons[i]) {
      continue;
    }
    if (i == ui.selectedMould) {
      lv_obj_set_style_bg_color(ui.mouldProfileButtons[i],
                                lv_color_hex(0x2d7dd2),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
      // Index 0 has a distinct "Current" base color
      lv_color_t baseColor =
          (i == 0) ? lv_color_hex(0x2e4a3e) : lv_color_hex(0x26303a);
      lv_obj_set_style_bg_color(ui.mouldProfileButtons[i], baseColor,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    }
  }

  if (isDoubleTap) {
    setNotice(ui.mouldNotice, "Edit flow pending: use Send for now.");
  }
}

void syncMouldSendEditEnablement() {
  bool hasSelection =
      ui.selectedMould >= 0 && ui.selectedMould < ui.mouldProfileCount;
  setButtonEnabled(ui.mouldButtonEdit, hasSelection);
  setButtonEnabled(ui.mouldButtonSend,
                   hasSelection && DisplayComms::isSafeForUpdate());

  // Disable delete for Index 0 (Current)
  bool canDelete = hasSelection && (ui.selectedMould != 0);
  setButtonEnabled(ui.mouldButtonDelete, canDelete);
}

void syncMouldEditSaveEnablement() {
  if (isObjReady(ui.mouldButtonSave)) {
    setButtonEnabled(ui.mouldButtonSave, ui.mouldEditDirty);
  }
}

void rebuildMouldList() {
  if (!ui.mouldList) {
    return;
  }

  lv_obj_clean(ui.mouldList);

  for (int i = 0; i < MAX_MOULD_PROFILES; i++) {
    ui.mouldProfileButtons[i] = nullptr;
  }

  int y = 8;
  for (int i = 0; i < ui.mouldProfileCount; i++) {
    char nameBuf[48];
    const char *name = ui.mouldProfiles[i].name[0] != '\0'
                           ? ui.mouldProfiles[i].name
                           : "Unnamed Mould";

    if (i == 0) {
      snprintf(nameBuf, sizeof(nameBuf), "(Current) %s", name);
    } else {
      snprintf(nameBuf, sizeof(nameBuf), "%s", name);
    }

    lv_obj_t *button =
        createButton(ui.mouldList, nameBuf, 8, y, 286, 46, onMouldProfileSelect,
                     reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    if (button) {
      lv_color_t baseColor =
          (i == 0) ? lv_color_hex(0x2e4a3e) : lv_color_hex(0x26303a);
      lv_obj_set_style_bg_color(button, baseColor,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_border_width(button, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_border_color(button, lv_color_hex(0x41505f),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
      ui.mouldProfileButtons[i] = button;
    }
    y += 54;
  }

  if (ui.selectedMould >= ui.mouldProfileCount) {
    ui.selectedMould = -1;
  }
  syncMouldSendEditEnablement();
}

void onMouldSend(lv_event_t *) {
  if (ui.selectedMould < 0 || ui.selectedMould >= ui.mouldProfileCount) {
    setNotice(ui.mouldNotice, "Select a mould first.", lv_color_hex(0xfff0a0));
    return;
  }
  if (!DisplayComms::isSafeForUpdate()) {
    setNotice(ui.mouldNotice, "Unsafe machine state for MOULD send.",
              lv_color_hex(0xffff7a));
    return;
  }

  if (DisplayComms::sendMould(ui.mouldProfiles[ui.selectedMould])) {
    setNotice(ui.mouldNotice, "MOULD command sent.", lv_color_hex(0xff9be7a5));
  } else {
    setNotice(ui.mouldNotice, "Failed to send MOULD command.",
              lv_color_hex(0xffff7a));
  }
}

// ... Mould Edit Implementation ...

void showMouldEditKeyboard(lv_obj_t *textarea) {
  Serial.print("PRD_UI: showMouldEditKeyboard for ");
  Serial.println((uintptr_t)textarea, HEX);
  intptr_t index = reinterpret_cast<intptr_t>(lv_obj_get_user_data(textarea));
  Serial.printf("PRD_UI: Mould Field index = %d\n", (int)index);

  if (index < 0 || index >= MOULD_FIELD_COUNT) {
    Serial.println("PRD_UI: ERROR - Mould field index out of bounds!");
    return;
  }

  lv_keyboard_mode_t mode = (MOULD_FIELD_TYPE[index] == 0)
                                ? LV_KEYBOARD_MODE_TEXT_LOWER
                                : LV_KEYBOARD_MODE_NUMBER;
  showKeyboard(textarea, ui.mouldEditScroll, mode);
}

void onMouldEditInputFocus(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t *target = lv_event_get_target_obj(event);
  // Only trigger on CLICKED to avoid re-entry hangs from focus shifts
  if (code == LV_EVENT_CLICKED) {
    Serial.printf("PRD_UI: onMouldEditInputFocus CLICKED.\n");
    showMouldEditKeyboard(target);
  }
}

void onMouldEditFieldChanged(lv_event_t *event) {
  if (ui.inMouldEditPopulation)
    return;
  ui.mouldEditDirty = true;
  syncMouldEditSaveEnablement();
}

void onMouldEditCancel(lv_event_t *) {
  hideKeyboard();
  // Safeguard keyboard
  if (isObjReady(ui.sharedKeyboard)) {
    lv_obj_set_parent(ui.sharedKeyboard, lv_layer_top());
  }
  // Destroy panel to free memory
  if (ui.rightPanelMouldEdit) {
    lv_obj_delete_async(ui.rightPanelMouldEdit);
    ui.rightPanelMouldEdit = nullptr;
    ui.mouldEditScroll = nullptr;
    for (int i = 0; i < MOULD_FIELD_COUNT; i++)
      ui.mouldEditInputs[i] = nullptr;
  }
  lv_obj_clear_flag(ui.rightPanelMould, LV_OBJ_FLAG_HIDDEN);
  Serial.printf("PRD_UI: MouldEditPanel Destroyed (Cancel). Heap: %d\n",
                ESP.getFreeHeap());
}

void onMouldEditSave(lv_event_t *) {
  hideKeyboard();

  // Save inputs back to snapshot -> ui.mouldProfiles
  DisplayComms::MouldParams &p = ui.mouldProfiles[ui.selectedMould];

  for (int i = 0; i < MOULD_FIELD_COUNT; i++) {
    lv_obj_t *input = ui.mouldEditInputs[i];
    if (!input)
      continue;

    if (i == 13) {
      int idx = lv_dropdown_get_selected(input);
      strcpy(p.mode, (idx == 1) ? "3D" : "2D");
    } else {
      const char *txt = lv_textarea_get_text(input);
      if (!txt)
        continue;

      if (i == 0) {
        strncpy(p.name, txt, sizeof(p.name) - 1);
        p.name[sizeof(p.name) - 1] = 0;
      } else {
        float val = atof(txt);
        switch (i) {
        case 1:
          p.fillVolume = val;
          break;
        case 2:
          p.fillSpeed = val;
          break;
        case 3:
          p.fillPressure = val;
          break;
        case 4:
          p.packVolume = val;
          break;
        case 5:
          p.packSpeed = val;
          break;
        case 6:
          p.packPressure = val;
          break;
        case 7:
          p.packTime = val;
          break;
        case 8:
          p.coolingTime = val;
          break;
        case 9:
          p.fillAccel = val;
          break;
        case 10:
          p.fillDecel = val;
          break;
        case 11:
          p.packAccel = val;
          break;
        case 12:
          p.packDecel = val;
          break;
        case 14:
          p.injectTorque = val;
          break;
        }
      }
    }
  }

  // VALIDATION
  bool is3D = (strcmp(p.mode, "3D") == 0);
  bool valid = true;
  if (is3D) {
    if (p.injectTorque <= 0.01f)
      valid = false;
  } else {
    // 2D: fillVolume and fillSpeed must be > 0
    if (p.fillVolume <= 0.01f || p.fillSpeed <= 0.01f)
      valid = false;
  }

  if (!valid) {
    setNotice(ui.mouldNotice,
              is3D ? "3D requires Inject Torque > 0"
                   : "2D requires Fill Volume/Speed > 0",
              lv_color_hex(0xffff7a));
    return;
  }

  Storage::saveMoulds(ui.mouldProfiles, ui.mouldProfileCount);
  ui.mouldEditDirty = false;
  syncMouldEditSaveEnablement();

  // Hide the panel first to show the underlying list immediately
  if (ui.rightPanelMouldEdit) {
    lv_obj_add_flag(ui.rightPanelMouldEdit, LV_OBJ_FLAG_HIDDEN);
  }

  // Destroy panel async
  if (ui.rightPanelMouldEdit) {
    lv_obj_delete_async(ui.rightPanelMouldEdit);
    ui.rightPanelMouldEdit = nullptr;
    ui.mouldEditScroll = nullptr;
    for (int i = 0; i < MOULD_FIELD_COUNT; i++)
      ui.mouldEditInputs[i] = nullptr;
  }

  // Refresh list and show it
  rebuildMouldList();
  lv_obj_clear_flag(ui.rightPanelMould, LV_OBJ_FLAG_HIDDEN);
  setNotice(ui.mouldNotice, "Profile saved.", lv_color_hex(0xff9be7a5));
  Serial.printf("PRD_UI: MouldEditPanel Destroyed. Heap: %d\n",
                ESP.getFreeHeap());
}

void onMouldEdit(lv_event_t *) {
  if (ui.inMouldEditPopulation)
    return;

  if (ui.selectedMould < 0 || ui.selectedMould >= ui.mouldProfileCount) {
    setNotice(ui.mouldNotice, "Select a mould first.", lv_color_hex(0xfff0a0));
    return;
  }

  // Create panel on demand if not exists
  if (!isObjReady(ui.rightPanelMouldEdit)) {
    Serial.printf("PRD_UI: onMouldEdit - Creating panel. Heap: %d\n",
                  ESP.getFreeHeap());
    // Clear inputs array explicitly
    for (int i = 0; i < MOULD_FIELD_COUNT; i++)
      ui.mouldEditInputs[i] = nullptr;
    createMouldEditPanel();
  }

  if (!isObjReady(ui.rightPanelMouldEdit)) {
    Serial.println("PRD_UI: ERROR - Failed to create MouldEditPanel!");
    return;
  }

  // Populate fields
  ui.inMouldEditPopulation = true;
  DisplayComms::MouldParams &p = ui.mouldProfiles[ui.selectedMould];
  Serial.printf("PRD_UI: Populating fields for mould %d (%s)\n",
                ui.selectedMould, p.name);

  for (int i = 0; i < MOULD_FIELD_COUNT; i++) {
    lv_obj_t *input = ui.mouldEditInputs[i];
    if (!isObjReady(input)) {
      Serial.printf("PRD_UI: Population field %d - NOT READY\n", i);
      continue;
    }

    if (i == 13) {
      int dropdownIdx = (p.mode[0] == '3') ? 1 : 0;
      lv_dropdown_set_selected(input, dropdownIdx);
    } else {
      char buf[32] = {0};
      if (MOULD_FIELD_TYPE[i] == 0) { // String
        snprintf(buf, sizeof(buf), "%s", (i == 0) ? p.name : "");
      } else { // Float
        float val = 0.0f;
        switch (i) {
        case 1:
          val = p.fillVolume;
          break;
        case 2:
          val = p.fillSpeed;
          break;
        case 3:
          val = p.fillPressure;
          break;
        case 4:
          val = p.packVolume;
          break;
        case 5:
          val = p.packSpeed;
          break;
        case 6:
          val = p.packPressure;
          break;
        case 7:
          val = p.packTime;
          break;
        case 8:
          val = p.coolingTime;
          break;
        case 9:
          val = p.fillAccel;
          break;
        case 10:
          val = p.fillDecel;
          break;
        case 11:
          val = p.packAccel;
          break;
        case 12:
          val = p.packDecel;
          break;
        case 14:
          val = p.injectTorque;
          break;
        }
        snprintf(buf, sizeof(buf), "%.2f", val);
      }

      Serial.printf("PRD_UI: Field %d (%p) -> '%s'\n", i, input, buf);
      lv_textarea_set_text(input, buf);
    }
  }

  // Show panel
  ui.mouldEditDirty = false;
  syncMouldEditSaveEnablement();

  lv_obj_clear_flag(ui.rightPanelMouldEdit, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui.rightPanelMould, LV_OBJ_FLAG_HIDDEN);
  Serial.println("PRD_UI: onMouldEdit COMPLETE.");
  ui.inMouldEditPopulation = false;
}

void onMouldNew(lv_event_t *) {
  if (ui.mouldProfileCount >= MAX_MOULD_PROFILES) {
    setNotice(ui.mouldNotice, "Profile limit reached.", lv_color_hex(0xffff7a));
    return;
  }

  DisplayComms::MouldParams newProfile = {};
  snprintf(newProfile.name, sizeof(newProfile.name), "Local %d",
           ui.mouldProfileCount + 1);
  newProfile.mode[0] = '2';
  newProfile.mode[1] = 'D';
  newProfile.mode[2] = '\0';

  // Set default values for new profile
  newProfile.fillVolume = 10.0f;
  newProfile.fillSpeed = 5.0f;
  newProfile.fillPressure = 50.0f;
  newProfile.packVolume = 2.0f;
  newProfile.packSpeed = 2.0f;
  newProfile.packPressure = 40.0f;
  newProfile.packTime = 2.0f;
  newProfile.coolingTime = 5.0f;
  newProfile.fillAccel = 100.0f;
  newProfile.fillDecel = 100.0f;
  newProfile.packAccel = 100.0f;
  newProfile.packDecel = 100.0f;
  newProfile.injectTorque = 0.5f;

  ui.mouldProfiles[ui.mouldProfileCount] = newProfile;
  ui.mouldProfileCount++;
  rebuildMouldList();
  Storage::saveMoulds(ui.mouldProfiles, ui.mouldProfileCount);
  setNotice(ui.mouldNotice, "Created local mould profile.",
            lv_color_hex(0xff9be7a5));
}

void onMouldDeleteReal(lv_event_t *) {
  if (ui.mouldDeleteOverlay) {
    lv_obj_add_flag(ui.mouldDeleteOverlay, LV_OBJ_FLAG_HIDDEN);
  }

  if (ui.selectedMould < 0 || ui.selectedMould >= ui.mouldProfileCount) {
    return;
  }

  const int removeIndex = ui.selectedMould;
  for (int i = removeIndex; i < ui.mouldProfileCount - 1; i++) {
    ui.mouldProfiles[i] = ui.mouldProfiles[i + 1];
  }
  if (ui.mouldProfileCount > 0) {
    ui.mouldProfileCount--;
  }

  if (removeIndex == 0) {
    ui.lastMouldName[0] = '\0';
  }

  ui.selectedMould = -1;
  ui.lastTappedMould = -1;
  rebuildMouldList();
  Storage::saveMoulds(ui.mouldProfiles, ui.mouldProfileCount);
  setNotice(ui.mouldNotice, "Profile deleted.", lv_color_hex(0xff9be7a5));
}

void onMouldDeleteCancel(lv_event_t *) {
  if (ui.mouldDeleteOverlay) {
    lv_obj_add_flag(ui.mouldDeleteOverlay, LV_OBJ_FLAG_HIDDEN);
  }
}

void onMouldDelete(lv_event_t *) {
  if (ui.selectedMould < 0 || ui.selectedMould >= ui.mouldProfileCount) {
    setNotice(ui.mouldNotice, "Select a mould first.", lv_color_hex(0xfff0a0));
    return;
  }

  // Create overlay on demand if needed
  if (!isObjReady(ui.mouldDeleteOverlay)) {
    ui.mouldDeleteOverlay = lv_obj_create(ui.rightPanelMould);
    lv_obj_set_size(ui.mouldDeleteOverlay, RIGHT_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(ui.mouldDeleteOverlay, 0, 0);
    lv_obj_set_style_bg_color(ui.mouldDeleteOverlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui.mouldDeleteOverlay, LV_OPA_70, 0);
    lv_obj_add_flag(ui.mouldDeleteOverlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *box = lv_obj_create(ui.mouldDeleteOverlay);
    lv_obj_set_size(box, 280, 200);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x2a3540), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x3a4a5a), 0);
    lv_obj_set_style_border_width(box, 2, 0);

    lv_obj_t *l = lv_label_create(box);
    lv_label_set_text(l, "Delete profile?");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *name = lv_label_create(box);
    lv_label_set_text(name, ui.mouldProfiles[ui.selectedMould].name);
    lv_obj_set_style_text_color(name, lv_color_hex(0xff9be7a5), 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 45);

    createButton(box, "Cancel", 10, 110, 110, 50, onMouldDeleteCancel);
    lv_obj_t *btnDel =
        createButton(box, "Delete", 130, 110, 110, 50, onMouldDeleteReal);
    lv_obj_set_style_bg_color(btnDel, lv_color_hex(0xa83232), 0);
  } else {
    // Update name label in case it changed
    lv_obj_t *box = lv_obj_get_child(ui.mouldDeleteOverlay, 0);
    lv_obj_t *name = lv_obj_get_child(box, 1);
    lv_label_set_text(name, ui.mouldProfiles[ui.selectedMould].name);
    lv_obj_clear_flag(ui.mouldDeleteOverlay, LV_OBJ_FLAG_HIDDEN);
  }
}

double commonFieldValue(const DisplayComms::CommonParams &common,
                        int fieldIndex) {
  switch (fieldIndex) {
  case 0:
    return common.trapAccel;
  case 1:
    return common.compressTorque;
  case 2:
    return common.microIntervalMs;
  case 3:
    return common.microDurationMs;
  case 4:
    return common.purgeUp;
  case 5:
    return common.purgeDown;
  case 6:
    return common.purgeCurrent;
  case 7:
    return common.antidripVel;
  case 8:
    return common.antidripCurrent;
  case 9:
    return common.releaseDist;
  case 10:
    return common.releaseTrapVel;
  case 11:
    return common.releaseCurrent;
  case 12:
    return common.contactorCycles;
  case 13:
    return common.contactorLimit;
  default:
    return 0.0;
  }
}

void assignCommonField(DisplayComms::CommonParams &common, int fieldIndex,
                       const char *text) {
  if (!text) {
    return;
  }
  if (COMMON_FIELD_IS_INTEGER[fieldIndex]) {
    uint32_t value = static_cast<uint32_t>(strtoul(text, nullptr, 10));
    switch (fieldIndex) {
    case 2:
      common.microIntervalMs = value;
      break;
    case 3:
      common.microDurationMs = value;
      break;
    case 12:
      common.contactorCycles = value;
      break;
    case 13:
      common.contactorLimit = value;
      break;
    default:
      break;
    }
    return;
  }

  float value = static_cast<float>(atof(text));
  switch (fieldIndex) {
  case 0:
    common.trapAccel = value;
    break;
  case 1:
    common.compressTorque = value;
    break;
  case 4:
    common.purgeUp = value;
    break;
  case 5:
    common.purgeDown = value;
    break;
  case 6:
    common.purgeCurrent = value;
    break;
  case 7:
    common.antidripVel = value;
    break;
  case 8:
    common.antidripCurrent = value;
    break;
  case 9:
    common.releaseDist = value;
    break;
  case 10:
    common.releaseTrapVel = value;
    break;
  case 11:
    common.releaseCurrent = value;
    break;
  default:
    break;
  }
}

void syncCommonInputsFromModel(const DisplayComms::CommonParams &common) {
  ui.suppressCommonEvents = true;
  for (int i = 0; i < COMMON_FIELD_COUNT; i++) {
    if (!ui.commonInputs[i]) {
      continue;
    }
    char buffer[32];
    if (COMMON_FIELD_IS_INTEGER[i]) {
      snprintf(buffer, sizeof(buffer), "%lu",
               static_cast<unsigned long>(commonFieldValue(common, i)));
    } else {
      snprintf(buffer, sizeof(buffer), "%.3f", commonFieldValue(common, i));
    }
    setTextareaTextIfChanged(ui.commonInputs[i], buffer);
  }
  ui.suppressCommonEvents = false;
}

void syncCommonSendEnablement() {
  bool safe = DisplayComms::isSafeForUpdate();
  Serial.printf("PRD_UI: syncCommonSendEnablement dirty=%d safe=%d\n",
                (int)ui.commonDirty, (int)safe);
  // Allow enabling button if dirty, even if unsafe (actual send command will
  // check safety)
  setButtonEnabled(ui.commonButtonSend, ui.commonDirty);
}

void onCommonInputFocus(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t *target = lv_event_get_target_obj(event);
  // Only trigger on CLICKED to avoid re-entry hangs from focus shifts
  if (code == LV_EVENT_CLICKED) {
    Serial.printf("PRD_UI: onCommonInputFocus CLICKED.\n");
    showKeyboard(target, ui.commonScroll, LV_KEYBOARD_MODE_NUMBER);
  }
}

void onCommonFieldChanged(lv_event_t *event) {
  if (ui.suppressCommonEvents) {
    return;
  }
  if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED ||
      lv_event_get_code(event) == LV_EVENT_READY) {
    ui.commonDirty = true;
    syncCommonSendEnablement();
  }
}

bool sendCommonFromInputs() {
  if (!DisplayComms::isSafeForUpdate()) {
    setNotice(ui.commonNotice, "Unsafe machine state for COMMON send.",
              lv_color_hex(0xffff7a));
    return false;
  }

  DisplayComms::CommonParams toSend = DisplayComms::getCommon();
  for (int i = 0; i < COMMON_FIELD_COUNT; i++) {
    if (!ui.commonInputs[i]) {
      continue;
    }
    assignCommonField(toSend, i, lv_textarea_get_text(ui.commonInputs[i]));
  }

  if (DisplayComms::sendCommon(toSend)) {
    setNotice(ui.commonNotice, "COMMON command sent.",
              lv_color_hex(0xff9be7a5));
    ui.commonDirty = false;
    syncCommonSendEnablement();
    return true;
  }

  setNotice(ui.commonNotice, "Failed to send COMMON command.",
            lv_color_hex(0xffff7a));
  return false;
}

void onCommonSend(lv_event_t *) { sendCommonFromInputs(); }

void showCommonDiscardOverlay(bool show) {
  if (!ui.commonDiscardOverlay) {
    return;
  }
  if (show) {
    hideKeyboard();
    lv_obj_clear_flag(ui.commonDiscardOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.commonDiscardOverlay);
  } else {
    lv_obj_add_flag(ui.commonDiscardOverlay, LV_OBJ_FLAG_HIDDEN);
  }
}

void onCommonBack(lv_event_t *) {
  if (ui.commonDirty && ui.commonDiscardOverlay) {
    showCommonDiscardOverlay(true);
  } else {
    navigateTo(SCREEN_ID_MAIN);
  }
}

void onCommonDiscardSend(lv_event_t *) {
  if (sendCommonFromInputs()) {
    showCommonDiscardOverlay(false);
    navigateTo(SCREEN_ID_MAIN);
  }
}

void onCommonDiscardCancel(lv_event_t *) {
  syncCommonInputsFromModel(DisplayComms::getCommon());
  ui.commonDirty = false;
  syncCommonSendEnablement();
  showCommonDiscardOverlay(false);
  navigateTo(SCREEN_ID_MAIN);
}

void createLeftReadouts(lv_obj_t *screen, lv_obj_t **posLabel,
                        lv_obj_t **tempLabel) {
  *posLabel = lv_label_create(screen);
  lv_obj_set_pos(*posLabel, LEFT_X, 10);
  lv_obj_set_size(*posLabel, LEFT_WIDTH, LV_SIZE_CONTENT);
  lv_obj_set_style_text_align(*posLabel, LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(*posLabel, &lv_font_montserrat_16,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(*posLabel, "-- cm3");

  *tempLabel = lv_label_create(screen);
  lv_obj_set_pos(*tempLabel, LEFT_X, 770);
  lv_obj_set_size(*tempLabel, LEFT_WIDTH, LV_SIZE_CONTENT);
  lv_obj_set_style_text_align(*tempLabel, LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(*tempLabel, &lv_font_montserrat_16,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(*tempLabel, "--.- C");
}

void createMainPanel() {
  ui.rightPanelMain = createRightPanel(objects.main);
  if (!ui.rightPanelMain) {
    return;
  }
  lv_obj_t *title = lv_label_create(ui.rightPanelMain);
  lv_obj_set_pos(title, 18, 12);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(title, lv_color_hex(0xffffffff),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(title, "Main");

  lv_obj_t *stateHeader = lv_label_create(ui.rightPanelMain);
  lv_obj_set_pos(stateHeader, 18, 60);
  lv_obj_set_style_text_font(stateHeader, &lv_font_montserrat_16,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(stateHeader, lv_color_hex(0xff9fb2c7),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(stateHeader, "Machine State");

  ui.stateValue = lv_label_create(ui.rightPanelMain);
  lv_obj_set_pos(ui.stateValue, 18, 90);
  lv_obj_set_width(ui.stateValue, RIGHT_WIDTH - 36);
  lv_label_set_long_mode(ui.stateValue, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(ui.stateValue, &lv_font_montserrat_30,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(ui.stateValue, "--");

  ui.stateAction1 = createButton(ui.rightPanelMain, "Refresh State", 18, 235,
                                 150, 52, onStateActionQueryState);
  ui.stateAction2 = createButton(ui.rightPanelMain, "Refresh Error", 182, 235,
                                 150, 52, onStateActionQueryError);

  createButton(ui.rightPanelMain, "Mould Settings", 18, 720, 150, 58,
               onNavigate,
               reinterpret_cast<void *>(
                   static_cast<intptr_t>(SCREEN_ID_MOULD_SETTINGS)));
  createButton(ui.rightPanelMain, "Common Settings", 182, 720, 150, 58,
               onNavigate,
               reinterpret_cast<void *>(
                   static_cast<intptr_t>(SCREEN_ID_COMMON_SETTINGS)));

  ui.mainErrorLabel = lv_label_create(ui.rightPanelMain);
  lv_obj_set_pos(ui.mainErrorLabel, 18, 640);
  lv_obj_set_width(ui.mainErrorLabel, RIGHT_WIDTH - 36);
  lv_label_set_long_mode(ui.mainErrorLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(ui.mainErrorLabel, &lv_font_montserrat_16,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(ui.mainErrorLabel, lv_color_hex(0xffff6b6b),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(ui.mainErrorLabel, "");
  lv_obj_add_flag(ui.mainErrorLabel, LV_OBJ_FLAG_HIDDEN);
}

void createMouldPanel() {
  ui.rightPanelMould = createRightPanel(objects.mould_settings);
  if (!ui.rightPanelMould) {
    return;
  }

  lv_obj_t *title = lv_label_create(ui.rightPanelMould);
  lv_obj_set_pos(title, 18, 12);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(title, "Mould Selection");

  ui.mouldList = lv_obj_create(ui.rightPanelMould);
  lv_obj_set_pos(ui.mouldList, 18, 54);
  lv_obj_set_size(ui.mouldList, RIGHT_WIDTH - 36, 530);
  lv_obj_set_style_bg_color(ui.mouldList, lv_color_hex(0x1a222b),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(ui.mouldList, lv_color_hex(0x3a4a5a),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(ui.mouldList, 1,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(ui.mouldList, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_scrollbar_mode(ui.mouldList, LV_SCROLLBAR_MODE_ACTIVE);

  ui.mouldNotice = lv_label_create(ui.rightPanelMould);
  lv_obj_set_pos(ui.mouldNotice, 18, 592);
  lv_obj_set_width(ui.mouldNotice, RIGHT_WIDTH - 36);
  lv_label_set_long_mode(ui.mouldNotice, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(ui.mouldNotice, lv_color_hex(0xffd6d6d6),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(ui.mouldNotice, "Select a profile.");

  ui.mouldButtonBack = createButton(
      ui.rightPanelMould, "Back", 18, 648, 96, 52, onNavigate,
      reinterpret_cast<void *>(static_cast<intptr_t>(SCREEN_ID_MAIN)));
  ui.mouldButtonSend =
      createButton(ui.rightPanelMould, "Send", 127, 648, 96, 52, onMouldSend);
  ui.mouldButtonEdit =
      createButton(ui.rightPanelMould, "Edit", 236, 648, 96, 52, onMouldEdit);
  ui.mouldButtonNew =
      createButton(ui.rightPanelMould, "New", 70, 712, 96, 52, onMouldNew);
  ui.mouldButtonDelete = createButton(ui.rightPanelMould, "Delete", 184, 712,
                                      96, 52, onMouldDelete);
}

void createMouldEditPanel() {
  ui.rightPanelMouldEdit = createRightPanel(objects.mould_settings);
  if (!ui.rightPanelMouldEdit)
    return;
  lv_obj_add_flag(ui.rightPanelMouldEdit, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *title = lv_label_create(ui.rightPanelMouldEdit);
  lv_obj_set_pos(title, 18, 12);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(title, "Edit Mould");

  ui.mouldEditScroll = lv_obj_create(ui.rightPanelMouldEdit);
  if (!ui.mouldEditScroll) {
    Serial.println("PRD_UI: FAILED to create mouldEditScroll");
    return;
  }
  lv_obj_set_pos(ui.mouldEditScroll, 18, 54);
  lv_obj_set_size(ui.mouldEditScroll, RIGHT_WIDTH - 36, 598);
  lv_obj_set_style_bg_color(ui.mouldEditScroll, lv_color_hex(0x1a222b),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(ui.mouldEditScroll, lv_color_hex(0x3a4a5a),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(ui.mouldEditScroll, 1,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(ui.mouldEditScroll, 6,
                           LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_scrollbar_mode(ui.mouldEditScroll, LV_SCROLLBAR_MODE_ACTIVE);

  int y = 6;
  for (int i = 0; i < MOULD_FIELD_COUNT; i++) {
    lv_obj_t *label = lv_label_create(ui.mouldEditScroll);
    if (!label) {
      Serial.printf("PRD_UI: FAILED to create label for mould field %d\n", i);
      y += 42;
      continue;
    }
    lv_obj_set_pos(label, 4, y + 8);
    lv_obj_set_size(label, 160, LV_SIZE_CONTENT);
    lv_label_set_text(label, MOULD_FIELD_NAMES[i]);

    lv_obj_t *input = nullptr;
    bool isDropdown = (i == 13);

    if (isDropdown) {
      input = lv_dropdown_create(ui.mouldEditScroll);
      if (input) {
        lv_dropdown_set_options(input, "2D\n3D");
        lv_obj_set_size(input, 130, 42);
      }
    } else {
      input = lv_textarea_create(ui.mouldEditScroll);
      if (input) {
        lv_textarea_set_one_line(input, true);
        lv_textarea_set_max_length(input, (i == 0) ? 20 : 10);
        const char *accepted =
            (MOULD_FIELD_TYPE[i] == 0) ? nullptr : "0123456789.-";
        if (accepted)
          lv_textarea_set_accepted_chars(input, accepted);
        lv_textarea_set_text(input, (MOULD_FIELD_TYPE[i] == 0) ? "" : "0");
        lv_obj_set_style_text_align(input, LV_TEXT_ALIGN_RIGHT,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_size(input, 130, 34);
      }
    }

    if (!input) {
      Serial.printf("PRD_UI: FAILED to create input for field %d\n", i);
      y += 42;
      continue;
    }
    lv_obj_set_pos(input, 168, y);

    lv_obj_set_user_data(input,
                         reinterpret_cast<void *>(static_cast<intptr_t>(i)));

    // Only textareas should trigger the on-screen keyboard
    if (!isDropdown) {
      lv_obj_add_event_cb(input, onMouldEditInputFocus, LV_EVENT_CLICKED,
                          nullptr);
    }

    lv_obj_add_event_cb(input, onMouldEditFieldChanged, LV_EVENT_VALUE_CHANGED,
                        nullptr);

    if (!isDropdown) {
      lv_obj_add_event_cb(input, onMouldEditFieldChanged, LV_EVENT_READY,
                          nullptr);
      lv_obj_add_event_cb(input, onMouldEditFieldChanged, LV_EVENT_CANCEL,
                          nullptr);
    }

    ui.mouldEditInputs[i] = input;
    Serial.printf("PRD_UI: createMouldEditPanel stored input %d at %p\n", i,
                  input);
    y += 42;
  }

  ui.mouldButtonSave = createButton(ui.rightPanelMouldEdit, "Save", 18, 720,
                                    150, 58, onMouldEditSave);
  createButton(ui.rightPanelMouldEdit, "Cancel", 182, 720, 150, 58,
               onMouldEditCancel);
  Serial.println("PRD_UI: createMouldEditPanel complete.");
}

void createCommonPanel() {
  Serial.println("PRD_UI: createCommonPanel start");
  ui.rightPanelCommon = createRightPanel(objects.common_settings);
  if (!ui.rightPanelCommon) {
    Serial.println("PRD_UI: createCommonPanel failed to create RightPanel");
    return;
  }

  lv_obj_t *title = lv_label_create(ui.rightPanelCommon);
  lv_obj_set_pos(title, 18, 12);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(title, "Common Settings");

  ui.commonScroll = lv_obj_create(ui.rightPanelCommon);
  lv_obj_set_pos(ui.commonScroll, 18, 54);
  lv_obj_set_size(ui.commonScroll, RIGHT_WIDTH - 36, 598);
  lv_obj_set_style_bg_color(ui.commonScroll, lv_color_hex(0x1a222b),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(ui.commonScroll, lv_color_hex(0x3a4a5a),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(ui.commonScroll, 1,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(ui.commonScroll, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_scrollbar_mode(ui.commonScroll, LV_SCROLLBAR_MODE_ACTIVE);

  int y = 6;
  for (int i = 0; i < COMMON_FIELD_COUNT; i++) {
    lv_obj_t *label = lv_label_create(ui.commonScroll);
    lv_obj_set_pos(label, 4, y + 8);
    lv_obj_set_size(label, 160, LV_SIZE_CONTENT);
    lv_label_set_text(label, COMMON_FIELD_NAMES[i]);

    lv_obj_t *input = lv_textarea_create(ui.commonScroll);
    lv_obj_set_pos(input, 168, y);
    lv_obj_set_size(input, 130, 34);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, 20);
    lv_textarea_set_accepted_chars(
        input, COMMON_FIELD_IS_INTEGER[i] ? "0123456789" : "0123456789.-");
    lv_textarea_set_text(input, "0");
    lv_obj_set_style_text_align(input, LV_TEXT_ALIGN_RIGHT,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(input, onCommonInputFocus, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(input, onCommonInputFocus, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(input, onCommonInputFocus, LV_EVENT_DEFOCUSED, nullptr);
    lv_obj_add_event_cb(input, onCommonInputFocus, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(input, onCommonInputFocus, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_event_cb(input, onCommonFieldChanged, LV_EVENT_VALUE_CHANGED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_add_event_cb(input, onCommonFieldChanged, LV_EVENT_READY,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));

    ui.commonInputs[i] = input;
    y += 42;
  }

  ui.commonNotice = lv_label_create(ui.rightPanelCommon);
  lv_obj_set_pos(ui.commonNotice, 18, 660);
  lv_obj_set_width(ui.commonNotice, RIGHT_WIDTH - 36);
  lv_label_set_long_mode(ui.commonNotice, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(ui.commonNotice, lv_color_hex(0xffd6d6d6),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(ui.commonNotice, "Edit and press Send.");

  ui.commonButtonBack =
      createButton(ui.rightPanelCommon, "Back", 18, 720, 150, 58, onCommonBack);
  ui.commonButtonSend = createButton(ui.rightPanelCommon, "Send", 182, 720, 150,
                                     58, onCommonSend);

  ui.commonDiscardOverlay = nullptr; // Feature still disabled
  showCommonDiscardOverlay(false);
  Serial.println("PRD_UI: createCommonPanel end");
} // namespace

void updateStateWidgets(const DisplayComms::Status &status) {
  const char *stateText = status.state[0] != '\0' ? status.state : "--";
  setLabelTextIfChanged(ui.stateValue, stateText);

  bool hasState = status.state[0] != '\0';
  if (hasState) {
    if (ui.stateAction1) {
      lv_obj_clear_flag(ui.stateAction1, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui.stateAction2) {
      lv_obj_clear_flag(ui.stateAction2, LV_OBJ_FLAG_HIDDEN);
    }
  } else {
    if (ui.stateAction1) {
      lv_obj_add_flag(ui.stateAction1, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui.stateAction2) {
      lv_obj_add_flag(ui.stateAction2, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void updateMouldListFromComms(const DisplayComms::MouldParams &mould) {
  if (mould.name[0] == '\0') {
    return;
  }

  if (ui.mouldProfileCount == 0) {
    ui.mouldProfileCount = 1;
    ui.mouldProfiles[0] = mould;
    strncpy(ui.lastMouldName, mould.name, sizeof(ui.lastMouldName) - 1);
    ui.lastMouldName[sizeof(ui.lastMouldName) - 1] = '\0';
    rebuildMouldList();
    return;
  }

  if (strcmp(ui.lastMouldName, mould.name) != 0) {
    ui.mouldProfiles[0] = mould;
    strncpy(ui.lastMouldName, mould.name, sizeof(ui.lastMouldName) - 1);
    ui.lastMouldName[sizeof(ui.lastMouldName) - 1] = '\0';
    rebuildMouldList();
    return;
  }

  // Keep active profile data fresh even when the name doesn't change.
  ui.mouldProfiles[0] = mould;
}

} // namespace

namespace PrdUi {

void init() {
  if (ui.initialized) {
    return;
  }

  if (!isObjReady(objects.main) || !isObjReady(objects.mould_settings) ||
      !isObjReady(objects.common_settings)) {
    Serial.printf("PRD_UI: screen validity m=%d ms=%d cs=%d\n",
                  static_cast<int>(isObjReady(objects.main)),
                  static_cast<int>(isObjReady(objects.mould_settings)),
                  static_cast<int>(isObjReady(objects.common_settings)));
    return;
  }

  // Load persisted moulds
  Storage::init();
  Storage::loadMoulds(ui.mouldProfiles, ui.mouldProfileCount,
                      MAX_MOULD_PROFILES);

  // Pre-initialize shared keyboard on top layer
  getSharedKeyboard(nullptr);

  // Disable screen-level scrolling to prevent left-side plunger from jumping
  lv_obj_remove_flag(objects.main, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(objects.mould_settings, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(objects.common_settings, LV_OBJ_FLAG_SCROLLABLE);

  Serial.println("PRD_UI: init hideLegacyWidgets");
  hideLegacyWidgets();

  // Panels and readouts are now created ON DEMAND in tick()

  if (ui.mouldProfileCount == 0) {
    ui.mouldProfileCount = 1;
    strncpy(ui.mouldProfiles[0].name, "Awaiting QUERY_MOULD",
            sizeof(ui.mouldProfiles[0].name) - 1);
    ui.mouldProfiles[0].name[sizeof(ui.mouldProfiles[0].name) - 1] = '\0';
  }

  ui.initialized = true;
  Serial.println("PRD_UI: init basic state complete");
}

void updateRefillBlocks(const DisplayComms::Status &status) {
  float currentPos = status.encoderTurns;
  const char *state = status.state;

  // 1. Start Sequence: Entering REFILL
  if (strcmp(state, "REFILL") == 0) {
    if (!ui.refillSequenceActive) {
      ui.refillSequenceActive = true;
      // Capture start position. During Refill, plunger moves UP (turns
      // decrease).
      ui.startRefillPos = currentPos;
      Serial.printf("PRD_UI: Refill Sequence Started at %.2f\n", currentPos);
    }
    ui.isRefilling = true;
  } else {
    ui.isRefilling = false;
  }

  // 2. End Sequence: Entering READY_TO_INJECT
  if (strcmp(state, "READY_TO_INJECT") == 0 &&
      strcmp(ui.lastState, "READY_TO_INJECT") != 0 && ui.refillSequenceActive) {

    // Calculate total geometric space between Bottom (360.5) and Plunger
    // (currentPos)
    float spaceBelowPlunger = 360.5f - currentPos;
    if (spaceBelowPlunger < 0)
      spaceBelowPlunger = 0;

    // Calculate how much volume is already occupied by existing blocks
    float existingVolume = 0.0f;
    for (int i = 0; i < ui.blockCount; i++) {
      existingVolume += ui.refillBlocks[i].volume;
    }

    // New block fills whatever physical space remains
    float delta = spaceBelowPlunger - existingVolume;

    // Only add positive blocks (real refills)
    if (delta > 0.5f) {
      if (ui.blockCount < 16) {
        ui.refillBlocks[ui.blockCount] = RefillBlock(delta, millis(), true);
        ui.blockCount++;
        Serial.printf("PRD_UI: Block added. Vol: %.2f. SpaceBelow: %.2f "
                      "Existing: %.2f Cur: %.2f. "
                      "Count: %d\n",
                      delta, spaceBelowPlunger, existingVolume, currentPos,
                      ui.blockCount);
      } else {
        Serial.println("PRD_UI: Block limit reached!");
      }
    } else {
      Serial.printf("PRD_UI: Ignored invalid block. Vol: %.2f. SpaceBelow: "
                    "%.2f Existing: %.2f "
                    "Cur: %.2f\n",
                    delta, spaceBelowPlunger, existingVolume, currentPos);
    }
    ui.refillSequenceActive = false; // Sequence complete
  }

  // 3. Consumption Logic (Injection)
  // Injection moves Plunger DOWN, so turns INCREASE.
  if (currentPos > ui.lastFramePos) {
    float consumedCm3 = currentPos - ui.lastFramePos;

    // Ignore small jitters or massive jumps (e.g. wrapping)
    if (consumedCm3 > 0.001f && consumedCm3 < 100.0f) {
      while (consumedCm3 > 0.001f && ui.blockCount > 0) {
        if (ui.refillBlocks[0].volume > consumedCm3) {
          ui.refillBlocks[0].volume -= consumedCm3;
          consumedCm3 = 0;
        } else {
          consumedCm3 -= ui.refillBlocks[0].volume;
          // Shift blocks
          for (int i = 0; i < ui.blockCount - 1; i++) {
            ui.refillBlocks[i] = ui.refillBlocks[i + 1];
          }
          ui.blockCount--;
          ui.refillBlocks[ui.blockCount] = RefillBlock();
        }
      }
    }
  }

  ui.lastFramePos = currentPos;
  strncpy(ui.lastState, state, sizeof(ui.lastState) - 1);
}
void updatePlungerPosition(float turns) {
  // Plunger/Rod Movement Logic
  // The plunger object (rod) sits on top of the barrel interior.
  // When empty (turns=0), it should be at Y=0 (fully covering the barrel).
  // When full (turns=MAX), it should be at Y=-Height (fully retracted).
  // We use the same scale as the blocks to ensure the tip matches the stack
  // height.

  // Visual Calibration:
  // User reports a gap (plunger too high relative to blocks).
  // User reports blocks centered too far right.

  // Exact linear mapping based directly on physical encoder turns.
  // 360.5 turns = Tip Bottom at Y=791 (Lifted 2px off UI edge).
  // 22.53 turns = Tip Top exactly at Y=0 (Edge of the gray square).
  // Total Tip Travel: 711 - 0 = 711px. (Since Tip Bottom 791 - Tip 80 = 711 Tip
  // Top). Scale factor: 711px / (360.5 - 22.53) turns = 2.1037 px/turn
  static const float MAX_TURNS = 360.5f;
  static const float PX_PER_TURN = 711.0f / (360.5f - 22.53f); // ~2.1037f

  // The plunger tip is fixed at Y=700 inside the plunger container.
  static const int TIP_ANCHOR_Y = 700;

  float clampedTurns = turns;
  if (clampedTurns < 0.0f)
    clampedTurns = 0.0f;
  if (clampedTurns > MAX_TURNS)
    clampedTurns = MAX_TURNS;

  // Formula: TipY = (turns - 22.53) * scale. Plunger Y = TipY - Anchor.
  float targetTipY = (clampedTurns - 22.53f) * PX_PER_TURN;
  int yOffset = static_cast<int>(targetTipY) - TIP_ANCHOR_Y;

  // Safety Clamping
  if (yOffset < -750)
    yOffset = -750;
  if (yOffset > 13)
    yOffset = 13;

  // Apply to all plunger objects
  if (isObjReady(objects.plunger_tip__plunger))
    lv_obj_set_y(objects.plunger_tip__plunger, yOffset);
  if (isObjReady(objects.obj0__plunger))
    lv_obj_set_y(objects.obj0__plunger, yOffset);
  if (isObjReady(objects.obj2__plunger))
    lv_obj_set_y(objects.obj2__plunger, yOffset);
  if (isObjReady(objects.obj5__plunger))
    lv_obj_set_y(objects.obj5__plunger, yOffset);
}

void renderRefillBlocksForBands(lv_obj_t **bands) {
  if (!bands)
    return;

  // Using 2.1037f to perfectly match Plunger's pixels-per-turn mapping.
  static const float PX_PER_TURN = 711.0f / (360.5f - 22.53f);

  if (ui.blockCount > 0) {
    Serial.printf("PRD_UI: Render Blocks count=%d. PX_PER_TURN=%.2f\n",
                  ui.blockCount, PX_PER_TURN);
  }

  int y = 791; // Bottom of barrel is 791px (lifted from edge).
  uint32_t now = millis();

  for (int i = 0; i < 16; i++) {
    lv_obj_t *band = bands[i];
    if (!isObjReady(band))
      continue;

    if (i < ui.blockCount) {
      int h = static_cast<int>(ui.refillBlocks[i].volume * PX_PER_TURN);
      if (h < 1)
        h = 1;
      y -= h;

      // Serial.printf("PRD_UI: Band %d. Vol=%.2f H=%d Y=%d\n", i,
      // ui.refillBlocks[i].volume, h, y);

      lv_obj_set_size(band, 80, h);
      // Shift left by setting X to -8 relative to container
      // Container X=10. X=-8 -> Abs X=2. Plunger Abs X=20...?
      // User requested total 8px left shift relative to original.
      lv_obj_set_pos(band, -8, y);
      lv_obj_remove_flag(band, LV_OBJ_FLAG_HIDDEN);

      // Color based on age (millis)
      // 0-10s: Blue, 10-30s: Orange, 30s+: Red
      uint32_t age = now - ui.refillBlocks[i].addedMs;
      lv_color_t color;
      if (age < 10000)
        color = lv_color_hex(0x3498db); // Blue
      else if (age < 30000)
        color = lv_color_hex(0xe67e22); // Orange
      else
        color = lv_color_hex(0xe74c3c); // Red
      lv_obj_set_style_bg_color(band, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
      lv_obj_add_flag(band, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void renderAllPlungers() {
  // We need to gather the bands for each screen.
  // The objects struct has them in groups of 16.
  // indices based on screens.c:
  // Main/plunger_tip: 4 + 2 = 6
  // Main/obj0: 27 + 2 = 29
  // Mould/obj2: 56 + 2 = 58
  // Common/obj5: 102 + 2 = 104

  lv_obj_t **allObjects = (lv_obj_t **)&objects;

  renderRefillBlocksForBands(&allObjects[6]);
  renderRefillBlocksForBands(&allObjects[29]);
  renderRefillBlocksForBands(&allObjects[58]);
  renderRefillBlocksForBands(&allObjects[104]);
}

void handleDebugCommand(const char *cmd) {
  if (!cmd)
    return;
  Serial.printf("PRD_UI: Debug Command received: %s\n", cmd);

  // Simple MOCK parser: MOCK|STATE|Name or MOCK|POS|Value
  char buf[64];
  strncpy(buf, cmd, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *part1 = strtok(buf, "|");
  if (part1 && strcmp(part1, "MOCK") == 0) {
    char *part2 = strtok(nullptr, "|");
    char *part3 = strtok(nullptr, "|");
    if (part2 && part3) {
      DisplayComms::Status mockStatus = DisplayComms::getStatus();
      if (strcmp(part2, "STATE") == 0) {
        ui.mockEnabled = true;
        strncpy(ui.mockState, part3, sizeof(ui.mockState) - 1);
        ui.mockState[sizeof(ui.mockState) - 1] = '\0';
      } else if (strcmp(part2, "POS") == 0) {
        ui.mockEnabled = true;
        ui.mockPos = atof(part3);
      } else if (strcmp(part2, "OFF") == 0) {
        ui.mockEnabled = false;
      }
    }
  }
}

void tick() {
  if (!ui.initialized) {
    return;
  }

  lv_obj_t *active = lv_screen_active();

  // lifecycle Management: Purge panels when LEAVING a screen to save RAM
  // Move this to the TOP to free memory before we try to build the next
  // screen
  if (ui.lastActiveScreen && active != ui.lastActiveScreen) {
    if (ui.lastActiveScreen == objects.mould_settings)
      purgeMouldPanels();
    else if (ui.lastActiveScreen == objects.common_settings)
      purgeCommonPanel();
    else if (ui.lastActiveScreen == objects.main)
      purgeMainPanel();
    uiYield(); // Give some time for LVGL to process the async deletes
    ui.lastActiveScreen = active;
    return; // Wait for next tick to build the new screen (memory safety)
  }
  ui.lastActiveScreen = active;

  // Lifecycle Management: Construct panels ONLY when visible
  if (active == objects.main) {
    if (!isObjReady(ui.rightPanelMain)) {
      Serial.println("PRD_UI: Building Main Panel on demand.");
      createLeftReadouts(objects.main, &ui.posLabelMain, &ui.tempLabelMain);
      createMainPanel();
      ui.lastMainScreen = objects.main;
    }
  } else if (active == objects.mould_settings) {
    if (!isObjReady(ui.rightPanelMould)) {
      Serial.println("PRD_UI: Building Mould Panel on demand.");
      createLeftReadouts(objects.mould_settings, &ui.posLabelMould,
                         &ui.tempLabelMould);
      createMouldPanel();
      rebuildMouldList();
      ui.lastMouldScreen = objects.mould_settings;
      uiYield();
    }
  } else if (active == objects.common_settings) {
    if (!isObjReady(ui.rightPanelCommon)) {
      Serial.println("PRD_UI: Building Common Panel on demand.");
      createLeftReadouts(objects.common_settings, &ui.posLabelCommon,
                         &ui.tempLabelCommon);
      createCommonPanel();
      ui.lastCommonScreen = objects.common_settings;
      uiYield();
    }
  }
  ui.lastActiveScreen = active;

  // Handle pointer invalidation (extra safety)
  if (!isObjReady(ui.rightPanelMould)) {
    ui.rightPanelMould = nullptr;
    ui.mouldList = nullptr;
  }
  if (!isObjReady(ui.rightPanelCommon)) {
    ui.rightPanelCommon = nullptr;
  }
  if (!isObjReady(ui.rightPanelMain)) {
    ui.rightPanelMain = nullptr;
  }

  const DisplayComms::Status &realStatus = DisplayComms::getStatus();
  DisplayComms::Status status = realStatus;
  if (ui.mockEnabled) {
    status.encoderTurns = ui.mockPos;
    strncpy(status.state, ui.mockState, sizeof(status.state) - 1);
    status.state[sizeof(status.state) - 1] = '\0';
  }

  const DisplayComms::MouldParams &mould = DisplayComms::getMould();
  const DisplayComms::CommonParams &common = DisplayComms::getCommon();

  updateLeftReadouts(status);
  updateRefillBlocks(status);
  updatePlungerPosition(status.encoderTurns);
  updateStateWidgets(status);
  updateErrorFrames(status);
  renderAllPlungers();
  if (isObjReady(ui.mouldList) &&
      !lv_obj_has_flag(ui.mouldList, LV_OBJ_FLAG_HIDDEN)) {
    updateMouldListFromComms(mould);
  }
  syncMouldSendEditEnablement();

  if (isObjReady(ui.rightPanelCommon) &&
      !lv_obj_has_flag(ui.rightPanelCommon, LV_OBJ_FLAG_HIDDEN)) {
    if (!ui.commonDirty) {
      syncCommonInputsFromModel(common);
    }
    // Only sync button enablement when panel is actually visible to save CPU
    syncCommonSendEnablement();
  }
}

bool isInitialized() { return ui.initialized; }

} // namespace PrdUi

void handleDebugCommand(const char *cmd) {
  if (!cmd)
    return;
  Serial.printf("PRD_UI: Debug Command received: %s\n", cmd);

  // Simple MOCK parser: MOCK|STATE|Name or MOCK|POS|Value
  char buf[64];
  strncpy(buf, cmd, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *part1 = strtok(buf, "|");
  if (part1 && strcmp(part1, "MOCK") == 0) {
    char *part2 = strtok(nullptr, "|");
    char *part3 = strtok(nullptr, "|");
    if (part2 && part3) {
      if (strcmp(part2, "STATE") == 0) {
        ui.mockEnabled = true;
        strncpy(ui.mockState, part3, sizeof(ui.mockState) - 1);
        ui.mockState[sizeof(ui.mockState) - 1] = '\0';
      } else if (strcmp(part2, "POS") == 0) {
        ui.mockEnabled = true;
        ui.mockPos = atof(part3);
      } else if (strcmp(part2, "OFF") == 0) {
        ui.mockEnabled = false;
      }
    }
  }
}
