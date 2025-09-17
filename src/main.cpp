#include <Arduino.h>
#include "Adafruit_ThinkInk.h"
#include "lvgl.h"
#include <WiFi.h>
#include <ArduinoHttpClient.h>
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

/*LVGL draw into this buffer, 1/10 screen size usually works well. The size is in bytes*/
#define DRAW_BUF_SIZE (HOR_RES * VER_RES / 5 * (LV_COLOR_DEPTH))

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

lv_obj_t *msg_label;
lv_obj_t *wifi_label;
lv_obj_t *v_label;
int last_wifi_status = WL_IDLE_STATUS;

const int ADC_BITS = 12;
const int ADC_COUNTS_FULLSCALE = 1 << ADC_BITS;

void setup() {
  // digitalWrite(LED_BUILTIN, 1);
  Serial.begin();
  // while (!Serial) {
  //   digitalWrite(LED_BUILTIN, (millis() >> 8) & 0x1);
  // }
  // digitalWrite(LED_BUILTIN, 0);
  Serial.println("booted");

  analogReadResolution(ADC_BITS); // analogRead defaults to 10-bit ADC resolution; bump it up

  epd.begin(THINKINK_MONO);

  Serial.println("past epd init");

  lv_init();
  lv_tick_set_cb(my_tick);

  lv_display_t *disp;
  disp = lv_display_create(HOR_RES, VER_RES);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

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
}

WiFiClient wifi;
HttpClient http(wifi, SERVER_ADDRESS, SERVER_PORT);

const float VREF = 3.3f;

const float ADC1_DIV_LO = 680.f;
const float ADC1_DIV_HI = 1000.f;
const float ADC2_DIV_LO = 1000.f;
const float ADC2_DIV_HI = 1000.f;

const float ADC1_MULT = (ADC1_DIV_HI + ADC1_DIV_LO) / ADC1_DIV_LO;
const float ADC2_MULT = (ADC2_DIV_HI + ADC2_DIV_LO) / ADC2_DIV_LO;

int loop_count = 0;
int adc1_count_accum = 0;
int adc2_count_accum = 0;

void loop() {
  int wifi_status = WiFi.status();

  // update wifi status, if it's changed (set_text always dirties the screen, so
  // don't thrash the e-paper if it hasn't)
  if (wifi_status != last_wifi_status) {
    switch (wifi_status) {
      case WL_CONNECTED:
        lv_label_set_text(wifi_label, LV_SYMBOL_WIFI LV_SYMBOL_OK);
        break;
      default:
        lv_label_set_text(wifi_label, LV_SYMBOL_WIFI LV_SYMBOL_REFRESH);
        break;
    }

    last_wifi_status = wifi_status;
  }

  if (wifi_status == WL_CONNECTED) {
    // hit API every ~30 seconds
    if (loop_count % 6144 == 0) {
      http.get("/message");

      int status_code = http.responseStatusCode();

      if (status_code >= 300) {
        lv_label_set_text_fmt(msg_label, "error: API returned status code %d", status_code);
      } else {
        lv_label_set_text(msg_label, http.responseBody().c_str());
      }
    }
  } else {
    // if we're not connected to wifi, try to reconnect to wifi
    WiFi.begin(WIFI_SSID, WIFI_PSK);
  }

  // check vin + vbat every ~5 seconds -- oversample a bit to smooth out noise
  if (loop_count % 1024 == 0) {
    adc1_count_accum += analogRead(27);
    adc2_count_accum += analogRead(28);
  }

  // update voltage display every ~20 seconds
  if (loop_count % 4096 == 0) {
    // divide out the oversampling; convert the averaged count back to a voltage
    float adc1_v = VREF * ADC1_MULT * (adc1_count_accum >> 2) / ADC_COUNTS_FULLSCALE;
    float adc2_v = VREF * ADC2_MULT * (adc2_count_accum >> 2) / ADC_COUNTS_FULLSCALE;

    int adc1_v_units = (int)adc1_v;
    int adc1_v_100ths = (int)(adc1_v * 100.f) % 100;
    int adc2_v_units = (int)adc2_v;
    int adc2_v_100ths = (int)(adc2_v * 100.f) % 100;

    lv_label_set_text_fmt(v_label, "vin %d.%02d bat %d.%02d", adc1_v_units, adc1_v_100ths, 
      adc2_v_units, adc2_v_100ths);

    adc1_count_accum = adc2_count_accum = 0;
  }

  lv_timer_handler();
  delay(5);

  loop_count++;
}
