#include "display_ui.h"
#include "config.h"
#include "runtime_config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <Wire.h>

Adafruit_SSD1306 display(LCD_WIDTH, LCD_HEIGHT, &Wire, -1);

int eye_state = 0; // 0: Neutral, 1: Blink, 2: Left, 3: Right, 4: Happy, 5: Wink
static bool display_ready = false;
static bool info_mode = false;
static bool button_prev_raw = false;
static bool button_stable = false;
static unsigned long button_last_change_ms = 0;
static unsigned long last_render_ms = 0;
static unsigned long last_info_refresh_ms = 0;
static unsigned long next_expression_ms = 0;
static unsigned long blink_until_ms = 0;

static const unsigned long BUTTON_DEBOUNCE_MS = 40;
static const unsigned long RENDER_INTERVAL_MS = 70;
static const unsigned long INFO_REFRESH_MS = 400;

void draw_eyes() {
  display.clearDisplay();

  const int eye_width = 28;
  const int eye_height = 18;
  const int eye_y = 7;
  const int left_eye_x = 18;
  const int right_eye_x = 128 - 18 - eye_width;

  auto draw_open_eye = [&](int x, int pupil_offset_x, bool sleepy) {
    display.fillRoundRect(x, eye_y, eye_width, eye_height, 7, SSD1306_WHITE);
    int pupil_w = 7;
    int pupil_h = sleepy ? 4 : 8;
    int pupil_x = x + 10 + pupil_offset_x;
    int pupil_y = eye_y + (sleepy ? 9 : 5);
    display.fillRoundRect(pupil_x, pupil_y, pupil_w, pupil_h, 3, SSD1306_BLACK);
  };

  auto draw_blink_eye = [&](int x, bool smile) {
    int y = eye_y + 10;
    if (smile) {
      display.drawLine(x, y + 1, x + eye_width / 2, y - 2, SSD1306_WHITE);
      display.drawLine(x + eye_width / 2, y - 2, x + eye_width, y + 1,
                       SSD1306_WHITE);
    } else {
      display.fillRoundRect(x + 1, y, eye_width - 2, 3, 2, SSD1306_WHITE);
    }
  };

  switch (eye_state) {
  case 0: // Neutral
    draw_open_eye(left_eye_x, 0, false);
    draw_open_eye(right_eye_x, 0, false);
    break;
  case 1: // Blink
    draw_blink_eye(left_eye_x, false);
    draw_blink_eye(right_eye_x, false);
    break;
  case 2: // Looking Left
    draw_open_eye(left_eye_x, -3, false);
    draw_open_eye(right_eye_x, -3, false);
    break;
  case 3: // Looking Right
    draw_open_eye(left_eye_x, 3, false);
    draw_open_eye(right_eye_x, 3, false);
    break;
  case 4: // Happy
    draw_blink_eye(left_eye_x, true);
    draw_blink_eye(right_eye_x, true);
    break;
  case 5: // Wink
    draw_blink_eye(left_eye_x, false);
    draw_open_eye(right_eye_x, 1, false);
    break;
  }
  display.display();
}

void draw_info() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  String ssid = WiFi.SSID();
  if (ssid.length() == 0)
    ssid = "Not Connected";
  display.println("SSID: " + ssid);

  display.setCursor(0, 10);
  display.println("IP: " + WiFi.localIP().toString());

  display.setCursor(0, 20);
  String ws_uri = runtime_config_get().ws_uri;
  if (ws_uri.length() > 21) {
    ws_uri = ws_uri.substring(0, 18) +
             "..."; // Truncate if too long 128 cols = approx 21 chars at size 1
  }
  display.println("WS: " + ws_uri);

  display.display();
}

void display_ui_init() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    return;
  }

  display.clearDisplay();
  display.display();
  display_ready = true;
}

void display_ui_loop() {
  if (!display_ready) {
    return;
  }

  unsigned long now = millis();
  bool button_raw = (digitalRead(BUTTON_PIN) == LOW);
  if (button_raw != button_prev_raw) {
    button_prev_raw = button_raw;
    button_last_change_ms = now;
  }
  if (now - button_last_change_ms >= BUTTON_DEBOUNCE_MS) {
    button_stable = button_raw;
  }

  bool button_pressed = button_stable;

  if (button_pressed != info_mode) {
    info_mode = button_pressed;
    last_render_ms = 0;
    if (info_mode) {
      last_info_refresh_ms = 0;
    }
  }

  if (info_mode) {
    if (now - last_info_refresh_ms >= INFO_REFRESH_MS || last_info_refresh_ms == 0) {
      draw_info();
      last_info_refresh_ms = now;
    }
    // Reset eye animation timer so it doesn't jump when released
    next_expression_ms = now + 250;
    eye_state = 0;
  } else {
    if (blink_until_ms != 0 && now >= blink_until_ms) {
      blink_until_ms = 0;
      eye_state = 0;
    }

    if (next_expression_ms == 0 || now >= next_expression_ms) {
      int r = random(0, 100);
      if (r < 45) {
        eye_state = 0; // open
        next_expression_ms = now + 1000;
      } else if (r < 62) {
        eye_state = 2; // left
        next_expression_ms = now + 900;
      } else if (r < 79) {
        eye_state = 3; // right
        next_expression_ms = now + 900;
      } else if (r < 89) {
        eye_state = 4; // happy
        next_expression_ms = now + 700;
      } else if (r < 96) {
        eye_state = 5; // wink
        next_expression_ms = now + 550;
      } else {
        eye_state = 1; // blink
        blink_until_ms = now + 130;
        next_expression_ms = now + 450;
      }
    }

    if (now - last_render_ms >= RENDER_INTERVAL_MS || last_render_ms == 0) {
      draw_eyes();
      last_render_ms = now;
    }
  }
}
