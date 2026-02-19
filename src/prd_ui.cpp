#include "prd_ui.h"

#include "display_comms.h"
#include "storage.h"
#include "ui/eez-flow.h"
#include "ui/screens.h"

#include <Arduino.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr lv_coord_t LEFT_X = 8;
// ... (rest of the file constants)

// ...

//...
constexpr lv_coord_t LEFT_WIDTH = 114;
constexpr lv_coord_t RIGHT_X = 130;
constexpr lv_coord_t RIGHT_WIDTH = 350;
constexpr lv_coord_t SCREEN_HEIGHT = 800;
constexpr int MAX_MOULD_PROFILES = 8;
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

// ... (Common fields)

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

struct UiState {
  bool initialized = false;

  lv_obj_t *rightPanelMain = nullptr;
  lv_obj_t *rightPanelMould = nullptr;
  lv_obj_t *rightPanelMouldEdit = nullptr; // New
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
  lv_obj_t *mouldButtonNew = nullptr;
  lv_obj_t *mouldButtonDelete = nullptr;
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
  lv_obj_t *commonKeyboard = nullptr;
  lv_obj_t *commonInputs[COMMON_FIELD_COUNT] = {};
  lv_obj_t *commonDiscardOverlay = nullptr;
  bool commonDirty = false;
  bool suppressCommonEvents = false;

  // Mould Edit State
  lv_obj_t *mouldEditScroll = nullptr;
  lv_obj_t *mouldEditInputs[MOULD_FIELD_COUNT] = {};
  lv_obj_t *mouldEditKeyboard =
      nullptr; // Separate keyboard for safety/simplicity
  DisplayComms::MouldParams editMouldSnapshot = {};
  bool mouldEditDirty = false;

  char lastMouldName[32] = {0};
};

UiState ui;

inline bool isObjReady(lv_obj_t *obj) { return obj && lv_obj_is_valid(obj); }

inline void uiYield() { delay(0); }

