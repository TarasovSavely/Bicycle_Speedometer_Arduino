// V1.1 Исправлены баги: (25 августа 2021 г)
// 1. Неправильное отображение средней скорости при достаточно большом расстоянии
// 2. Мигание дисплея при использовании второго режима отображнеия скорости
// 3. Повышена стабильность показаний заряда батареи
// V1.0 Первоначальная версия (2021 г)
// Автор: Тарасов Савелий, 2021 г

/////////////////////STTINGS/////////////////////

// EEPROM

#define eepr_key 100 // For reset settings, change this number (0-255)
#define addr_struct 0 // (0-1023) Note: 1000+-5 reserved 

// ENCODER

#define DIR 0 // Rotation direction of the encoder (0-1)

// BICYCLE

#define wheel_len 201.7f // In centimeters

// ABOUT

#define ms_for_stop 3000 // Above is considered a stop

/////////////////////PINS/////////////////////

// MOSFET

#define mos_pin 11

// TM1637

#define CLK 6
#define DIO 7

// ENCODER

#define S1 5
#define S2 4
#define KEY_interrupt 1 // Number interrupt for encoder button

// HALL SENSOR

#define HALL_interrupt 0 // Number interrupt for Hall sensor 

/////////////////////FOR DEVELOPER/////////////////////

#define QUA_MENU 9
#define QUA_S_MENU 6
#define TIMEOUT_CLICK 700
#define DEBOUNCE 50
#define PERIOD_UPDATE_MENU 100

#include <U8glib.h>
#include <microDS3231.h>
#include <EEPROM.h>
#include <GyverPower.h>
#include <GyverHacks.h>
#include <TM1637_Tarasov.h>
#include "encMinim.h"

U8GLIB_SSD1306_128X32 u8g;
TM1637_Tarasov disp (CLK, DIO);
MicroDS3231 rtc;
encMinim enc(S2, S1, (KEY_interrupt + 2), DIR);

struct {
  byte key_check;
  byte brig;
  uint32_t all_seconds;
  uint32_t all_centimeters;
  uint32_t number_rewrite;
  bool speed_mode;
  bool percent_mode;
  byte percent_to_mode_energy_saving;
} settings;

volatile uint32_t last_isr = 1;
volatile uint32_t cur_tenth_millimeters = 1;
volatile uint32_t cur_milliseconds = 1;
volatile int cur_max_speed = 0;
volatile int cur_hundredth_kmph = 0;

int count_click_enc = 0;
int menu_state = 1;

uint32_t tmr_click_enc;

bool oled_draw_flag = true; // oled_redraw_flag
bool enerjy_save;

int vbat;

uint32_t get_bat (int n = 30);

void setup() {
  power.setSleepMode (POWERDOWN_SLEEP);

  pinMode (HALL_interrupt + 2, INPUT_PULLUP);
  pinMode (mos_pin, OUTPUT);

  digitalWrite (mos_pin, 1);

  delay (100);

  u8g = U8GLIB_SSD1306_128X32(U8G_I2C_OPT_NONE);

  u8g.firstPage(); do {} while ( u8g.nextPage() ); // Clear OLED-display
  delay (200);

  last_isr = millis ();
  attachInterrupt (KEY_interrupt, but_enc_isr, FALLING); // Attach interrupt for encoder button
  attachInterrupt (HALL_interrupt, hall_isr, FALLING); // Attach interrupt for Holl sensor


  disp.init (); // Initialisation seven-segment display
  disp.clear ();       // Clear display
  disp.brightness(7);  // Set maximal brightness

  EEPROM.get (addr_struct, settings); // Get a structure from non-volatile memory

  if (settings.key_check != eepr_key) { // If this a first start-up
    first_start_up ();
  }

  disp.brightness(settings.brig);

  if (settings.number_rewrite > 100000UL) {
    overwrite ();
  }

  if (rtc.lostPower()) {  //  on loss of power
    lost_power ();
  }

  restoreConstant(1000);

  tmr_click_enc = millis ();
  oled_draw_flag = true;
  enerjy_save = false;
  last_isr = 1;
  cur_tenth_millimeters = 1;
  cur_milliseconds = 1;
  cur_max_speed = 0;
  cur_hundredth_kmph = 0;
}

