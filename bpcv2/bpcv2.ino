// copyright 2024 BotanicFields, Inc.
// BPC Simulator Ver.2 for ATOM Lite / ATOM Matrix / ATOMS3 Lite
//
#include <M5Unified.h>
#include <FastLED.h>      // https://github.com/FastLED/FastLED
#include <Ticker.h>
#include <WiFi.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include "BF_Pcf8563.h"
#include "BF_RtcxNtp.h"

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// for TCO(Time Code Output)
const int bpc_frequency(68500); // 68.5kHz
struct tm       td;  // time of day: year, month, day, day of week, hour, minute, second
struct timespec ts;  // time spec: second, nano-second

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// for NTP
const char* time_zone  = "CST-8";
const char* ntp_server = "pool.ntp.org";
bool rtcx_available(false);
bool localtime_valid(false);

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// for WiFi
const          int wifi_config_portal_timeout_sec(60);
const unsigned int wifi_retry_interval_ms(60000);
      unsigned int wifi_retry_last_ms(0);
const          int wifi_retry_max_times(3);
               int wifi_retry_times(0);

wl_status_t wifi_status(WL_NO_SHIELD);
const char* wl_status_str[] = {
  "WL_IDLE_STATUS",      // 0
  "WL_NO_SSID_AVAIL",    // 1
  "WL_SCAN_COMPLETED",   // 2
  "WL_CONNECTED",        // 3
  "WL_CONNECT_FAILED",   // 4
  "WL_CONNECTION_LOST",  // 5
  "WL_DISCONNECTED",     // 6
  "WL_NO_SHIELD",        // 7 <-- 255
  "wl_status invalid",   // 8
};

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// for FastLED
const int led_pin_atom(27);        // GPIO27 for ATOM Lite/Matrix
const int led_pin_atoms3lite(35);  // GPIO35 for ATOMS3 Lite
const unsigned char led_num(25);   // ATOM Matrix: 25, ATOM lite/ATOMS3 Lite: 1 --> 25
CRGB leds[led_num]; 

const int led_on_r(0x80);                // 0..0xFF
const int led_on_g(0x80);                // 0..0xFF
const int led_on_b(0x80);                // 0..0xFF
const int led_brightness(40);            // 0..0xFF
const unsigned int blink_slow_ms(1000);  // blink period
const unsigned int blink_fast_ms( 200);  // blink period

enum led_r_t {
  led_r_off,   // WL_CONNECTED
  led_r_slow,  // WL_NO_SSID_AVAIL
  led_r_fast,  // WL_DISCONNECTED
  led_r_on,    // WL_IDLE_STATUS, WL_CONNECTION_LOST, etc.
};
enum led_g_t {
  led_g_off,   // time valid
  led_g_slow,  // waiting time valid
  led_g_fast,  //
  led_g_on,    // WiFi configuration portal is active
};
enum led_b_t {
  led_b_off,   // TCO off
  led_b_slow,  //
  led_b_fast,  //
  led_b_on,    // TCO on
};
led_r_t led_r(led_r_off);
led_g_t led_g(led_g_off);
led_b_t led_b(led_b_off);

bool led_enable(true);

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// LED
void LedShow()
{
  CRGB led(0);
  if (led_enable) {
    switch (led_r) {
      case led_r_on  : led.r = led_on_r;  break;
      case led_r_fast: led.r = LedBlink(blink_fast_ms) ? led_on_r : 0;  break;
      case led_r_slow: led.r = LedBlink(blink_slow_ms) ? led_on_r : 0;  break;
      default:  break;  // led.r = 0
    }
    switch (led_g) {
      case led_g_on  : led.g = led_on_g;  break;
      case led_g_fast: led.g = LedBlink(blink_fast_ms) ? led_on_g : 0;  break;
      case led_g_slow: led.g = LedBlink(blink_slow_ms) ? led_on_g : 0;  break;
      default:  break;  // led.g = 0
    }
    switch (led_b) {
      case led_b_on  : led.b = led_on_b;  break;
      case led_b_fast: led.b = LedBlink(blink_fast_ms) ? led_on_b : 0;  break;
      case led_b_slow: led.b = LedBlink(blink_slow_ms) ? led_on_b : 0;  break;
      default:  break;  // led.b = 0
    }
  }
  leds[0] = led;
  FastLED.show();
}

bool LedBlink(unsigned int period_ms)
{
  return millis() / period_ms % 2 != 0;
}

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// WiFi
const char* WlStatus(wl_status_t wl_status)
{
  if (wl_status >= 0 && wl_status <= 6) {
    return wl_status_str[wl_status];
  }
  if (wl_status == 255) {
    return wl_status_str[7];
  }
  return wl_status_str[8];
}