inline void hideIfPresent(lv_obj_t *obj) {
  if (isObjReady(obj)) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

float turnsToCm3(float turns) {
  static const float TURNS_PER_CM3 = 0.99925f;
  if (TURNS_PER_CM3 == 0.0f) {
    return 0.0f;
  }
  return turns / TURNS_PER_CM3;
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
  lv_obj_t *button = lv_button_create(parent);
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
  lv_label_set_text(label, text);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(label);
  return button;
}

void hideLegacyWidgets() {
  // Keep main screen from the known-good demo for now.
  // Replace only Mould/Common in this step.
  hideIfPresent(objects.obj3);
  hideIfPresent(objects.obj4);
  hideIfPresent(objects.button_to_mould_settings_1);

  // Common settings
  hideIfPresent(objects.obj6);
  hideIfPresent(objects.button_to_mould_settings_2);
}

lv_obj_t *createRightPanel(lv_obj_t *screen) {
  if (!isObjReady(screen)) {
    Serial.println("PRD_UI: createRightPanel skipped (invalid screen)");
    return nullptr;
  }
  lv_obj_t *panel = lv_obj_create(screen);
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

  snprintf(posText, sizeof(posText), "%.2f cm3",
           turnsToCm3(status.encoderTurns));
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
  eez_flow_set_screen(static_cast<int16_t>(target), LV_SCR_LOAD_ANIM_NONE, 0,
                      0);
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
      lv_obj_set_style_bg_color(ui.mouldProfileButtons[i],
                                lv_color_hex(0x26303a),
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
  setButtonEnabled(ui.mouldButtonDelete, hasSelection);
}

void rebuildMouldList() {
  if (!ui.mouldList) {
    return;
  }

  uint32_t childCount = lv_obj_get_child_count(ui.mouldList);
  while (childCount > 0) {
    lv_obj_t *child = lv_obj_get_child(ui.mouldList, childCount - 1);
    lv_obj_delete(child);
    childCount--;
  }

  for (int i = 0; i < MAX_MOULD_PROFILES; i++) {
    ui.mouldProfileButtons[i] = nullptr;
  }

  int y = 8;
  for (int i = 0; i < ui.mouldProfileCount; i++) {
    const char *name = ui.mouldProfiles[i].name[0] != '\0'
                           ? ui.mouldProfiles[i].name
                           : "Unnamed Mould";
    lv_obj_t *button =
        createButton(ui.mouldList, name, 8, y, 286, 46, onMouldProfileSelect,
                     reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_set_style_bg_color(button, lv_color_hex(0x26303a),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, lv_color_hex(0x41505f),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    ui.mouldProfileButtons[i] = button;
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
  if (!ui.mouldEditKeyboard || !textarea) {
    return;
  }
  // Ensure keyboard is on screen parent for visibility
  lv_obj_set_parent(ui.mouldEditKeyboard, objects.mould_settings);
  lv_keyboard_set_textarea(ui.mouldEditKeyboard, textarea);

  // Check if text input (Name/Mode) or Number
  // Simple heuristic: Name is index 0, Mode is index 13
  // But wait, we don't have index here easily unless we store it.
  // We can check userdata.
  intptr_t index = reinterpret_cast<intptr_t>(lv_obj_get_user_data(textarea));
  if (MOULD_FIELD_TYPE[index] == 0) {
    lv_keyboard_set_mode(ui.mouldEditKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
  } else {
    lv_keyboard_set_mode(ui.mouldEditKeyboard, LV_KEYBOARD_MODE_NUMBER);
  }

  lv_obj_clear_flag(ui.mouldEditKeyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(ui.mouldEditKeyboard);
  lv_obj_scroll_to_view_recursive(textarea, LV_ANIM_ON);
}

void hideMouldEditKeyboard() {
  if (ui.mouldEditKeyboard) {
    lv_obj_add_flag(ui.mouldEditKeyboard, LV_OBJ_FLAG_HIDDEN);
  }
}

void onMouldEditInputFocus(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t *target = lv_event_get_target_obj(event);

  if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
    showMouldEditKeyboard(target);
    return;
  }

  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL ||
      code == LV_EVENT_DEFOCUSED) {
    hideMouldEditKeyboard();
  }
}

void onMouldEditFieldChanged(lv_event_t *event) { ui.mouldEditDirty = true; }

void onMouldEditCancel(lv_event_t *) {
  hideMouldEditKeyboard();
  lv_obj_add_flag(ui.rightPanelMouldEdit, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui.rightPanelMould, LV_OBJ_FLAG_HIDDEN);
}

void onMouldEditSave(lv_event_t *) {
  hideMouldEditKeyboard();

  // Save inputs back to snapshot -> ui.mouldProfiles
  DisplayComms::MouldParams &p = ui.mouldProfiles[ui.selectedMould];

  for (int i = 0; i < MOULD_FIELD_COUNT; i++) {
    if (!ui.mouldEditInputs[i])
      continue;
    const char *txt = lv_textarea_get_text(ui.mouldEditInputs[i]);
    if (!txt)
      continue;

    if (MOULD_FIELD_TYPE[i] == 0) { // String
      if (i == 0) {
        strncpy(p.name, txt, sizeof(p.name) - 1);
        p.name[sizeof(p.name) - 1] = 0;
      }
      if (i == 13) {
        strncpy(p.mode, txt, sizeof(p.mode) - 1);
        p.mode[sizeof(p.mode) - 1] = 0;
      }
    } else { // Float
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

  Storage::saveMoulds(ui.mouldProfiles, ui.mouldProfileCount);
  rebuildMouldList(); // Refresh list names

  // If this is the active mould, update controller?
  // User must click "Send" explicitly from the list to update controller.

  lv_obj_add_flag(ui.rightPanelMouldEdit, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui.rightPanelMould, LV_OBJ_FLAG_HIDDEN);
  setNotice(ui.mouldNotice, "Profile saved.", lv_color_hex(0xff9be7a5));
}

void onMouldEdit(lv_event_t *) {
  if (ui.selectedMould < 0 || ui.selectedMould >= ui.mouldProfileCount) {
    setNotice(ui.mouldNotice, "Select a mould first.", lv_color_hex(0xfff0a0));
    return;
  }

  // Populate fields
  DisplayComms::MouldParams &p = ui.mouldProfiles[ui.selectedMould];
  for (int i = 0; i < MOULD_FIELD_COUNT; i++) {
    if (!ui.mouldEditInputs[i])
      continue;
    char buf[32];
    if (MOULD_FIELD_TYPE[i] == 0) { // String
      const char *s = (i == 0) ? p.name : p.mode;
      strncpy(buf, s, sizeof(buf));
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
    lv_textarea_set_text(ui.mouldEditInputs[i], buf);
  }

  // Show panel
  lv_obj_add_flag(ui.rightPanelMould, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui.rightPanelMouldEdit, LV_OBJ_FLAG_HIDDEN);
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

void onMouldDelete(lv_event_t *) {
  if (ui.selectedMould < 0 || ui.selectedMould >= ui.mouldProfileCount) {
    setNotice(ui.mouldNotice, "Select a mould first.", lv_color_hex(0xfff0a0));
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
  setNotice(ui.mouldNotice, "Profile deleted.", lv_color_hex(0xfff0a0));
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
  setButtonEnabled(ui.commonButtonSend,
                   ui.commonDirty && DisplayComms::isSafeForUpdate());
}

void hideCommonKeyboard() {
  if (!ui.commonKeyboard) {
    return;
  }
  lv_keyboard_set_textarea(ui.commonKeyboard, nullptr);
  lv_obj_add_flag(ui.commonKeyboard, LV_OBJ_FLAG_HIDDEN);
}

void showCommonKeyboard(lv_obj_t *textarea) {
  if (!ui.commonKeyboard || !textarea) {
    return;
  }
  lv_keyboard_set_textarea(ui.commonKeyboard, textarea);
  lv_keyboard_set_mode(ui.commonKeyboard, LV_KEYBOARD_MODE_NUMBER);
  lv_obj_clear_flag(ui.commonKeyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(ui.commonKeyboard);
  lv_obj_scroll_to_view_recursive(textarea, LV_ANIM_ON);
}

void onCommonInputFocus(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t *target = lv_event_get_target_obj(event);

  if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
    showCommonKeyboard(target);
    return;
  }

  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL ||
      code == LV_EVENT_DEFOCUSED) {
    hideCommonKeyboard();
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
    hideCommonKeyboard();
    lv_obj_clear_flag(ui.commonDiscardOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.commonDiscardOverlay);
  } else {
    lv_obj_add_flag(ui.commonDiscardOverlay, LV_OBJ_FLAG_HIDDEN);
  }
}

void onCommonBack(lv_event_t *) {
  hideCommonKeyboard();
  if (ui.commonDirty) {
    showCommonDiscardOverlay(true);
    return;
  }
  eez_flow_set_screen(SCREEN_ID_MAIN, LV_SCR_LOAD_ANIM_NONE, 0, 0);
}

void onCommonDiscardSend(lv_event_t *) {
  if (sendCommonFromInputs()) {
    hideCommonKeyboard();
    showCommonDiscardOverlay(false);
    eez_flow_set_screen(SCREEN_ID_MAIN, LV_SCR_LOAD_ANIM_NONE, 0, 0);
  }
}

void onCommonDiscardCancel(lv_event_t *) {
  hideCommonKeyboard();
  syncCommonInputsFromModel(DisplayComms::getCommon());
  ui.commonDirty = false;
  syncCommonSendEnablement();
  showCommonDiscardOverlay(false);
  eez_flow_set_screen(SCREEN_ID_MAIN, LV_SCR_LOAD_ANIM_NONE, 0, 0);
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
    lv_obj_set_pos(label, 4, y + 8);
    lv_obj_set_size(label, 160, LV_SIZE_CONTENT);
    lv_label_set_text(label, MOULD_FIELD_NAMES[i]);

    lv_obj_t *input = lv_textarea_create(ui.mouldEditScroll);
    lv_obj_set_pos(input, 168, y);
    lv_obj_set_size(input, 130, 34);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, (i == 0) ? 20 : 10);

    const char *accepted =
        (MOULD_FIELD_TYPE[i] == 0) ? nullptr : "0123456789.-";
    if (accepted)
      lv_textarea_set_accepted_chars(input, accepted);

    lv_textarea_set_text(input, (MOULD_FIELD_TYPE[i] == 0) ? "" : "0");
    lv_obj_set_style_text_align(input, LV_TEXT_ALIGN_RIGHT,
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_user_data(input,
                         reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_add_event_cb(input, onMouldEditInputFocus, LV_EVENT_CLICKED,
                        nullptr);
    lv_obj_add_event_cb(input, onMouldEditInputFocus, LV_EVENT_FOCUSED,
                        nullptr);
    lv_obj_add_event_cb(input, onMouldEditInputFocus, LV_EVENT_DEFOCUSED,
                        nullptr);
    lv_obj_add_event_cb(input, onMouldEditInputFocus, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(input, onMouldEditInputFocus, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_event_cb(input, onMouldEditFieldChanged, LV_EVENT_VALUE_CHANGED,
                        nullptr);

    ui.mouldEditInputs[i] = input;
    y += 42;
  }

  createButton(ui.rightPanelMouldEdit, "Save", 18, 720, 150, 58,
               onMouldEditSave);
  createButton(ui.rightPanelMouldEdit, "Cancel", 182, 720, 150, 58,
               onMouldEditCancel);

  ui.mouldEditKeyboard = lv_keyboard_create(ui.rightPanelMouldEdit);
  lv_obj_set_pos(ui.mouldEditKeyboard, 0, SCREEN_HEIGHT - 250);
  lv_obj_set_size(ui.mouldEditKeyboard, RIGHT_WIDTH, 250);
  lv_keyboard_set_mode(ui.mouldEditKeyboard, LV_KEYBOARD_MODE_NUMBER);
  lv_keyboard_set_textarea(ui.mouldEditKeyboard, nullptr);
  lv_obj_add_flag(ui.mouldEditKeyboard, LV_OBJ_FLAG_HIDDEN);
}

void createCommonPanel() {
  ui.rightPanelCommon = createRightPanel(objects.common_settings);
  if (!ui.rightPanelCommon) {
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
    if ((i & 1) == 1) {
      uiYield();
    }
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

  ui.commonKeyboard = lv_keyboard_create(ui.rightPanelCommon);
  lv_obj_set_pos(ui.commonKeyboard, 0, SCREEN_HEIGHT - 250);
  lv_obj_set_size(ui.commonKeyboard, RIGHT_WIDTH, 250);
  lv_keyboard_set_mode(ui.commonKeyboard, LV_KEYBOARD_MODE_NUMBER);
  lv_keyboard_set_textarea(ui.commonKeyboard, nullptr);
  lv_obj_add_flag(ui.commonKeyboard, LV_OBJ_FLAG_HIDDEN);

  ui.commonDiscardOverlay = lv_obj_create(ui.rightPanelCommon);
  lv_obj_set_pos(ui.commonDiscardOverlay, 0, 0);
  lv_obj_set_size(ui.commonDiscardOverlay, RIGHT_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_style_bg_color(ui.commonDiscardOverlay, lv_color_hex(0x000000),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(ui.commonDiscardOverlay, LV_OPA_60,
                          LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(ui.commonDiscardOverlay, 0,
                                LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_t *dialog = lv_obj_create(ui.commonDiscardOverlay);
  lv_obj_set_size(dialog, 300, 180);
  lv_obj_center(dialog);
  lv_obj_set_style_bg_color(dialog, lv_color_hex(0x1f2630),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(dialog, lv_color_hex(0x4a5d71),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(dialog, 1, LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_t *dialogText = lv_label_create(dialog);
  lv_obj_set_pos(dialogText, 14, 18);
  lv_obj_set_width(dialogText, 272);
  lv_label_set_long_mode(dialogText, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(dialogText, LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(dialogText, "You have unsent changes.\nSend or Cancel?");

  createButton(dialog, "Send", 24, 112, 110, 46, onCommonDiscardSend);
  createButton(dialog, "Cancel", 166, 112, 110, 46, onCommonDiscardCancel);
  showCommonDiscardOverlay(false);
  uiYield();
}

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

  Serial.println("PRD_UI: init hideLegacyWidgets");
  hideLegacyWidgets();
  uiYield();

  Serial.println("PRD_UI: init createLeftReadouts");
  createLeftReadouts(objects.mould_settings, &ui.posLabelMould,
                     &ui.tempLabelMould);
  createLeftReadouts(objects.common_settings, &ui.posLabelCommon,
                     &ui.tempLabelCommon);
  uiYield();

  Serial.println("PRD_UI: init createMouldPanel");
  createMouldPanel();
  createMouldEditPanel();
  createCommonPanel();
  uiYield();

  ui.mouldProfileCount = 1;
  strncpy(ui.mouldProfiles[0].name, "Awaiting QUERY_MOULD",
          sizeof(ui.mouldProfiles[0].name) - 1);
  ui.mouldProfiles[0].name[sizeof(ui.mouldProfiles[0].name) - 1] = '\0';
  rebuildMouldList();
  ui.selectedMould = -1;

  setButtonEnabled(ui.mouldButtonSend, false);
  setButtonEnabled(ui.mouldButtonEdit, false);
  setButtonEnabled(ui.commonButtonSend, false);

  ui.initialized = true;
  Serial.println("PRD_UI: init complete");
}

void tick() {
  if (!ui.initialized) {
    return;
  }

  const DisplayComms::Status &status = DisplayComms::getStatus();
  const DisplayComms::MouldParams &mould = DisplayComms::getMould();
  const DisplayComms::CommonParams &common = DisplayComms::getCommon();

  updateLeftReadouts(status);
  updateStateWidgets(status);
  updateErrorFrames(status);
  updateMouldListFromComms(mould);
  syncMouldSendEditEnablement();

  if (!ui.commonDirty) {
    syncCommonInputsFromModel(common);
  }
  syncCommonSendEnablement();
}

bool isInitialized() { return ui.initialized; }

} // namespace PrdUi