void loop() {
  get_bat ();
  check_bat ();
  processing_enc ();
  if (!enerjy_save) {
    draw_menu ();
  }
}

uint32_t get_bat (int n = 30) {
  static uint32_t tmr = 0;
  static int last_n = 0;
  static uint32_t sum_avg = 0;
  if (millis () - tmr > 200 || n > last_n) {
    tmr = millis ();
    sum_avg = 0;
    for (int q = 0; q < n; q++) {
      enc.tick ();
      sum_avg += getVCC();
    }
    vbat = avg (lithiumPercent((uint32_t)sum_avg / n));
    last_n = n;
  }
  return sum_avg / last_n;
}

void check_bat () {
  static uint32_t tmr;
  static bool flag;
  if (millis () - tmr > 10000 && !flag) {
    tmr = millis ();
    get_bat ();

    if (vbat < settings.percent_to_mode_energy_saving) {
      get_bat (70);
      if (vbat < settings.percent_to_mode_energy_saving) {
        warning_low_bat ();
        flag = true;
      }
    }
  }
  if (millis () - tmr > 120000 && flag) {
    tmr = millis ();
    warning_low_bat ();
    get_bat (70);
    if (vbat > settings.percent_to_mode_energy_saving) {
      flag = false;
    }
  }
}
void warning_low_bat () {
  oled_draw_flag = true;

  u8g.firstPage ();
  do {
    u8g.setFont(rusMax20);
    u8g.setPrintPos(0, 20);
    u8g.print(F("НИЗИКИЙ ЗАРЯД!"));
  } while ( u8g.nextPage() );

  disp.point (true);
  disp.write_int (8888, 0);

  waiting_push ();

}

void draw_menu () {
  static uint32_t tmr;
  if (millis () - tmr > PERIOD_UPDATE_MENU) {
    tmr = millis ();
    switch (menu_state) {
      case 1: item_1 ();
        break;
      case 2: item_2 ();
        break;
      case 3: item_3 ();
        break;
      case 4: item_4 ();
        break;
      case 5: item_5 ();
        break;
      case 6: item_6 ();
        break;
      case 7: item_7 ();
        break;
      case 8: item_8 ();
        break;
      case 9: item_9 ();
        break;
    }
  }
}

void item_1 () {
  disp.point (true);
  if (settings.speed_mode) {
    uint32_t new_speed = (((float)36 * wheel_len) / ((uint32_t)millis () - last_isr));
    new_speed *= 100;
    if (new_speed + 100 <= cur_hundredth_kmph) {
      disp.write_int (new_speed, 1);
    } else {
      disp.write_int (cur_hundredth_kmph, 1);
    }
  } else {
    disp.write_int (cur_hundredth_kmph, 1);
  }

  if (oled_draw_flag) {
    oled_draw_flag = false;
    u8g.firstPage();
    do {
      u8g.setFont(rusMax20);
      u8g.setPrintPos(0, 20);
      u8g.print(F("Скорость"));
    } while ( u8g.nextPage() );
  }
}
void item_2 () {
  disp.point (false);
  disp.write_int ((uint32_t)((uint32_t)settings.all_centimeters + ((uint32_t)cur_tenth_millimeters / 100)) / 100000, 1);
  if (oled_draw_flag) {
    oled_draw_flag = false;
    u8g.firstPage();
    do {
      u8g.setFont(rusMax20);
      u8g.setPrintPos(0, 20);
      u8g.print(F("Общий пробег"));
    } while ( u8g.nextPage() );
  }
}

void item_3 () {
  disp.point (true);
  disp.write_int ((uint32_t)cur_tenth_millimeters / 100000UL, 1);
  if (oled_draw_flag) {
    oled_draw_flag = false;
    u8g.firstPage();
    do {
      u8g.setFont(rusMax20);
      u8g.setPrintPos(0, 20);
      u8g.print(F("Расстояние"));
    } while ( u8g.nextPage() );
  }
}