void WifiCheck()
{
  wl_status_t wifi_status_new = WiFi.status();
  if (wifi_status != wifi_status_new) {
    wifi_status = wifi_status_new;
    Serial.printf("[WiFi]%s\n", WlStatus(wifi_status));
    switch (wifi_status) {
      case WL_CONNECTED    : led_r = led_r_off;   break;
      case WL_NO_SSID_AVAIL: led_r = led_r_slow;  break;
      case WL_DISCONNECTED : led_r = led_r_fast;  break;
      default              : led_r = led_r_on;    break;  // state transition also
    }
  }

  // retry interval
  if (millis() - wifi_retry_last_ms < wifi_retry_interval_ms) {
    return;
  }
  wifi_retry_last_ms = millis();

  // reboot if wifi connection fails
  if (wifi_status == WL_CONNECT_FAILED) {
    Serial.print("[WiFi]connect failed: rebooting..\n");
    ESP.restart();
    return;
  }

  // let the wifi process do if wifi is not disconnected
  if (wifi_status != WL_DISCONNECTED) {
    wifi_retry_times = 0;
    return;
  }

  // reboot if wifi is disconnected for a long time
  if (++wifi_retry_times > wifi_retry_max_times) {
    Serial.print("[WiFi]disconnect timeout: rebooting..\n");
    ESP.restart();
    return;
  }

  // reconnect, and reboot if reconnection fails
  Serial.printf("[WiFi]reconnect %d\n", wifi_retry_times);
  if (!WiFi.reconnect()) {
    Serial.print("[WiFi]reconnect failed: rebooting..\n");
    ESP.restart();
    return;
  };
}

void WifiConfigModeCallback(WiFiManager *wm)
{
  led_g = led_g_on;  // green LED indicates configuration portal is active
  LedShow();
}

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// TCO(Time Code Output)
Ticker tk;
const int ticker_interval_ms(100);  // 100ms
const int marker(-1);  // marker code TcoValue() returns
int tco000;

// PWM for TCO signal
const uint8_t  ledc_pin_atom(22);   // GPIO22 for ATOM Lite/Matrix
const uint8_t  ledc_pin_atoms3(5);  // GPIO5 for ATOMS3 Lite
const uint8_t  ledc_channel(0);
const uint32_t ledc_frequency(bpc_frequency);
const uint8_t  ledc_resolution(8);  // 2^8 = 256
const uint32_t ledc_duty_on(128);   // 128/256 = 50%
const uint32_t ledc_duty_off(0);    // 0

void TcoInit()
{
  // carrier for TCO
  uint32_t ledc_freq_get = ledcSetup(ledc_channel, ledc_frequency, ledc_resolution);
  Serial.printf("ledc frequency get = %d\n", ledc_freq_get);
 
  int ledc_pin = ledc_pin_atom;
  if (M5.getBoard() == m5::board_t::board_M5AtomS3Lite) {
    ledc_pin = ledc_pin_atoms3;
  }
  Serial.printf("ledc pin = %d\n", ledc_pin);
  ledcAttachPin(ledc_pin, ledc_channel);

  // wait until middle of 100ms timing. ex. 50ms, 150ms, 250ms,..
  clock_gettime(CLOCK_REALTIME, &ts);
  delayMicroseconds((150000000 - ts.tv_nsec % 100000000) / 1000);

  // for the first sample of statistics
  clock_gettime(CLOCK_REALTIME, &ts);
  Serial.printf("ts.tv_nsec = %ld\n", ts.tv_nsec);

  // start Ticker for TCO
  tk.attach_ms(ticker_interval_ms, TcoGen);
}

