#include <Arduino.h>
#include "Adafruit_ThinkInk.h"
#include "lvgl.h"
#include <WiFi.h>
#include <ArduinoHttpClient.h>
#include <protothreads.h>
#include "secrets.hpp"

// e-paper breakout pinout:
// SCK GP14, MISO GP12, MOSI GP15, ECS GP13, D/C GP11, SRCS GP10, SDCS GP9,
// RST GP8, BUSY GP7, ENA GP6
//
// not using SRAM since the RP2040 has more than enough RAM for our purposes
ThinkInk_213_Mono_GDEY0213B74 epd(11 /* D/C */, 8 /* RST */, 13 /* CS */, 
  -1 /* SRCS */, 7 /* BUSY */, &SPI1);

/*Set to your screen resolution and rotation*/
#define HOR_RES 250
#define VER_RES 122
#define ROTATION LV_DISPLAY_ROTATION_0

// make sure the drawing buffer is large enough that lvgl can draw the entire
// screen at once, to avoid multiple repaints per update
#define DRAW_BUF_SIZE ((HOR_RES * VER_RES * LV_COLOR_DEPTH) + 16)

uint32_t draw_buf[DRAW_BUF_SIZE / 4];

static uint32_t my_tick() {
  return millis();
}

/* LVGL calls it when a rendered image needs to copied to the display*/
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, unsigned char *px_map)
{
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);

  // px_map starts with an 8-byte header that throws off the alignment of the screen
  epd.drawBitmap(area->x1, area->y1, (uint8_t *)(px_map + 8), w, h, EPD_BLACK, EPD_WHITE);
  epd.display();

  lv_display_flush_ready(disp);
}

int last_response_code = 200;
String last_message("starting up...");

float vin_v = 0.0f;
float vbat_v = 0.0f;

static pt lvgl_task_pt;
static pt update_screen_task_pt;
static pt wifi_keep_alive_task_pt;
static pt fetch_message_task_pt;
static pt adc_update_task_pt;

lv_display_t *disp;

lv_obj_t *msg_label;
lv_obj_t *wifi_label;
lv_obj_t *v_label;

WiFiClient wifi;
HttpClient http(wifi, SERVER_ADDRESS, SERVER_PORT);

static int lvgl_task(pt *pt) {
  PT_BEGIN(pt);

  while (true) {
    lv_timer_handler();
    PT_SLEEP(pt, 50);
  }

  PT_END(pt);
}

static int update_screen_task(pt *pt) {
  PT_BEGIN(pt);

  epd.begin(THINKINK_MONO);

  Serial.println("past epd init");

  lv_init();
  lv_tick_set_cb(my_tick);

  disp = lv_display_create(HOR_RES, VER_RES);
  // disable automatic refresh because this is e-paper and LVGL will
  // automatically refresh after each element on the screen changes, which
  // is obnoxious
  lv_display_delete_refr_timer(disp);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_FULL);

  Serial.println("past lvgl init");

  msg_label = lv_label_create(lv_screen_active());
  lv_obj_set_width(msg_label, HOR_RES);
  lv_obj_set_height(msg_label, VER_RES - 12);
  lv_label_set_long_mode(msg_label, LV_LABEL_LONG_DOT);
  lv_label_set_text(msg_label, "starting up...");
  // "the ship at pier 66 is the Serenade of the Seas.  she arrived from Vancouver "
  // "2 hours ago.  she weighs 90,090 tons, is 293 meters long, and carries 2,143 passengers."
  lv_obj_set_style_text_font(msg_label, &lv_font_montserrat_16, 0);
  lv_obj_align(msg_label, LV_ALIGN_TOP_LEFT, 0, 12);

  wifi_label = lv_label_create(lv_screen_active());
  lv_label_set_text(wifi_label, LV_SYMBOL_WIFI LV_SYMBOL_POWER);
  lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_10, 0);
  lv_obj_align(wifi_label, LV_ALIGN_TOP_RIGHT, 0, 0);

  v_label = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_font(v_label, &lv_font_montserrat_10, 0);
  lv_obj_align(v_label, LV_ALIGN_TOP_LEFT, 0, 0);

  Serial.println("past lvgl build");

  epd.begin();
  // invert the pixel buffer to match the white-on-black pixel buffer coming out of lvgl
  epd.setColorBuffer(0, false);
  epd.setBlackBuffer(0, false);
  epd.clearBuffer();
  epd.display();

  Serial.println("past epd present");

  while (true) {
    Serial.println("update_screen_task loop");

    // update wifi status
    switch (WiFi.status()) {
      case WL_CONNECTED:
        lv_label_set_text(wifi_label, LV_SYMBOL_WIFI LV_SYMBOL_OK);
        break;
      default:
        lv_label_set_text(wifi_label, LV_SYMBOL_WIFI LV_SYMBOL_REFRESH);
        break;
    }

    // update voltage display
    int vin_v_units = (int)vin_v;
    int vin_v_100ths = (int)(vin_v * 100.f) % 100;
    int vbat_v_units = (int)vbat_v;
    int vbat_v_100ths = (int)(vbat_v * 100.f) % 100;

    lv_label_set_text_fmt(v_label, "vin %d.%02d bat %d.%02d", vin_v_units, vin_v_100ths, 
      vbat_v_units, vbat_v_100ths);

    // update message
    if (last_response_code >= 300) {
      lv_label_set_text_fmt(msg_label, "error: API returned status code %d", last_response_code);
    } else {
      lv_label_set_text(msg_label, last_message.c_str());
    }

    lv_display_refr_timer(NULL);
    PT_SLEEP(pt, 30000);
  }

  PT_END(pt);
}