void item_4 () {
  disp.point (true);
  disp.write_int (cur_max_speed, 1);
  if (oled_draw_flag) {
    oled_draw_flag = false;
    u8g.firstPage();
    do {
      u8g.setFont(rusMax20);
      u8g.setPrintPos(0, 20);
      u8g.print(F("Мкс. скорость"));
    } while ( u8g.nextPage() );
  }
}

void item_5 () {
  disp.point (true);
  disp.write_int ((uint64_t)((uint64_t)cur_tenth_millimeters * 36) / cur_milliseconds, 1);
  if (oled_draw_flag) {
    oled_draw_flag = false;
    u8g.firstPage();
    do {
      u8g.setFont(rusMax20);
      u8g.setPrintPos(0, 20);
      u8g.print(F("Ср. скорость"));
    } while ( u8g.nextPage() );
  }
}

void item_6 () {

  disp.point (true);

  int h = (uint32_t)cur_milliseconds / 3600000UL;
  int m = (uint32_t)(cur_milliseconds % 3600000) / 60000UL;

  disp.write_int ( h * 100 + m, 1);

  if (oled_draw_flag) {
    oled_draw_flag = false;
    u8g.firstPage();
    do {
      u8g.setFont(rusMax20);
      u8g.setPrintPos(0, 20);
      u8g.print(F("Время поездки"));
    } while ( u8g.nextPage() );
  }
}

void item_7 () {

  disp.point (true);
  disp.write_int (rtc.getHours () * 100 + rtc.getMinutes (), 1);

  if (oled_draw_flag) {
    oled_draw_flag = false;
    u8g.firstPage();
    do {
      u8g.setFont(rusMax20);
      u8g.setPrintPos(0, 20);
      u8g.print(F("Часы"));
    } while ( u8g.nextPage() );
  }
}

void item_8 () {

  disp.point (false);
  disp.write_int(((uint32_t)settings.all_seconds + ((uint32_t)cur_milliseconds / 1000)) / 3600, 1);

  if (oled_draw_flag) {
    oled_draw_flag = false;
    u8g.firstPage();
    do {
      u8g.setFont(rusMax20);
      u8g.setPrintPos(0, 20);
      u8g.print(F("Пробег (ч.)"));
    } while ( u8g.nextPage() );
  }
}

void item_9 () {
  if (settings.percent_mode) {
    get_bat ();
    disp.point (false);
    disp.write_int(vbat, 0);
  } else {
    disp.point (true);
    disp.write_int((uint32_t)get_bat () / 10, 1);
  }

  if (oled_draw_flag) {
    oled_draw_flag = false;
    u8g.firstPage();
    do {
      u8g.setFont(rusMax20);
      u8g.setPrintPos(0, 20);
      u8g.print(F("Заряд батареи"));
    } while ( u8g.nextPage() );
  }
}

void settings_menu () {
  int select = 1;

  disp.clear ();
  disp.point (false);

  u8g.firstPage();
  do {
    u8g.setFont(rusMax20);
    u8g.setPrintPos(0, 20);
    u8g.print(F("Настройки"));
  } while ( u8g.nextPage() );

  waiting_push ();
  while (1) {
    enc._encState = 3;

    while (!enc.isClick ()) {
      enc.tick ();
      if (enc.isTurn ()) {
        if (enc.isLeft ()) select--;
        if (enc.isRight ()) select++;
        if (select < 1) select += QUA_S_MENU;
        if (select > QUA_S_MENU) select -= QUA_S_MENU;
        enc.resetState ();

        u8g.firstPage();
        do {
          u8g.setFont(rus4x6);

          select--;

          if (select < 1) select += QUA_S_MENU;
          if (select > QUA_S_MENU) select -= QUA_S_MENU;

          u8g.setPrintPos(0, 12);
          draw_settings_menu (select, false);

          select++;

          if (select < 1) select += QUA_S_MENU;
          if (select > QUA_S_MENU) select -= QUA_S_MENU;

          u8g.setPrintPos(0, 20);
          draw_settings_menu (select, true);


          select++;

          if (select < 1) select += QUA_S_MENU;
          if (select > QUA_S_MENU) select -= QUA_S_MENU;

          u8g.setPrintPos(0, 28);
          draw_settings_menu (select, false);

          select--;

          if (select < 1) select += QUA_S_MENU;
          if (select > QUA_S_MENU) select -= QUA_S_MENU;

        } while ( u8g.nextPage() );
      }
    }
    if (select == 6) {
      settings.number_rewrite++;
      EEPROM.put (addr_struct, settings);
      oled_draw_flag = true;
      break;
    }
    switch (select) {
      case 1: get_brightness (); break;
      case 2: get_percent_to_mode_energy_saving (); break;
      case 3: get_rtc_time (); break;
      case 4: get_flag_speed (); break;
      case 5: get_flag_percent (); break;
    }
    disp.clear ();
  }
}

