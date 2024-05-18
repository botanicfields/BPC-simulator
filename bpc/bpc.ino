// BPC Simulator on M5ATOM
#include <M5Unified.h>
#include <Ticker.h>
#include <WiFi.h>

// for Wi-Fi
const char* ssid     = "SSID";
const char* password = "PASSWORD";

// for NTP
const char* time_zone  = "CST-8";
const char* ntp_server = "pool.ntp.org";

// for TCO(Time Code Output)
const int bpc_frequency(68500);  // 68.5kHz
struct tm       td;  // time of day: year, month, day, day of week, hour, minute, second
struct timespec ts;  // time spec: second, nano-second

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// TCO(Time Code Output)
Ticker tk;
const int ticker_interval_ms(100);
const int marker(-1);  // marker code TcoValue() returns

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

  // start Ticker for TCO
  tk.attach_ms(ticker_interval_ms, TcoGen);
}

void TcoGen()
{
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
}

void Tco000ms()
{
  if (TcoValue() == marker) {
    TcOff();
    Serial.print(&td, "\n%A %B %d %Y %H:%M:%S\n");
  } 
  else {
    TcOn();
  }
}

void Tco100ms()
{
  if (TcoValue() == 0) {
    TcOff();
    Serial.print("0");
  }
}

void Tco200ms()
{
  if (TcoValue() == 1) {
    TcOff();
    Serial.print("1");
  }
}

void Tco300ms()
{
  if (TcoValue() == 2) {
    TcOff();
    Serial.print("2");
  }
}

void Tco400ms()
{
  if (TcoValue() == 3) {
    TcOff();
    Serial.print("3");
  }
}

void TcOn()
{
  ledcWrite(ledc_channel, ledc_duty_on);
}

void TcOff()
{
  ledcWrite(ledc_channel, ledc_duty_off);
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
                 + (second1 + hour3 + hour4 + minute5 + minute6 + minute7 + wday8 + wday9) % 2;
  
  int mday11 = td.tm_mday / 16;             // day of month = 1..31
  int mday12 = td.tm_mday % 16 / 4;
  int mday13 = td.tm_mday % 4;

  int month14 = (td.tm_mon + 1) / 4;        // month = 1..12
  int month15 = (td.tm_mon + 1) % 4;

  int year16 = td.tm_year % 100 % 64 / 16;  // year = 0..99
  int year17 = td.tm_year % 100 % 16 / 4;
  int year18 = td.tm_year % 100 % 4;

  int year_p19 = td.tm_year % 100 / 64 * 2
                 + (mday11 + mday12 + mday13 + month14 + month15 + year16 + year17 + year18) % 2;

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

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// main
const unsigned int loop_period_ms(1);

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
  M5.begin(cfg);
  delay(3000);
  Serial.println();

  // WiFi connect
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");

  // NTP start
  configTzTime(time_zone, ntp_server);
  Serial.printf("NtpBegin: config TZ time = %s\n", time_zone);
  getLocalTime(&td);
  Serial.print(&td, "localtime: %A, %B %d %Y %H:%M:%S\n");

  // TCO start
  TcoInit();
}

void loop()
{
  delay(loop_period_ms);
}