// main task of TCO
void TcoGen()
{
  // statistics of ts_nsec
  static int    tk_count(0);
  static int    tk_max(0);
  static int    tk_min(0);
  static double tk_sum(0.0);
  static double tk_sq_sum(0.0);
  static int    tk_distribution[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  static int    tk_last_nsec(0);

  if (!localtime_valid) {
    led_g = led_g_slow;  // localtime not valid yet
    return;
  }
  led_g = led_g_off;  // localtime valid

  getLocalTime(&td);
  clock_gettime(CLOCK_REALTIME, &ts);
  int ts_100ms = ts.tv_nsec / 100000000;
  switch (ts_100ms) {
    case 0: Tco000ms();  break;
    case 1: Tco100ms();  break;
    case 2: Tco200ms();  break;
    case 3: Tco300ms();  break;
    case 4: Tco400ms();  break;
    default:             break;
  }

  if (tk_count++ != 0) {
    int tk_deviation = ts.tv_nsec - tk_last_nsec;
    if (tk_deviation < 0) {
      tk_deviation += 1000000000;  // 0xx - 9xx ms --> 10xx - 9xx ms
    }
    tk_deviation -= 100000000; // center 100 ms --> 0

    if (tk_max < tk_deviation) tk_max = tk_deviation;
    if (tk_min > tk_deviation) tk_min = tk_deviation;
    tk_sum    += (double)tk_deviation;
    tk_sq_sum += (double)tk_deviation * (double)tk_deviation;

    if      (tk_deviation < -50000000) ++tk_distribution[0];  //     ~ -50ms
    else if (tk_deviation <  -5000000) ++tk_distribution[1];  //     ~  -5ms
    else if (tk_deviation <   -500000) ++tk_distribution[2];  //     ~  -0.5ms
    else if (tk_deviation <    -50000) ++tk_distribution[3];  //     ~  -0.05ms
    else if (tk_deviation <     50000) ++tk_distribution[4];  // -0.05 ~ 0.05ms
    else if (tk_deviation <    500000) ++tk_distribution[5];  //  0.05ms ~
    else if (tk_deviation <   5000000) ++tk_distribution[6];  //  0.5ms  ~
    else if (tk_deviation <  50000000) ++tk_distribution[7];  //  5ms    ~
    else                               ++tk_distribution[8];  // 50ms    ~
  }
  tk_last_nsec = ts.tv_nsec;

  if ((td.tm_sec == 0) && (ts.tv_nsec < 100000000)) {
    for (int i = 0; i < 9; ++i) {
      Serial.printf("%d ", tk_distribution[i]);
    }
    double tk_average = tk_sum / (double)tk_count;
    double tk_variance = (tk_sq_sum - tk_sum * tk_sum / (double)tk_count) / (double)tk_count;
    double tk_std_deviation = sqrt(tk_variance);
    Serial.printf("\nn= %d, ave= %.4f  sdv= %.4f  min= %d  max= %d\n", tk_count, tk_average, tk_std_deviation, tk_min, tk_max);
  }
}

void Tco000ms()
{
  tco000 = TcoValue();
  if (tco000 == marker) {
    TcOn();
    Serial.print(&td, "\n%A %B %d %Y %H:%M:%S\n");
  } 
  else {
    TcOff();
  }
}

void Tco100ms()
{
  if (tco000 == 0) {
    TcOn();
    Serial.print("0");
  }
}

void Tco200ms()
{
  if (tco000 == 1) {
    TcOn();
    Serial.print("1");
  }
}

void Tco300ms()
{
  if (tco000 == 2) {
    TcOn();
    Serial.print("2");
  }
}

void Tco400ms()
{
  if (tco000 == 3) {
    TcOn();
    Serial.print("3");
  }
}

void TcOn()
{
  ledcWrite(ledc_channel, ledc_duty_on);
  led_b = led_b_on;
}

void TcOff()
{
  ledcWrite(ledc_channel, ledc_duty_off);
  led_b = led_b_off;
}

int TcoValue()
{
  int second1 = 0;                          // second = 0:0, 1:20, 2:40
  if (td.tm_sec >= 20) second1 = 1;
  if (td.tm_sec >= 40) second1 = 2;

  int hour3 = td.tm_hour % 12 / 4;          // hour = 0..11
  int hour4 = td.tm_hour % 4;

  int minute5 = td.tm_min / 16 ;            // minute = 0..59
  int minute6 = td.tm_min % 16 / 4;
  int minute7 = td.tm_min % 4;

  int wday8 = td.tm_wday / 4;               // day of week = 1: Monday .. 7:Sunday
  if (td.tm_wday == 0) wday8 = 1;
  int wday9 = td.tm_wday % 4;
  if (td.tm_wday == 0) wday9 = 3;

  int ampm_p10 = td.tm_hour / 12 * 2        // am pm = 0:AM, 1:PM
                 + (bits(second1) + bits(hour3) + bits(hour4) + bits(minute5) + bits(minute6) + bits(minute7) + bits(wday8) + bits(wday9)) % 2;
  
  int mday11 = td.tm_mday / 16;             // day of month = 1..31
  int mday12 = td.tm_mday % 16 / 4;
  int mday13 = td.tm_mday % 4;

  int month14 = (td.tm_mon + 1) / 4;        // month = 1..12
  int month15 = (td.tm_mon + 1) % 4;

  int year16 = td.tm_year % 100 % 64 / 16;  // year = 0..99
  int year17 = td.tm_year % 100 % 16 / 4;
  int year18 = td.tm_year % 100 % 4;

  int year_p19 = td.tm_year % 100 / 64 * 2
                 + (bits(mday11) + bits(mday12) + bits(mday13) + bits(month14) + bits(month15) + bits(year16) + bits(year17) + bits(year18)) % 2;

  int tco;
  switch (td.tm_sec % 20) {
    case  0: tco = marker;    break;
    case  1: tco = second1;   break;
    case  2: tco = 0;         break;
    case  3: tco = hour3;     break;
    case  4: tco = hour4;     break;
    case  5: tco = minute5;   break;
    case  6: tco = minute6;   break;
    case  7: tco = minute7;   break;
    case  8: tco = wday8;     break;
    case  9: tco = wday9;     break;

    case 10: tco = ampm_p10;  break;
    case 11: tco = mday11;    break;
    case 12: tco = mday12;    break;
    case 13: tco = mday13;    break;
    case 14: tco = month14;   break;
    case 15: tco = month15;   break;
    case 16: tco = year16;    break;
    case 17: tco = year17;    break;
    case 18: tco = year18;    break;
    case 19: tco = year_p19;  break;

    default: tco = 3;         break;
  }
  return tco;
}

int bits(int a)
{
  if(a == 0) return 0;
  if(a == 3) return 2;
  return 1;  // a == 1, 2
}

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// main
const unsigned int loop_period_ms(100);
      unsigned int loop_last_ms;
const int button_atom(39);    // GPIO39
const int button_atoms3(41);  // GPIO41

void setup()
{
  if (M5.getBoard() == m5::board_t::board_M5Atom) {
    // To avoid Wi-Fi issues, force GPIO0 to 0 while CH552 outputs 5V with its internal pullup.
    // https://twitter.com/wakwak_koba/status/1553162622479974400
    // https://www.facebook.com/groups/154504605228235/posts/699719300706760/
    pinMode(0, OUTPUT);
    digitalWrite(0, LOW);
  }

  // M5Unified
  auto cfg = M5.config();
  cfg.external_rtc = true;  // default=false. use Unit RTC.
  M5.begin(cfg);
  delay(3000);
  Serial.println();

  // FastLED
#if defined (CONFIG_IDF_TARGET_ESP32S3)  // FastLED requires strict constant for the pin number 
  if (M5.getBoard() == m5::board_t::board_M5AtomS3Lite) {
    FastLED.addLeds<WS2811, led_pin_atoms3lite, GRB>(leds, led_num);
  }
#else
  if (M5.getBoard() == m5::board_t::board_M5Atom) {
    FastLED.addLeds<WS2811, led_pin_atom, GRB>(leds, led_num);
  }
#endif
  FastLED.setBrightness(led_brightness);
  for (int i = 0; i < led_num; ++i) {
    leds[i] = 0;
  }

  // Unit RTC (External)
  if (rtcx.Begin(Wire) == 0) {
    rtcx_available = true;
    if (SetTimeFromRtcx(time_zone)) {
      localtime_valid = true;
    }
  }
  if (!localtime_valid) {
    Serial.print("RTC not valid: set the localtime temporarily\n");
    td.tm_year = 117;  // 2017 > 2016, getLocalTime() returns true
    td.tm_mon  = 0;    // January
    td.tm_mday = 1;
    td.tm_hour = 0;
    td.tm_min  = 0;
    td.tm_sec  = 0;
    struct timeval tv = { mktime(&td), 0 };
    settimeofday(&tv, NULL);
  }
  getLocalTime(&td);
  Serial.print(&td, "localtime: %A, %B %d %Y %H:%M:%S\n");
  // print sample: must be < 64
  //....:....1....:....2....:....3....:....4....:....5....:....6....
  //localtime: Wednesday, September 11 2021 11:10:46

  // WiFi start
  WiFiManager wm;  // blocking mode only

  // erase SSID/Key to force rewrite
  int button_pin = button_atom;
  if (M5.getBoard() == m5::board_t::board_M5AtomS3Lite) {
    button_pin = button_atoms3;
  }
  if (digitalRead(button_pin) == LOW) {
    wm.resetSettings();
  }

  // WiFi connect
  wm.setConfigPortalTimeout(wifi_config_portal_timeout_sec);
  wm.setAPCallback(WifiConfigModeCallback);
  wm.autoConnect();
  WiFi.setSleep(false);  // https://macsbug.wordpress.com/2021/05/02/buttona-on-m5stack-does-not-work-properly/
  wifi_retry_last_ms = millis() - wifi_retry_interval_ms;

  // NTP start
  NtpBegin(time_zone, ntp_server);

  // TCO start
  TcoInit();

  // clear button of erase SSID/Key
  M5.update();

  // loop control
  loop_last_ms = millis();
}

void loop()
{
  M5.update();
  LedShow();

  WifiCheck();
  if (RtcxUpdate(rtcx_available)) {
    localtime_valid = true;  // SNTP sync completed
  };

  // button: TCO monitor on/off
  if (M5.BtnA.wasReleased()) {
    led_enable = !led_enable;
  }

  // loop control
  unsigned int delay_ms(0);
  unsigned int elapse_ms = millis() - loop_last_ms;
  if (elapse_ms < loop_period_ms) {
    delay_ms = loop_period_ms - elapse_ms;
  }
  delay(delay_ms);
  loop_last_ms = millis();
//  Serial.printf("loop elapse = %dms\n", elapse_ms);  // for monitoring elapsed time
}