void draw_settings_menu (int num, bool select) {
  if (select) {
    u8g.print(F(">"));
  } else {
    u8g.print(F(" "));
  }
  switch (num) {
    case 1: u8g.print(F("Яркость")); break;
    case 2: u8g.print(F("Режим энерго/сб.")); break;
    case 3: u8g.print(F("Время")); break;
    case 4: u8g.print(F("Отобр. скорости")); break;
    case 5: u8g.print(F("Отобр. заряда")); break;
    case 6: u8g.print(F("Выйти")); break;
  }
}

void processing_enc () {
  enc.tick ();

  if (millis () - tmr_click_enc >= TIMEOUT_CLICK && count_click_enc) {
    if (count_click_enc == 1) {
      enerjy_save = !enerjy_save;
      if (enerjy_save) {
        disp.clear ();
        u8g.firstPage ();
        do {} while (u8g.nextPage ());
      } else {
        oled_draw_flag = true;
      }
    }
    if (count_click_enc == 2) {
      sleep_and_save ();
      digitalWrite (mos_pin, 1);
    }
    if (count_click_enc == 3) {
      settings_menu ();
    }
    count_click_enc = 0;
  }

  if (enc.isTurn ()) {
    if (enc.isLeft ()) {
      menu_state--;
      oled_draw_flag = true;
    } if (enc.isRight ()) {
      menu_state++;
      oled_draw_flag = true;
    }
    if (menu_state < 1) menu_state = QUA_MENU;
    if (menu_state > QUA_MENU) menu_state = 1;

    enc.resetState ();

    count_click_enc = 0;
  }

  if (enc.isClick ()) {

    if (millis () - tmr_click_enc < TIMEOUT_CLICK || !count_click_enc) {
      tmr_click_enc = millis ();
      count_click_enc++;
    }

  }
}

void sleep_and_save () {
  detachInterrupt (HALL_interrupt);

  digitalWrite (mos_pin, 0);
  digitalWrite (CLK, 1);
  digitalWrite (DIO, 1);

  settings.number_rewrite++;
  settings.all_centimeters += cur_tenth_millimeters / 100;
  settings.all_seconds += cur_milliseconds / 1000;

  cur_tenth_millimeters = 1;
  cur_milliseconds = 1;
  cur_max_speed = 0;
  cur_hundredth_kmph = 0;

  count_click_enc = 0;
  menu_state = 1;


  EEPROM.put (addr_struct, settings);
  power.sleep (SLEEP_FOREVER);
  asm volatile ("jmp 0");
}

void first_start_up () { // Print welcome, get settings

  settings.key_check = eepr_key;
  settings.number_rewrite = 1;
  settings.brig = 7;
  settings.percent_to_mode_energy_saving = 25;
  settings.all_seconds = 0;
  settings.all_centimeters = 0;
  settings.percent_mode = true;
  settings.speed_mode = true;

  u8g.firstPage();
  do {
    u8g.setFont(rus4x6);
    u8g.setPrintPos(0, 20);
    u8g.print(F("Установка настроек"));
  } while ( u8g.nextPage() );

  waiting_push ();
  get_brightness ();
  get_percent_to_mode_energy_saving ();
  get_rtc_time ();
  EEPROM.put (addr_struct, settings);
}

