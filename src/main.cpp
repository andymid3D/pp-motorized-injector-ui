/**
 * Motorized Injector HMI - EEZ Studio LVGL Project
 * Main PlatformIO sketch for ESP32 with profile-selectable RGB display
 */

#include <Arduino.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// EEZ Studio generated UI files
#include "ui/ui.h"
#include "ui/structs.h"
#include "ui/vars.h"
#include "ui/eez-flow.h"
#include "ui/screens.h"
#include "display_comms.h"
#include "prd_ui.h"

// Import BARREL_CAPACITY_MM from actions
extern const float BARREL_CAPACITY_MM;

#ifndef SCREEN_DIAG_ONLY
#define SCREEN_DIAG_ONLY 0
#endif

#ifndef SCREEN_DIAG_DISABLE_TOUCH_I2C
#define SCREEN_DIAG_DISABLE_TOUCH_I2C 0
#endif

#ifndef SCREEN_DIAG_DISABLE_BACKLIGHT_I2C
#define SCREEN_DIAG_DISABLE_BACKLIGHT_I2C 0
#endif

#define TFT_WIDTH 800
#define TFT_HEIGHT 480

#if defined(DISPLAY_PROFILE_WAVESHARE_4_3B)

#define TFT_BL -1

#ifndef DISPLAY_UART_RX_PIN
#define DISPLAY_UART_RX_PIN 43
#endif

#ifndef DISPLAY_UART_TX_PIN
#define DISPLAY_UART_TX_PIN 44
#endif

#define RGB_PIN_D0 GPIO_NUM_14
#define RGB_PIN_D1 GPIO_NUM_38
#define RGB_PIN_D2 GPIO_NUM_18
#define RGB_PIN_D3 GPIO_NUM_17
#define RGB_PIN_D4 GPIO_NUM_10
#define RGB_PIN_D5 GPIO_NUM_39
#define RGB_PIN_D6 GPIO_NUM_0
#define RGB_PIN_D7 GPIO_NUM_45
#define RGB_PIN_D8 GPIO_NUM_48
#define RGB_PIN_D9 GPIO_NUM_47
#define RGB_PIN_D10 GPIO_NUM_21
#define RGB_PIN_D11 GPIO_NUM_1
#define RGB_PIN_D12 GPIO_NUM_2
#define RGB_PIN_D13 GPIO_NUM_42
#define RGB_PIN_D14 GPIO_NUM_41
#define RGB_PIN_D15 GPIO_NUM_40

#define RGB_PIN_HENABLE GPIO_NUM_5
#define RGB_PIN_VSYNC GPIO_NUM_3
#define RGB_PIN_HSYNC GPIO_NUM_46
#define RGB_PIN_PCLK GPIO_NUM_7

#define RGB_FREQ_WRITE 16000000
#define RGB_HSYNC_FRONT 8
#define RGB_HSYNC_PULSE 4
#define RGB_HSYNC_BACK 8
#define RGB_VSYNC_FRONT 8
#define RGB_VSYNC_PULSE 4
#define RGB_VSYNC_BACK 8

#else

#define TFT_BL 2

#ifndef DISPLAY_UART_RX_PIN
#define DISPLAY_UART_RX_PIN 44
#endif

#ifndef DISPLAY_UART_TX_PIN
#define DISPLAY_UART_TX_PIN 43
#endif

#define RGB_PIN_D0 GPIO_NUM_8
#define RGB_PIN_D1 GPIO_NUM_3
#define RGB_PIN_D2 GPIO_NUM_46
#define RGB_PIN_D3 GPIO_NUM_9
#define RGB_PIN_D4 GPIO_NUM_1
#define RGB_PIN_D5 GPIO_NUM_5
#define RGB_PIN_D6 GPIO_NUM_6
#define RGB_PIN_D7 GPIO_NUM_7
#define RGB_PIN_D8 GPIO_NUM_15
#define RGB_PIN_D9 GPIO_NUM_16
#define RGB_PIN_D10 GPIO_NUM_4
#define RGB_PIN_D11 GPIO_NUM_45
#define RGB_PIN_D12 GPIO_NUM_48
#define RGB_PIN_D13 GPIO_NUM_47
#define RGB_PIN_D14 GPIO_NUM_21
#define RGB_PIN_D15 GPIO_NUM_14