static int wifi_keep_alive_task(pt *pt) {
  PT_BEGIN(pt);

  while (true) {
    Serial.println("wifi_keep_alive_task loop");

    if (WiFi.status() != WL_CONNECTED) {
      // if we're not connected to wifi, try to reconnect to wifi
      WiFi.begin(WIFI_SSID, WIFI_PSK);
    }

    PT_SLEEP(pt, 1000);
  }

  PT_END(pt);
}

static int fetch_message_task(pt *pt) {
  PT_BEGIN(pt);

  while (true) {
    Serial.println("fetch_message_task loop");

    if (WiFi.status() == WL_CONNECTED) {
      http.get("/message");

      last_response_code = http.responseStatusCode();
      last_message = http.responseBody();
    }

    PT_SLEEP(pt, 30000);
  }

  PT_END(pt);
}
  
const int ADC_BITS = 12;
const int ADC_COUNTS_FULLSCALE = 1 << ADC_BITS;

static int adc_update_task(pt *pt) {
  PT_BEGIN(pt);

  const float VREF = 3.3f;

  const float ADC1_DIV_LO = 680.f;
  const float ADC1_DIV_HI = 1000.f;
  const float ADC2_DIV_LO = 1000.f;
  const float ADC2_DIV_HI = 1000.f;

  const float ADC1_MULT = (ADC1_DIV_HI + ADC1_DIV_LO) / ADC1_DIV_LO;
  const float ADC2_MULT = (ADC2_DIV_HI + ADC2_DIV_LO) / ADC2_DIV_LO;

  while (true) {
    Serial.println("adc_update_task loop");

    int vin_counts = analogRead(27);
    int vbat_counts = analogRead(28);

    // convert the counts to a voltage
    vin_v = VREF * ADC1_MULT * vin_counts / ADC_COUNTS_FULLSCALE;
    vbat_v = VREF * ADC2_MULT * vbat_counts / ADC_COUNTS_FULLSCALE;

    PT_SLEEP(pt, 5000);
  }

  PT_END(pt);
}

void setup() {
  // digitalWrite(LED_BUILTIN, 1);
  Serial.begin();
  // while (!Serial) {
  //   digitalWrite(LED_BUILTIN, (millis() >> 8) & 0x1);
  // }
  // digitalWrite(LED_BUILTIN, 0);
  Serial.println("booted");

  analogReadResolution(ADC_BITS); // analogRead defaults to 10-bit ADC resolution; bump it up

  PT_INIT(&lvgl_task_pt);
  PT_INIT(&update_screen_task_pt);
  PT_INIT(&wifi_keep_alive_task_pt);
  PT_INIT(&fetch_message_task_pt);
  PT_INIT(&adc_update_task_pt);
}

void loop() {
  lvgl_task(&lvgl_task_pt);
  update_screen_task(&update_screen_task_pt);
  wifi_keep_alive_task(&wifi_keep_alive_task_pt);
  fetch_message_task(&fetch_message_task_pt);
  adc_update_task(&adc_update_task_pt);
}