void hall_isr () {
  uint32_t cur_isr = millis ();
  uint32_t diff = millis () - last_isr;
  if (diff < ms_for_stop && diff > DEBOUNCE) {
    cur_milliseconds += diff;
    cur_tenth_millimeters += wheel_len * 100;
    cur_hundredth_kmph = ((float)3600 * wheel_len) / diff;
    cur_max_speed = max (cur_max_speed, cur_hundredth_kmph);
  }
  last_isr = cur_isr;
}

void but_enc_isr () {

}

void overwrite () { // Print warning and beg change adress
  int new_addr = addr_struct + int (sizeof (settings));
  print_overwrite (new_addr);
  while (!enc.isClick ()) {
    enc.tick ();
    if (enc.isTurn ()) {
      if (enc.isLeft ()) new_addr -= int (sizeof (settings));
      if (enc.isRight ()) new_addr += int (sizeof (settings));
      if (enc.isLeftH ()) new_addr -= (int (sizeof (settings)) * 2);
      if (enc.isRightH ())new_addr += (int (sizeof (settings)) * 2);
      enc.resetState ();
      if (new_addr < 0 || new_addr > (1023 - int (sizeof (settings)))) new_addr = 0;
      print_overwrite (new_addr);
    }
  }
  EEPROM.get (addr_struct, settings);
  settings.number_rewrite = 1;
  EEPROM.put (new_addr, settings);
}

void print_overwrite (int new_addr) {
  u8g.firstPage();
  do {
    u8g.setFont(rus4x6);
    u8g.setPrintPos(0, 12);
    u8g.print(F("Кол-во перезаписей >100000 - "));
    u8g.setPrintPos(0, 20);
    u8g.print(settings.number_rewrite);
    u8g.print(F(" Новый адрес - "));
    u8g.setPrintPos(0, 28);
    u8g.print(new_addr);
  } while ( u8g.nextPage() );
}

void lost_power () { // Print warning and get time
  u8g.firstPage();
  do {
    u8g.setFont(rus4x6);
    u8g.setPrintPos(0, 20);
    u8g.print(F("Питание DS3231 было потеряно"));
  } while ( u8g.nextPage() );
  waiting_push ();
  get_rtc_time ();
}
void print_brightness () {
  u8g.firstPage();
  do {
    u8g.setFont(rus4x6);
    u8g.setPrintPos(0, 20);
    u8g.print (F("Яркость "));
    u8g.print (settings.brig);
  } while ( u8g.nextPage() );
}

void print_mode_enerjy_save () {
  u8g.firstPage();
  do {
    u8g.setFont(rus4x6);
    u8g.setPrintPos(0, 20);
    u8g.print(F("Режим энергосбереж. при "));
    u8g.print(settings.percent_to_mode_energy_saving);
    u8g.print(F("% "));
  } while ( u8g.nextPage() );
}

void print_text_time () {
  u8g.firstPage();
  do {
    u8g.setFont(rus4x6);
    u8g.setPrintPos(0, 20);
    u8g.print(F("Время"));
  } while ( u8g.nextPage() );
}

void waiting_push () {
  while (!enc.isClick ()) {
    enc.tick ();
    if (enc.isTurn ()) {
      enc.resetState ();
    }
  }
}

void get_brightness () {
  disp.point (true);
  disp.write_int(8888, 0);

  print_brightness ();

  while (!enc.isClick ()) {

    enc.tick();

    if (enc.isTurn()) {

      if (enc.isLeft () && settings.brig != 0) settings.brig--;
      if (enc.isRight () && settings.brig != 7) settings.brig++;
      enc.resetState ();

      disp.brightness (settings.brig);
      disp.write_int(8888, 0);

      print_brightness ();

    }
  }

  disp.clear ();
}