#define RGB_PIN_HENABLE GPIO_NUM_40
#define RGB_PIN_VSYNC GPIO_NUM_41
#define RGB_PIN_HSYNC GPIO_NUM_39
#define RGB_PIN_PCLK GPIO_NUM_0

#define RGB_FREQ_WRITE 15000000
#define RGB_HSYNC_FRONT 8
#define RGB_HSYNC_PULSE 4
#define RGB_HSYNC_BACK 43
#define RGB_VSYNC_FRONT 8
#define RGB_VSYNC_PULSE 4
#define RGB_VSYNC_BACK 12

#endif

// LovyanGFX display configuration
class LGFX : public lgfx::LGFX_Device
{
public:
  lgfx::Bus_RGB     _bus_instance;
  lgfx::Panel_RGB   _panel_instance;

  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;

      cfg.pin_d0  = RGB_PIN_D0;
      cfg.pin_d1  = RGB_PIN_D1;
      cfg.pin_d2  = RGB_PIN_D2;
      cfg.pin_d3  = RGB_PIN_D3;
      cfg.pin_d4  = RGB_PIN_D4;

      cfg.pin_d5  = RGB_PIN_D5;
      cfg.pin_d6  = RGB_PIN_D6;
      cfg.pin_d7  = RGB_PIN_D7;
      cfg.pin_d8  = RGB_PIN_D8;
      cfg.pin_d9  = RGB_PIN_D9;
      cfg.pin_d10 = RGB_PIN_D10;

      cfg.pin_d11 = RGB_PIN_D11;
      cfg.pin_d12 = RGB_PIN_D12;
      cfg.pin_d13 = RGB_PIN_D13;
      cfg.pin_d14 = RGB_PIN_D14;
      cfg.pin_d15 = RGB_PIN_D15;

      cfg.pin_henable = RGB_PIN_HENABLE;
      cfg.pin_vsync   = RGB_PIN_VSYNC;
      cfg.pin_hsync   = RGB_PIN_HSYNC;
      cfg.pin_pclk    = RGB_PIN_PCLK;
      cfg.freq_write  = RGB_FREQ_WRITE;

      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = RGB_HSYNC_FRONT;
      cfg.hsync_pulse_width = RGB_HSYNC_PULSE;
      cfg.hsync_back_porch  = RGB_HSYNC_BACK;

      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = RGB_VSYNC_FRONT;
      cfg.vsync_pulse_width = RGB_VSYNC_PULSE;
      cfg.vsync_back_porch  = RGB_VSYNC_BACK;

      cfg.pclk_active_neg   = 1;
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;

      _bus_instance.config(cfg);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = TFT_WIDTH;
      cfg.memory_height = TFT_HEIGHT;
      cfg.panel_width   = TFT_WIDTH;
      cfg.panel_height  = TFT_HEIGHT;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
};

LGFX lcd;

// Touch panel configuration
// Must come after lcd declaration as touch.h may map against display dimensions
#include "touch.h"

static void panel_backlight_on() {
#if defined(DISPLAY_PROFILE_WAVESHARE_4_3B)
#if SCREEN_DIAG_DISABLE_BACKLIGHT_I2C
  Serial.println("[BOOT] backlight I2C disabled by SCREEN_DIAG_DISABLE_BACKLIGHT_I2C");
#else
  // Waveshare 4.3B: backlight through CH422G I2C expander
  uint8_t v = 0x01;
  Wire.beginTransmission(0x24);
  Wire.write(v);
  uint8_t e1 = Wire.endTransmission();

  v = 0x1E;
  Wire.beginTransmission(0x38);
  Wire.write(v);
  uint8_t e2 = Wire.endTransmission();

  Serial.printf("[BOOT] backlight CH422G endTransmission: e1=%u e2=%u\n", e1, e2);
#endif
#else
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif
}

// Display flushing callback for LVGL
void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  (void)disp;
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  lv_draw_sw_rgb565_swap(px_map, w * h);
  lcd.pushImageDMA(area->x1, area->y1, w, h, (uint16_t *)px_map);
  lv_disp_flush_ready(disp);
}

uint32_t my_tick_cb() {
  return (esp_timer_get_time() / 1000LL);
}