void get_flag_speed () {
  disp.point (false);
  disp.write_int(settings.speed_mode, 0);

  settings.speed_mode = !settings.speed_mode;
  enc._encState = 3;

  while (!enc.isClick ()) {

    enc.tick();

    if (enc.isTurn()) {

      settings.speed_mode = !settings.speed_mode;
      disp.write_int(settings.speed_mode, 0);

      enc.resetState ();

      u8g.firstPage();
      do {
        u8g.setFont(rus4x6);
        u8g.setPrintPos(0, 20);
        u8g.print(F("Режим отображ. скорости"));

      } while ( u8g.nextPage() );

    }
  }

  disp.clear ();
}

void get_flag_percent () {

  disp.point (false);
  disp.write_int (settings.percent_mode, 0);

  settings.percent_mode = !settings.percent_mode;
  enc._encState = 3;

  while (!enc.isClick ()) {

    enc.tick();

    if (enc.isTurn()) {

      settings.percent_mode = !settings.percent_mode;
      disp.write_int(settings.percent_mode, 0);

      enc.resetState ();

      u8g.firstPage();
      do {
        u8g.setFont(rus4x6);
        u8g.setPrintPos(0, 20);
        u8g.print(F("Режим отображ. заряда"));

      } while ( u8g.nextPage() );

    }
  }

  disp.clear ();
}

void get_percent_to_mode_energy_saving () {

  disp.point (false);
  disp.write_int (settings.percent_to_mode_energy_saving, 0);

  print_mode_enerjy_save ();

  while (!enc.isClick ()) {
    enc.tick ();
    if (enc.isTurn()) {

      if (enc.isLeft () && settings.percent_to_mode_energy_saving != 0) settings.percent_to_mode_energy_saving--;
      if (enc.isRight () && settings.percent_to_mode_energy_saving != 100) settings.percent_to_mode_energy_saving++;
      enc.resetState ();

      disp.write_int (settings.percent_to_mode_energy_saving, 0);

      u8g.firstPage();
      do {
        print_mode_enerjy_save ();
      } while ( u8g.nextPage() );

    }
  }
  disp.clear ();
}

void get_rtc_time () {
  int hrs = rtc.getHours();
  int mns = rtc.getMinutes();

  disp.point (true);
  disp.write_int (hrs * 100 + mns, 1);


  print_text_time ();

  while (!enc.isClick ()) {
    enc.tick ();
    if (enc.isTurn()) {

      if (enc.isLeft ()) mns = mns - 1;
      if (enc.isRight ()) mns = mns + 1;
      if (enc.isLeftH ()) hrs = hrs - 1;
      if (enc.isRightH ()) hrs = hrs + 1;

      if (hrs > 23) hrs = 0;
      if (hrs < 0) hrs = 23;

      if (mns > 59) mns = 0;
      if (mns < 0) mns = 59;

      enc.resetState ();

      disp.write_int (hrs * 100 + mns, 1);

    }
  }
  rtc.setTime(0, mns, hrs, 1, 1, 2001);

  disp.clear ();
}

uint32_t avg(uint32_t value) {
  const uint32_t bufSize = 60; // количество значений
  static uint32_t buf[bufSize]; // массив из последних значений
  static uint32_t pos = 0; // текущая позиция
  static uint32_t sum = 0; // сумма всех значений
  uint32_t avg = 0; // среднее значение
  static bool first = true;

  if (first) {
    first = false;
    sum = 0;
    for (int i = 0; i < bufSize; i++) {
      buf [i] = value;
      sum += value;
    }
  }
  buf[pos] = value; // добавление нового значения
  sum += value; // его добавление в сумму
  avg = sum / bufSize; // вычисление среднего значения
  // компенсация погрешности целочисленного деления
  if ((sum % bufSize) >= (bufSize / 2)) avg++;
  // вычисление позиции следующего элемента
  pos++;
  if (pos == bufSize) pos = 0;
  sum -= buf[pos]; // элемент, который будет перезаписан
  // в следующий раз заранее вычитается
  // из суммы
  return avg;
}