void my_touch_read_cb(lv_indev_t * drv, lv_indev_data_t * data) {

  if (touch_has_signal()) {
    if (touch_touched()) {
      data->state = LV_INDEV_STATE_PRESSED;
      data->point.x = touch_last_x;
      data->point.y = touch_last_y;
    } else if (touch_released()) {
      data->state = LV_INDEV_STATE_RELEASED;
    }
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }

  delay(15);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.setDebugOutput(true);
  Serial.println("[BOOT] Motorized Injector HMI Starting...");

#if !SCREEN_DIAG_DISABLE_TOUCH_I2C || !SCREEN_DIAG_DISABLE_BACKLIGHT_I2C
  // Force I2C in master mode explicitly (avoid accidental slave overloads)
  Wire.begin((int)TOUCH_GT911_SDA, (int)TOUCH_GT911_SCL);
#else
  Serial.println("[BOOT] global I2C init skipped by diag flags");
#endif

#if defined(DISPLAY_PROFILE_WAVESHARE_4_3B)
  Serial.println("[BOOT] Display profile: Waveshare 4.3B");
#else
  Serial.println("[BOOT] Display profile: Elecrow 5\"");
#endif

  Serial.println("[BOOT] panel_backlight_on...");
  panel_backlight_on();
  Serial.println("[BOOT] panel_backlight_on done");

#if SCREEN_DIAG_DISABLE_TOUCH_I2C
  Serial.println("[BOOT] touch_init skipped by SCREEN_DIAG_DISABLE_TOUCH_I2C");
#else
  Serial.println("[BOOT] touch_init...");
  touch_init();
  Serial.println("[BOOT] touch_init done");
#endif

  Serial.println("[BOOT] lcd.init...");
  lcd.init();
  lcd.setRotation(1);
  lcd.fillScreen(TFT_BLACK);
  delay(200);
  Serial.printf("[BOOT] Display %dx%d\n", lcd.width(), lcd.height());

  Serial.println("[BOOT] lv_init...");
  lv_init();
  lv_tick_set_cb(my_tick_cb);

  lv_display_t *display = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
  static uint8_t buf[TFT_WIDTH * TFT_HEIGHT / 10 * 2];
  lv_display_set_buffers(display, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, my_flush_cb);
  lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);

#if SCREEN_DIAG_DISABLE_TOUCH_I2C
  Serial.println("[BOOT] LVGL display initialized (touch disabled)");
#else
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touch_read_cb);
  Serial.println("[BOOT] LVGL display+input initialized");
#endif

  Serial.println("[BOOT] ui_init...");
  ui_init();
  Serial.println("[BOOT] ui_init done");

  plunger_stateValue plungerStateValue(eez::flow::getGlobalVariable(FLOW_GLOBAL_VARIABLE_PLUNGER_STATE));
  if (plungerStateValue) {
    plungerStateValue.max_barrel_capacity(BARREL_CAPACITY_MM);
  }

#if SCREEN_DIAG_ONLY
  Serial.println("[BOOT] SCREEN_DIAG_ONLY=1 -> comms/prd_ui disabled");
#else
  DisplayComms::begin(Serial2, DISPLAY_UART_RX_PIN, DISPLAY_UART_TX_PIN, 115200);
  Serial.printf("[BOOT] Display UART init RX=%d TX=%d\n", DISPLAY_UART_RX_PIN, DISPLAY_UART_TX_PIN);

  PrdUi::init();

  DisplayComms::sendQueryState();
  DisplayComms::sendQueryError();
  DisplayComms::sendQueryMould();
  DisplayComms::sendQueryCommon();
#endif
}

void loop() {
  static int16_t lastScreen = -1;
  static uint32_t lastHeartbeatMs = 0;

  lv_timer_handler();
  ui_tick();

#if !SCREEN_DIAG_ONLY
  if (!PrdUi::isInitialized()) {
    PrdUi::init();
  }

  DisplayComms::update();
  DisplayComms::applyUiUpdates();
  PrdUi::tick();

  if (g_currentScreen != lastScreen) {
    int screenId = g_currentScreen + 1;
    if (screenId == SCREEN_ID_MOULD_SETTINGS) {
      DisplayComms::sendQueryMould();
    } else if (screenId == SCREEN_ID_COMMON_SETTINGS) {
      DisplayComms::sendQueryCommon();
    } else {
      DisplayComms::sendQueryState();
      DisplayComms::sendQueryError();
    }
    lastScreen = g_currentScreen;
  }
#endif

  uint32_t now = ::millis();
  if (now - lastHeartbeatMs >= 1000) {
    lastHeartbeatMs = now;
    Serial.printf("[LOOP] alive t=%lu screen=%d\n", (unsigned long)now, (int)g_currentScreen);
  }

  delay(5);
}

