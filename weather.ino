// weather.ino - Aura Weather Display with Theme Customization
// Version: 2.3.8 (Fork)
// Original: Surrey-Homeware/Aura  
// Fork adds: Location animation now runs on all screens

#include <Arduino.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include "esp_system.h"
#include "translations.h"
#include "theme_config.h"
#include "web_server.h"

#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS
#define LCD_BACKLIGHT_PIN 21

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))

#define LATITUDE_DEFAULT "51.5074"
#define LONGITUDE_DEFAULT "-0.1278"
#define LOCATION_DEFAULT "London"
#define DEFAULT_CAPTIVE_SSID "Aura"
#define UPDATE_INTERVAL 600000UL  // 10 minutes

// Night mode starts at 10pm and ends at 6am
#define NIGHT_MODE_START_HOUR 22
#define NIGHT_MODE_END_HOUR 6

LV_FONT_DECLARE(lv_font_montserrat_latin_12);
LV_FONT_DECLARE(lv_font_montserrat_latin_14);
LV_FONT_DECLARE(lv_font_montserrat_latin_16);
LV_FONT_DECLARE(lv_font_montserrat_latin_20);
LV_FONT_DECLARE(lv_font_montserrat_latin_42);

static Language current_language = LANG_EN;

// Font selection based on language
const lv_font_t* get_font_12() {
  return &lv_font_montserrat_latin_12;
}

const lv_font_t* get_font_14() {
  return &lv_font_montserrat_latin_14;
}

const lv_font_t* get_font_16() {
  return &lv_font_montserrat_latin_16;
}

const lv_font_t* get_font_20() {
  return &lv_font_montserrat_latin_20;
}

const lv_font_t* get_font_42() {
  return &lv_font_montserrat_latin_42;
}

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
uint32_t draw_buf[DRAW_BUF_SIZE / 4];
int x, y, z;

// Preferences
static Preferences prefs;
static ThemeManager themeMgr;
static bool use_fahrenheit = false;
static bool use_24_hour = false; 
static bool use_night_mode = false;
static char latitude[16] = LATITUDE_DEFAULT;
static char longitude[16] = LONGITUDE_DEFAULT;
static String location = String(LOCATION_DEFAULT);
static char dd_opts[512];
static JsonArray geoResults;  // Populated by do_geocode_query(), used by UI callbacks

// Screen dimming variables
static bool night_mode_active = false;
static bool temp_screen_wakeup_active = false;
static lv_timer_t *temp_screen_wakeup_timer = nullptr;
static int last_checked_hour = -1;  // Cache for night mode optimization

// UI components
static lv_obj_t *lbl_today_temp;
static lv_obj_t *lbl_today_feels_like;
static lv_obj_t *lbl_forecast;
static lv_obj_t *lbl_location_scroll;
static lv_timer_t *scroll_animation_timer = nullptr;
static lv_obj_t *img_today_icon;
static lv_obj_t *box_daily;
static lv_obj_t *box_hourly;
static lv_obj_t *box_wind;
static lv_obj_t *box_precipitation;
static lv_obj_t *box_pressure;
static lv_obj_t *box_sun;
static lv_obj_t *box_moon;
static lv_obj_t *box_temp_chart;

// Wind screen labels
static lv_obj_t *lbl_wind_speed_title;
static lv_obj_t *lbl_wind_speed_value;
static lv_obj_t *lbl_wind_direction_title;
static lv_obj_t *lbl_wind_direction_value;
static lv_obj_t *lbl_wind_gusts_title;
static lv_obj_t *lbl_wind_gusts_value;
static lv_obj_t *lbl_uv_index_title;
static lv_obj_t *lbl_uv_index_value;
static lv_obj_t *lbl_cloud_cover_title;
static lv_obj_t *lbl_cloud_cover_value;
static lv_obj_t *lbl_pollen_count_title;
static lv_obj_t *lbl_pollen_count_value;
static lv_obj_t *lbl_air_quality_title;
static lv_obj_t *lbl_air_quality_value;

// Precipitation screen labels
static lv_obj_t *lbl_precipitation_title;
static lv_obj_t *lbl_precipitation_value;
static lv_obj_t *lbl_humidity_title;
static lv_obj_t *lbl_humidity_value;
static lv_obj_t *lbl_dew_point_title;
static lv_obj_t *lbl_dew_point_value;
static lv_obj_t *lbl_visibility_title;
static lv_obj_t *lbl_visibility_value;
static lv_obj_t *lbl_pressure_title;
static lv_obj_t *lbl_pressure_value;

// Sun & Moon screen labels (7 days)
static lv_obj_t *lbl_sun_day[7];
static lv_obj_t *lbl_sunrise[7];
static lv_obj_t *lbl_sunset[7];

// Moon phase screen (7 days)
static lv_obj_t *lbl_moon_day[7];
static lv_obj_t *lbl_moon_phase_text[7];
static lv_obj_t *img_moon_phase[7];

// Temperature chart
static lv_obj_t *temp_chart;
static lv_chart_series_t *temp_series_high;
static lv_chart_series_t *temp_series_low;
static lv_obj_t *lbl_temp_high_val;
static lv_obj_t *lbl_temp_low_val;
static lv_obj_t *lbl_day_letters[7];

static lv_obj_t *lbl_daily_day[7];
static lv_obj_t *lbl_daily_high[7];
static lv_obj_t *lbl_daily_low[7];
static lv_obj_t *img_daily[7];
static lv_obj_t *lbl_hourly[7];
static lv_obj_t *lbl_precipitation_probability[7];
static lv_obj_t *lbl_hourly_temp[7];
static lv_obj_t *img_hourly[7];
static lv_obj_t *lbl_loc;
static lv_obj_t *loc_ta;
static lv_obj_t *results_dd;
static lv_obj_t *btn_close_loc;
static lv_obj_t *btn_close_obj;
static lv_obj_t *kb;
static lv_obj_t *settings_win;
static lv_obj_t *location_win = nullptr;
static lv_obj_t *unit_switch;
static lv_obj_t *clock_24hr_switch;
static lv_obj_t *night_mode_switch;
static lv_obj_t *language_dropdown;
static lv_obj_t *lbl_clock;

// Weather icons
LV_IMG_DECLARE(icon_blizzard);
LV_IMG_DECLARE(icon_blowing_snow);
LV_IMG_DECLARE(icon_clear_night);
LV_IMG_DECLARE(icon_cloudy);
LV_IMG_DECLARE(icon_drizzle);
LV_IMG_DECLARE(icon_flurries);
LV_IMG_DECLARE(icon_haze_fog_dust_smoke);
LV_IMG_DECLARE(icon_heavy_rain);
LV_IMG_DECLARE(icon_heavy_snow);
LV_IMG_DECLARE(icon_isolated_scattered_tstorms_day);
LV_IMG_DECLARE(icon_isolated_scattered_tstorms_night);
LV_IMG_DECLARE(icon_mostly_clear_night);
LV_IMG_DECLARE(icon_mostly_cloudy_day);
LV_IMG_DECLARE(icon_mostly_cloudy_night);
LV_IMG_DECLARE(icon_mostly_sunny);
LV_IMG_DECLARE(icon_partly_cloudy);
LV_IMG_DECLARE(icon_partly_cloudy_night);
LV_IMG_DECLARE(icon_scattered_showers_day);
LV_IMG_DECLARE(icon_scattered_showers_night);
LV_IMG_DECLARE(icon_showers_rain);
LV_IMG_DECLARE(icon_sleet_hail);
LV_IMG_DECLARE(icon_snow_showers_snow);
LV_IMG_DECLARE(icon_strong_tstorms);
LV_IMG_DECLARE(icon_sunny);
LV_IMG_DECLARE(icon_tornado);
LV_IMG_DECLARE(icon_wintry_mix_rain_snow);

// Moon Phase Icons (8 phases)
LV_IMG_DECLARE(icon_new_moon);
LV_IMG_DECLARE(icon_waxing_crescent);
LV_IMG_DECLARE(icon_first_quarter);
LV_IMG_DECLARE(icon_waxing_gibbous);
LV_IMG_DECLARE(icon_full_moon);
LV_IMG_DECLARE(icon_waning_gibbous);
LV_IMG_DECLARE(icon_third_quarter);
LV_IMG_DECLARE(icon_waning_crescent);

// Weather Images
LV_IMG_DECLARE(image_blizzard);
LV_IMG_DECLARE(image_blowing_snow);
LV_IMG_DECLARE(image_clear_night);
LV_IMG_DECLARE(image_cloudy);
LV_IMG_DECLARE(image_drizzle);
LV_IMG_DECLARE(image_flurries);
LV_IMG_DECLARE(image_haze_fog_dust_smoke);
LV_IMG_DECLARE(image_heavy_rain);
LV_IMG_DECLARE(image_heavy_snow);
LV_IMG_DECLARE(image_isolated_scattered_tstorms_day);
LV_IMG_DECLARE(image_isolated_scattered_tstorms_night);
LV_IMG_DECLARE(image_mostly_clear_night);
LV_IMG_DECLARE(image_mostly_cloudy_day);
LV_IMG_DECLARE(image_mostly_cloudy_night);
LV_IMG_DECLARE(image_mostly_sunny);
LV_IMG_DECLARE(image_partly_cloudy);
LV_IMG_DECLARE(image_partly_cloudy_night);
LV_IMG_DECLARE(image_scattered_showers_day);
LV_IMG_DECLARE(image_scattered_showers_night);
LV_IMG_DECLARE(image_showers_rain);
LV_IMG_DECLARE(image_sleet_hail);
LV_IMG_DECLARE(image_snow_showers_snow);
LV_IMG_DECLARE(image_strong_tstorms);
LV_IMG_DECLARE(image_sunny);
LV_IMG_DECLARE(image_tornado);
LV_IMG_DECLARE(image_wintry_mix_rain_snow);

void create_ui();
void fetch_and_update_weather();
void create_settings_window();
static void screen_event_cb(lv_event_t *e);
static void settings_event_handler(lv_event_t *e);
const lv_img_dsc_t *choose_image(int wmo_code, int is_day);
const lv_img_dsc_t *choose_icon(int wmo_code, int is_day);

// Moon phase enum for type-safe operations
enum MoonPhase {
  MOON_NEW,
  MOON_WAXING_CRESCENT,
  MOON_FIRST_QUARTER,
  MOON_WAXING_GIBBOUS,
  MOON_FULL,
  MOON_WANING_GIBBOUS,
  MOON_THIRD_QUARTER,
  MOON_WANING_CRESCENT
};

// Moon phase function declarations
static MoonPhase calculate_moon_phase(int year, int month, int day);
static const char* get_moon_phase_abbr(MoonPhase phase);
static void set_moon_icon(lv_obj_t* img, MoonPhase phase);

// Screen state management
struct ScreenState {
  int current_screen;
  bool showing_forecast;
  lv_timer_t *scroll_timer;
  lv_obj_t *boxes[7];  // References to all screen boxes
} screen_state = {0, true, nullptr, {nullptr}};

// Helper function: Create standard box with common styling
static lv_obj_t* create_standard_box(lv_obj_t* parent) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_set_size(box, 220, 180);
  lv_obj_align(box, LV_ALIGN_TOP_LEFT, 10, 135);
  lv_obj_set_style_bg_color(box, lv_color_hex(themeMgr.getBoxBg()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(box, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(box, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(box, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(box, cycle_screens_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
  return box;
}

// Screen dimming functions
bool night_mode_should_be_active();
void activate_night_mode();
void deactivate_night_mode();
void check_for_night_mode();
void handle_temp_screen_wakeup_timeout(lv_timer_t *timer);


int day_of_week(int y, int m, int d) {
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

String hour_of_day(int hour) {
  const LocalizedStrings* strings = get_strings(current_language);
  if(hour < 0 || hour > 23) return String(strings->invalid_hour);

  if (use_24_hour) {
    if (hour < 10)
      return String("0") + String(hour);
    else
      return String(hour);
  } else {
    if(hour == 0) return String("12") + strings->am;
    
    bool isMorning = (hour < 12);
    String suffix = isMorning ? strings->am : strings->pm;

    int displayHour = hour % 12;
    if(displayHour == 0) displayHour = 12;  // Fix midnight/noon edge case

    return String(displayHour) + suffix;
  }
}

String urlencode(const String &str) {
  String encoded = "";
  char buf[5];
  for (size_t i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    // Unreserved characters according to RFC 3986
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      // Percent-encode others (snprintf for safety)
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

static MoonPhase calculate_moon_phase(int year, int month, int day) {
  // Calculate days since known new moon (Jan 6, 2000)
  int days_since_2000 = (year - 2000) * 365 + (year - 2000) / 4;
  int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  days_since_2000 += month_days[month - 1] + day - 6;
  
  // Correct leap year logic (handles century rules)
  if (month > 2) {
    bool is_leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (is_leap) days_since_2000++;
  }
  
  // Moon cycle is 29.53 days (more accurate than 30)
  float moon_day = fmod((float)days_since_2000, 29.53f);
  
  // Phase boundaries adjusted for 29.53 day cycle
  if (moon_day < 1.85f) return MOON_NEW;
  else if (moon_day < 7.38f) return MOON_WAXING_CRESCENT;
  else if (moon_day < 9.23f) return MOON_FIRST_QUARTER;
  else if (moon_day < 14.77f) return MOON_WAXING_GIBBOUS;
  else if (moon_day < 16.61f) return MOON_FULL;
  else if (moon_day < 22.15f) return MOON_WANING_GIBBOUS;
  else if (moon_day < 23.99f) return MOON_THIRD_QUARTER;
  else return MOON_WANING_CRESCENT;
}

// Get abbreviated moon phase text for display
static const char* get_moon_phase_abbr(MoonPhase phase) {
  switch(phase) {
    case MOON_NEW: return "New";
    case MOON_WAXING_CRESCENT: return "Wax Cres";
    case MOON_FIRST_QUARTER: return "1st Qtr";
    case MOON_WAXING_GIBBOUS: return "Wax Gibb";
    case MOON_FULL: return "Full";
    case MOON_WANING_GIBBOUS: return "Wan Gibb";
    case MOON_THIRD_QUARTER: return "3rd Qtr";
    case MOON_WANING_CRESCENT: return "Wan Cres";
    default: return "Unknown";
  }
}

// Set moon phase icon - 8 dedicated icons (no rotation)
static void set_moon_icon(lv_obj_t* img, MoonPhase phase) {
  const void* icon;
  
  switch(phase) {
    case MOON_NEW: icon = &icon_new_moon; break;
    case MOON_WAXING_CRESCENT: icon = &icon_waxing_crescent; break;
    case MOON_FIRST_QUARTER: icon = &icon_first_quarter; break;
    case MOON_WAXING_GIBBOUS: icon = &icon_waxing_gibbous; break;
    case MOON_FULL: icon = &icon_full_moon; break;
    case MOON_WANING_GIBBOUS: icon = &icon_waning_gibbous; break;
    case MOON_THIRD_QUARTER: icon = &icon_third_quarter; break;
    case MOON_WANING_CRESCENT: icon = &icon_waning_crescent; break;
    default: icon = &icon_new_moon; break;
  }
  
  lv_img_set_src(img, icon);
}

static const char* degrees_to_cardinal(float degrees) {
  // Normalize to 0-360 range
  degrees = fmodf(degrees + 360.0f, 360.0f);
  
  const char* directions[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", 
                              "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  int index = (int)((degrees + 11.25) / 22.5) % 16;
  return directions[index];
}

static void update_clock(lv_timer_t *timer) {
  struct tm timeinfo;

  check_for_night_mode();

  if (!getLocalTime(&timeinfo)) return;

  const LocalizedStrings* strings = get_strings(current_language);
  char buf[16];
  if (use_24_hour) {
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  } else {
    int hour = timeinfo.tm_hour % 12;
    if(hour == 0) hour = 12;
    const char *ampm = (timeinfo.tm_hour < 12) ? strings->am : strings->pm;
    snprintf(buf, sizeof(buf), "%d:%02d%s", hour, timeinfo.tm_min, ampm);
  }
  lv_label_set_text(lbl_clock, buf);
}

// Helper function for slide animations
static void slide_x_animation(lv_obj_t *obj, int from_x, int to_x, uint32_t duration) {
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, obj);
  lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_x);
  lv_anim_set_values(&anim, from_x, to_x);
  lv_anim_set_time(&anim, duration);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
  lv_anim_start(&anim);
}

static void scroll_location_animation(lv_timer_t *timer) {
  if (!lbl_forecast || !lbl_location_scroll) return;
  
  const int off_screen_right = SCREEN_WIDTH + 40;  // Off-screen right
  const int off_screen_left = -SCREEN_WIDTH;       // Off-screen left
  const int on_screen_x = 20;                      // On-screen position
  const uint32_t anim_duration = 800;
  
  if (screen_state.showing_forecast) {
    // Show location, hide forecast
    lv_obj_clear_flag(lbl_location_scroll, LV_OBJ_FLAG_HIDDEN);
    
    slide_x_animation(lbl_forecast, lv_obj_get_x(lbl_forecast), off_screen_left, anim_duration);
    slide_x_animation(lbl_location_scroll, off_screen_right, on_screen_x, anim_duration);
    
    screen_state.showing_forecast = false;
  } else {
    // Show forecast, hide location
    lv_obj_clear_flag(lbl_forecast, LV_OBJ_FLAG_HIDDEN);
    
    slide_x_animation(lbl_location_scroll, lv_obj_get_x(lbl_location_scroll), off_screen_right, anim_duration);
    slide_x_animation(lbl_forecast, off_screen_left, on_screen_x, anim_duration);
    
    screen_state.showing_forecast = true;
  }
}

static void ta_event_cb(lv_event_t *e) {
  lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
  lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);

  // Show keyboard
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_move_foreground(kb);
  lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

static void kb_event_cb(lv_event_t *e) {
  lv_obj_t *kb = static_cast<lv_obj_t *>(lv_event_get_target(e));
  lv_obj_add_flag((lv_obj_t *)lv_event_get_target(e), LV_OBJ_FLAG_HIDDEN);

  if (lv_event_get_code(e) == LV_EVENT_READY) {
    const char *loc = lv_textarea_get_text(loc_ta);
    if (strlen(loc) > 0) {
      do_geocode_query(loc);
    }
  }
}

static void ta_defocus_cb(lv_event_t *e) {
  lv_obj_add_flag((lv_obj_t *)lv_event_get_user_data(e), LV_OBJ_FLAG_HIDDEN);
}

void touchscreen_read(lv_indev_t *indev, lv_indev_data_t *data) {
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    TS_Point p = touchscreen.getPoint();

    x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
    z = p.z;

    // Handle touch during dimmed screen
    if (night_mode_active) {
      // Temporarily wake the screen for 15 seconds
      analogWrite(LCD_BACKLIGHT_PIN, prefs.getUInt("brightness", 128));
    
      if (temp_screen_wakeup_timer) {
        lv_timer_del(temp_screen_wakeup_timer);
      }
      temp_screen_wakeup_timer = lv_timer_create(handle_temp_screen_wakeup_timeout, 15000, NULL);
      lv_timer_set_repeat_count(temp_screen_wakeup_timer, 1); // Run only once
      Serial.println("Woke up screen. Setting timer to turn of screen after 15 seconds of inactivity.");

      if (!temp_screen_wakeup_active) {
          // If this is the wake-up tap, don't pass this touch to the UI - just undim the screen
          temp_screen_wakeup_active = true;
          data->state = LV_INDEV_STATE_RELEASED;
          return;
      }
      // temp_screen_wakeup_active is already true, subsequent touches pass through normally
    }

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  static TFT_eSPI tft;  // Static to persist beyond setup()
  tft.init();
  
  // Setup backlight pin - analogWrite handles PWM automatically
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  Serial.println("Backlight initialized");

  lv_init();

  // Init touchscreen
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(0);

  lv_display_t *disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchscreen_read);

  // Load saved prefs
  prefs.begin("weather", false);
  themeMgr.init(&prefs);
  String lat = prefs.getString("latitude", LATITUDE_DEFAULT);
  lat.toCharArray(latitude, sizeof(latitude));
  String lon = prefs.getString("longitude", LONGITUDE_DEFAULT);
  lon.toCharArray(longitude, sizeof(longitude));
  use_fahrenheit = prefs.getBool("useFahrenheit", false);
  location = prefs.getString("location", LOCATION_DEFAULT);
  use_night_mode = prefs.getBool("useNightMode", false);
  uint32_t brightness = prefs.getUInt("brightness", 255);
  use_24_hour = prefs.getBool("use24Hour", false);
  current_language = (Language)prefs.getUInt("language", LANG_EN);
  analogWrite(LCD_BACKLIGHT_PIN, brightness);

  // Check for Wi-Fi config and request it if not available
  WiFiManager wm;
  wm.setAPCallback(apModeCallback);
  wm.autoConnect(DEFAULT_CAPTIVE_SSID);

  // Start web server for theme customization
  setupWebServer(&themeMgr);
  Serial.println("Theme editor available at http://" + WiFi.localIP().toString());

  lv_obj_clean(lv_scr_act());
  create_ui();
  
  // Start timers AFTER UI is created to avoid race conditions
  lv_timer_create(update_clock, 1000, NULL);
  
  // Create scroll animation timer (5 seconds between transitions)
  scroll_animation_timer = lv_timer_create(scroll_location_animation, 5000, NULL);
  screen_state.scroll_timer = scroll_animation_timer;
  screen_state.current_screen = 0;  // Start on daily forecast
  
  fetch_and_update_weather();
}

void flush_wifi_splashscreen(uint32_t ms = 200) {
  uint32_t start = millis();
  static uint32_t lvgl_tick_last = millis();
  
  while (millis() - start < ms) {
    lv_timer_handler();
    
    // Track actual elapsed time for LVGL
    uint32_t now = millis();
    lv_tick_inc(now - lvgl_tick_last);
    lvgl_tick_last = now;
    
    delay(5);
  }
}

void apModeCallback(WiFiManager *mgr) {
  wifi_splash_screen();
  flush_wifi_splashscreen();
}

void loop() {
  lv_timer_handler();
  handleWebServer();
  
  static uint32_t weather_last = millis();
  if (millis() - weather_last >= UPDATE_INTERVAL) {
    fetch_and_update_weather();
    weather_last = millis();
  }

  // Track actual elapsed time for LVGL (don't assume 5ms)
  static uint32_t lvgl_tick_last = millis();
  uint32_t now = millis();
  lv_tick_inc(now - lvgl_tick_last);
  lvgl_tick_last = now;
  
  delay(5);
}

void wifi_splash_screen() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(themeMgr.getBgTop()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(themeMgr.getBgBottom()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

  const LocalizedStrings* strings = get_strings(current_language);
  lv_obj_t *lbl = lv_label_create(scr);
  lv_label_set_text(lbl, strings->wifi_config);
  lv_obj_set_style_text_font(lbl, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(lbl);
  lv_scr_load(scr);
}

void create_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(themeMgr.getBgTop()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(themeMgr.getBgBottom()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  
  // Explicitly disable all scrolling on the screen
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
  
  // Set explicit size to prevent content overflow detection
  lv_obj_set_size(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_style_max_width(scr, SCREEN_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_max_height(scr, SCREEN_HEIGHT, LV_PART_MAIN);

  // Trigger settings screen on touch
  lv_obj_add_event_cb(scr, screen_event_cb, LV_EVENT_CLICKED, NULL);

  img_today_icon = lv_img_create(scr);
  lv_img_set_src(img_today_icon, &image_partly_cloudy);
  lv_obj_align(img_today_icon, LV_ALIGN_TOP_MID, -64, 4);

  static lv_style_t default_label_style;
  lv_style_init(&default_label_style);
  lv_style_set_text_color(&default_label_style, lv_color_hex(themeMgr.getTextPrimary()));
  lv_style_set_text_opa(&default_label_style, LV_OPA_COVER);

  const LocalizedStrings* strings = get_strings(current_language);

  lbl_today_temp = lv_label_create(scr);
  lv_label_set_text(lbl_today_temp, strings->temp_placeholder);
  lv_obj_set_style_text_font(lbl_today_temp, get_font_42(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_today_temp, LV_ALIGN_TOP_MID, 45, 25);
  lv_obj_add_style(lbl_today_temp, &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);

  lbl_today_feels_like = lv_label_create(scr);
  lv_label_set_text(lbl_today_feels_like, strings->feels_like_temp);
  lv_obj_set_style_text_font(lbl_today_feels_like, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_today_feels_like, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_today_feels_like, LV_ALIGN_TOP_MID, 45, 75);

  lbl_forecast = lv_label_create(scr);
  lv_label_set_text(lbl_forecast, strings->seven_day_forecast);
  lv_obj_set_style_text_font(lbl_forecast, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_forecast, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_add_flag(lbl_forecast, LV_OBJ_FLAG_FLOATING); // Don't affect parent bounds calculation
  lv_obj_align(lbl_forecast, LV_ALIGN_TOP_LEFT, 20, 110);

  // Create location label (initially hidden off-screen to the right)
  lbl_location_scroll = lv_label_create(scr);
  String location_upper = location;
  location_upper.toUpperCase();
  lv_label_set_text(lbl_location_scroll, location_upper.c_str());
  lv_obj_set_style_text_font(lbl_location_scroll, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_location_scroll, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_add_flag(lbl_location_scroll, LV_OBJ_FLAG_HIDDEN); // Hide BEFORE positioning to prevent bounds calculation
  lv_obj_add_flag(lbl_location_scroll, LV_OBJ_FLAG_FLOATING); // Don't affect parent bounds calculation
  lv_obj_align(lbl_location_scroll, LV_ALIGN_TOP_LEFT, SCREEN_WIDTH + 40, 110); // Position off-screen right

  box_daily = create_standard_box(scr);
  screen_state.boxes[0] = box_daily;
  lv_obj_clear_flag(box_daily, LV_OBJ_FLAG_HIDDEN);  // Daily shown by default

  for (int i = 0; i < 7; i++) {
    lbl_daily_day[i] = lv_label_create(box_daily);
    lbl_daily_high[i] = lv_label_create(box_daily);
    lbl_daily_low[i] = lv_label_create(box_daily);
    img_daily[i] = lv_img_create(box_daily);

    lv_obj_add_style(lbl_daily_day[i], &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_daily_day[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_daily_day[i], LV_ALIGN_TOP_LEFT, 2, i * 24);

    lv_obj_add_style(lbl_daily_high[i], &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_daily_high[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_daily_high[i], LV_ALIGN_TOP_RIGHT, 0, i * 24);

    lv_label_set_text(lbl_daily_low[i], "");
    lv_obj_set_style_text_color(lbl_daily_low[i], lv_color_hex(themeMgr.getTextLow()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_daily_low[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_daily_low[i], LV_ALIGN_TOP_RIGHT, -50, i * 24);

    lv_img_set_src(img_daily[i], &icon_partly_cloudy);
    lv_obj_align(img_daily[i], LV_ALIGN_TOP_LEFT, 72, i * 24);
  }

  box_hourly = create_standard_box(scr);
  screen_state.boxes[1] = box_hourly;

  for (int i = 0; i < 7; i++) {
    lbl_hourly[i] = lv_label_create(box_hourly);
    lbl_precipitation_probability[i] = lv_label_create(box_hourly);
    lbl_hourly_temp[i] = lv_label_create(box_hourly);
    img_hourly[i] = lv_img_create(box_hourly);

    lv_obj_add_style(lbl_hourly[i], &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_hourly[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_hourly[i], LV_ALIGN_TOP_LEFT, 2, i * 24);

    lv_obj_add_style(lbl_hourly_temp[i], &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_hourly_temp[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_hourly_temp[i], LV_ALIGN_TOP_RIGHT, 0, i * 24);

    lv_label_set_text(lbl_precipitation_probability[i], "");
    lv_obj_set_style_text_color(lbl_precipitation_probability[i], lv_color_hex(themeMgr.getTextTertiary()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_precipitation_probability[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_precipitation_probability[i], LV_ALIGN_TOP_RIGHT, -55, i * 24);

    lv_img_set_src(img_hourly[i], &icon_partly_cloudy);
    lv_obj_align(img_hourly[i], LV_ALIGN_TOP_LEFT, 72, i * 24);
  }

  lv_obj_add_flag(box_hourly, LV_OBJ_FLAG_HIDDEN);

  // Wind & Air Quality screen
  box_wind = create_standard_box(scr);
  screen_state.boxes[2] = box_wind;

  // Wind Speed (row 0)
  lbl_wind_speed_title = lv_label_create(box_wind);
  lv_label_set_text(lbl_wind_speed_title, strings->wind);
  lv_obj_set_style_text_color(lbl_wind_speed_title, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_wind_speed_title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_wind_speed_title, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl_wind_speed_value = lv_label_create(box_wind);
  lv_label_set_text(lbl_wind_speed_value, "--");
  lv_obj_set_style_text_color(lbl_wind_speed_value, lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_wind_speed_value, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_wind_speed_value, LV_ALIGN_TOP_RIGHT, 0, 0);

  // Wind Direction (row 1)
  lbl_wind_direction_title = lv_label_create(box_wind);
  lv_label_set_text(lbl_wind_direction_title, strings->direction);
  lv_obj_set_style_text_color(lbl_wind_direction_title, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_wind_direction_title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_wind_direction_title, LV_ALIGN_TOP_LEFT, 0, 24);

  lbl_wind_direction_value = lv_label_create(box_wind);
  lv_label_set_text(lbl_wind_direction_value, "--");
  lv_obj_set_style_text_color(lbl_wind_direction_value, lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_wind_direction_value, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_wind_direction_value, LV_ALIGN_TOP_RIGHT, 0, 24);

  // Wind Gusts (row 2)
  lbl_wind_gusts_title = lv_label_create(box_wind);
  lv_label_set_text(lbl_wind_gusts_title, strings->gusts);
  lv_obj_set_style_text_color(lbl_wind_gusts_title, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_wind_gusts_title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_wind_gusts_title, LV_ALIGN_TOP_LEFT, 0, 48);

  lbl_wind_gusts_value = lv_label_create(box_wind);
  lv_label_set_text(lbl_wind_gusts_value, "--");
  lv_obj_set_style_text_color(lbl_wind_gusts_value, lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_wind_gusts_value, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_wind_gusts_value, LV_ALIGN_TOP_RIGHT, 0, 48);

  // UV Index (row 3)
  lbl_uv_index_title = lv_label_create(box_wind);
  lv_label_set_text(lbl_uv_index_title, strings->uv_index);
  lv_obj_set_style_text_color(lbl_uv_index_title, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_uv_index_title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_uv_index_title, LV_ALIGN_TOP_LEFT, 0, 72);

  lbl_uv_index_value = lv_label_create(box_wind);
  lv_label_set_text(lbl_uv_index_value, "--");
  lv_obj_set_style_text_color(lbl_uv_index_value, lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_uv_index_value, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_uv_index_value, LV_ALIGN_TOP_RIGHT, 0, 72);

  // Cloud Cover (row 4)
  lbl_cloud_cover_title = lv_label_create(box_wind);
  lv_label_set_text(lbl_cloud_cover_title, strings->cloud_cover);
  lv_obj_set_style_text_color(lbl_cloud_cover_title, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_cloud_cover_title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_cloud_cover_title, LV_ALIGN_TOP_LEFT, 0, 96);

  lbl_cloud_cover_value = lv_label_create(box_wind);
  lv_label_set_text(lbl_cloud_cover_value, "--");
  lv_obj_set_style_text_color(lbl_cloud_cover_value, lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_cloud_cover_value, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_cloud_cover_value, LV_ALIGN_TOP_RIGHT, 0, 96);

  // Pollen Count (row 5)
  lbl_pollen_count_title = lv_label_create(box_wind);
  lv_label_set_text(lbl_pollen_count_title, strings->pollen);
  lv_obj_set_style_text_color(lbl_pollen_count_title, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_pollen_count_title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_pollen_count_title, LV_ALIGN_TOP_LEFT, 0, 120);

  lbl_pollen_count_value = lv_label_create(box_wind);
  lv_label_set_text(lbl_pollen_count_value, "--");
  lv_obj_set_style_text_color(lbl_pollen_count_value, lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_pollen_count_value, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_pollen_count_value, LV_ALIGN_TOP_RIGHT, 0, 120);

  // Air Quality (row 6)
  lbl_air_quality_title = lv_label_create(box_wind);
  lv_label_set_text(lbl_air_quality_title, "Air Quality");
  lv_obj_set_style_text_color(lbl_air_quality_title, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_air_quality_title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_air_quality_title, LV_ALIGN_TOP_LEFT, 0, 144);

  lbl_air_quality_value = lv_label_create(box_wind);
  lv_label_set_text(lbl_air_quality_value, "--");
  lv_obj_set_style_text_color(lbl_air_quality_value, lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_air_quality_value, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_air_quality_value, LV_ALIGN_TOP_RIGHT, 0, 144);

  lv_obj_add_flag(box_wind, LV_OBJ_FLAG_HIDDEN);

  // Precipitation & Humidity screen
  box_precipitation = create_standard_box(scr);
  screen_state.boxes[3] = box_precipitation;

  // Precipitation (row 0)
  lbl_precipitation_title = lv_label_create(box_precipitation);
  lv_label_set_text(lbl_precipitation_title, strings->precipitation);
  lv_obj_set_style_text_color(lbl_precipitation_title, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_precipitation_title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_precipitation_title, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl_precipitation_value = lv_label_create(box_precipitation);
  lv_label_set_text(lbl_precipitation_value, "--");
  lv_obj_set_style_text_color(lbl_precipitation_value, lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_precipitation_value, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_precipitation_value, LV_ALIGN_TOP_RIGHT, 0, 0);

  // Humidity (row 1)
  lbl_humidity_title = lv_label_create(box_precipitation);
  lv_label_set_text(lbl_humidity_title, strings->humidity);
  lv_obj_set_style_text_color(lbl_humidity_title, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_humidity_title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_humidity_title, LV_ALIGN_TOP_LEFT, 0, 36);

  lbl_humidity_value = lv_label_create(box_precipitation);
  lv_label_set_text(lbl_humidity_value, "--");
  lv_obj_set_style_text_color(lbl_humidity_value, lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_humidity_value, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_humidity_value, LV_ALIGN_TOP_RIGHT, 0, 36);

  // Dew Point (row 2)
  lbl_dew_point_title = lv_label_create(box_precipitation);
  lv_label_set_text(lbl_dew_point_title, strings->dew_point);
  lv_obj_set_style_text_color(lbl_dew_point_title, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_dew_point_title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_dew_point_title, LV_ALIGN_TOP_LEFT, 0, 72);

  lbl_dew_point_value = lv_label_create(box_precipitation);
  lv_label_set_text(lbl_dew_point_value, "--");
  lv_obj_set_style_text_color(lbl_dew_point_value, lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_dew_point_value, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_dew_point_value, LV_ALIGN_TOP_RIGHT, 0, 72);

  // Visibility (row 3)
  lbl_visibility_title = lv_label_create(box_precipitation);
  lv_label_set_text(lbl_visibility_title, strings->visibility);
  lv_obj_set_style_text_color(lbl_visibility_title, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_visibility_title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_visibility_title, LV_ALIGN_TOP_LEFT, 0, 108);

  lbl_visibility_value = lv_label_create(box_precipitation);
  lv_label_set_text(lbl_visibility_value, "--");
  lv_obj_set_style_text_color(lbl_visibility_value, lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_visibility_value, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_visibility_value, LV_ALIGN_TOP_RIGHT, 0, 108);

  // Pressure (row 4)
  lbl_pressure_title = lv_label_create(box_precipitation);
  lv_label_set_text(lbl_pressure_title, strings->pressure);
  lv_obj_set_style_text_color(lbl_pressure_title, lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_pressure_title, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_pressure_title, LV_ALIGN_TOP_LEFT, 0, 144);

  lbl_pressure_value = lv_label_create(box_precipitation);
  lv_label_set_text(lbl_pressure_value, "--");
  lv_obj_set_style_text_color(lbl_pressure_value, lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_pressure_value, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_pressure_value, LV_ALIGN_TOP_RIGHT, 0, 144);

  lv_obj_add_flag(box_precipitation, LV_OBJ_FLAG_HIDDEN);

  // Sun & Moon screen (7 days like daily forecast)
  box_sun = create_standard_box(scr);
  screen_state.boxes[4] = box_sun;

  for (int i = 0; i < 7; i++) {
    // Day label (left side)
    lbl_sun_day[i] = lv_label_create(box_sun);
    lv_obj_set_style_text_color(lbl_sun_day[i], lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_sun_day[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_sun_day[i], LV_ALIGN_TOP_LEFT, 2, i * 24);

    // Sunrise time (center-left)
    lbl_sunrise[i] = lv_label_create(box_sun);
    lv_obj_set_style_text_color(lbl_sunrise[i], lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_sunrise[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_sunrise[i], LV_ALIGN_TOP_LEFT, 72, i * 24);

    // Sunset time (right side)
    lbl_sunset[i] = lv_label_create(box_sun);
    lv_obj_set_style_text_color(lbl_sunset[i], lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_sunset[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_sunset[i], LV_ALIGN_TOP_RIGHT, 0, i * 24);
  }

  // Moon Phase screen (7 days like daily forecast)
  box_moon = create_standard_box(scr);
  screen_state.boxes[5] = box_moon;

  for (int i = 0; i < 7; i++) {
    // Day label (left side)
    lbl_moon_day[i] = lv_label_create(box_moon);
    lv_obj_set_style_text_color(lbl_moon_day[i], lv_color_hex(themeMgr.getTextPrimary()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_moon_day[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_moon_day[i], LV_ALIGN_TOP_LEFT, 2, i * 24);

    // Moon phase icon (centered, like weather icons)
    img_moon_phase[i] = lv_img_create(box_moon);
    lv_obj_align(img_moon_phase[i], LV_ALIGN_TOP_LEFT, 72, i * 24);

    // Moon phase text - abbreviated (right side)
    lbl_moon_phase_text[i] = lv_label_create(box_moon);
    lv_obj_set_style_text_color(lbl_moon_phase_text[i], lv_color_hex(themeMgr.getTextSecondary()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_moon_phase_text[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_moon_phase_text[i], LV_ALIGN_TOP_RIGHT, 0, i * 24);
  }

  // Temperature Chart screen (7-day temperature trend)
  box_temp_chart = create_standard_box(scr);
  screen_state.boxes[6] = box_temp_chart;

  // Y-axis labels - High temp (top left, white)
  lbl_temp_high_val = lv_label_create(box_temp_chart);
  lv_label_set_text(lbl_temp_high_val, "--");
  lv_obj_set_style_text_color(lbl_temp_high_val, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_temp_high_val, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_temp_high_val, LV_ALIGN_LEFT_MID, 0, -25);
  lv_obj_clear_flag(lbl_temp_high_val, LV_OBJ_FLAG_CLICKABLE);

  // Y-axis labels - Low temp (bottom left, white)
  lbl_temp_low_val = lv_label_create(box_temp_chart);
  lv_label_set_text(lbl_temp_low_val, "--");
  lv_obj_set_style_text_color(lbl_temp_low_val, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(lbl_temp_low_val, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_temp_low_val, LV_ALIGN_LEFT_MID, 0, 25);
  lv_obj_clear_flag(lbl_temp_low_val, LV_OBJ_FLAG_CLICKABLE);

  // Create LVGL chart object (shifted right, taller)
  lv_obj_t *chart = lv_chart_create(box_temp_chart);
  lv_obj_set_size(chart, 180, 135);
  lv_obj_align(chart, LV_ALIGN_RIGHT_MID, 0, -5);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, 7);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -20, 40);  // Will be adjusted dynamically
  
  // Make chart non-clickable so touch events pass through to parent box
  lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
  
  // Style the chart - remove gridlines, show only axis lines with square corners
  lv_obj_set_style_bg_color(chart, lv_color_hex(themeMgr.getBoxBg()), LV_PART_MAIN);
  lv_obj_set_style_border_width(chart, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(chart, lv_color_hex(themeMgr.getTextTertiary()), LV_PART_MAIN);
  lv_obj_set_style_border_side(chart, (lv_border_side_t)(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_BOTTOM), LV_PART_MAIN);
  lv_obj_set_style_radius(chart, 0, LV_PART_MAIN);  // Square corners
  lv_obj_set_style_pad_all(chart, 10, LV_PART_MAIN);
  
  // Hide gridlines completely
  lv_obj_set_style_line_width(chart, 0, LV_PART_MAIN);  // Hide horizontal gridlines
  lv_chart_set_div_line_count(chart, 0, 0);  // No division lines
  
  // Add two series: high temps and low temps
  lv_chart_series_t *series_high = lv_chart_add_series(chart, lv_color_hex(0xFF6B6B), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_series_t *series_low = lv_chart_add_series(chart, lv_color_hex(0x4ECDC4), LV_CHART_AXIS_PRIMARY_Y);
  
  // Set line width for the data series
  lv_obj_set_style_line_width(chart, 4, LV_PART_ITEMS);  // Thicker lines
  
  // Store chart and series references in global variables
  temp_chart = chart;
  temp_series_high = series_high;
  temp_series_low = series_low;

  // X-axis labels - Day letters (below the chart, aligned with plot points)
  // Chart is 180px wide, right-aligned in 220px box, with 10px internal padding
  // Plot points span across 160px (180 - 20px padding), starting at x=50 from box left
  // 7 points = 6 gaps, so spacing is 160/6 = 26.67px
  for (int i = 0; i < 7; i++) {
    lbl_day_letters[i] = lv_label_create(box_temp_chart);
    lv_label_set_text(lbl_day_letters[i], "");
    lv_obj_set_style_text_color(lbl_day_letters[i], lv_color_hex(themeMgr.getTextTertiary()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_day_letters[i], get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
    // Start at x=50 (40 for right-align + 10 for chart padding), then add 26.67px per point
    lv_obj_align(lbl_day_letters[i], LV_ALIGN_BOTTOM_LEFT, 50 + (int)(i * 26.67) - 3, 0);  // -3 to center letter
    lv_obj_clear_flag(lbl_day_letters[i], LV_OBJ_FLAG_CLICKABLE);
  }

  lv_obj_add_flag(box_temp_chart, LV_OBJ_FLAG_HIDDEN);

  // Create clock label in the top-right corner
  lbl_clock = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_clock, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_clock, lv_color_hex(themeMgr.getTextClock()), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(lbl_clock, "");
  lv_obj_align(lbl_clock, LV_ALIGN_TOP_RIGHT, -10, 2);
}

void populate_results_dropdown() {
  dd_opts[0] = '\0';
  for (JsonObject item : geoResults) {
    strcat(dd_opts, item["name"].as<const char *>());
    if (item["admin1"]) {
      strcat(dd_opts, ", ");
      strcat(dd_opts, item["admin1"].as<const char *>());
    }

    strcat(dd_opts, "\n");
  }

  if (geoResults.size() > 0) {
    lv_dropdown_set_options_static(results_dd, dd_opts);
    lv_obj_add_flag(results_dd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(btn_close_loc, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_close_loc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_close_loc, lv_palette_darken(LV_PALETTE_GREEN, 1), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_flag(btn_close_loc, LV_OBJ_FLAG_CLICKABLE);
  }
}

static void location_save_event_cb(lv_event_t *e) {
  JsonArray *pres = static_cast<JsonArray *>(lv_event_get_user_data(e));
  uint16_t idx = lv_dropdown_get_selected(results_dd);

  JsonObject obj = (*pres)[idx];
  double lat = obj["latitude"].as<double>();
  double lon = obj["longitude"].as<double>();

  snprintf(latitude, sizeof(latitude), "%.6f", lat);
  snprintf(longitude, sizeof(longitude), "%.6f", lon);
  prefs.putString("latitude", latitude);
  prefs.putString("longitude", longitude);

  String opts;
  const char *name = obj["name"];
  const char *admin = obj["admin1"];
  const char *country = obj["country_code"];
  opts += name;
  if (admin) {
    opts += ", ";
    opts += admin;
  }

  prefs.putString("location", opts);
  location = prefs.getString("location");

  // Re‐fetch weather immediately
  lv_label_set_text(lbl_loc, opts.c_str());
  fetch_and_update_weather();

  lv_obj_del(location_win);
  location_win = nullptr;
}

static void location_cancel_event_cb(lv_event_t *e) {
  lv_obj_del(location_win);
  location_win = nullptr;
}

void screen_event_cb(lv_event_t *e) {
  create_settings_window();
}

void cycle_screens_cb(lv_event_t *e) {
  const LocalizedStrings* strings = get_strings(current_language);
  
  // Hide current screen
  if (screen_state.boxes[screen_state.current_screen]) {
    lv_obj_add_flag(screen_state.boxes[screen_state.current_screen], LV_OBJ_FLAG_HIDDEN);
  }
  
  // Cycle to next screen (7 screens total)
  screen_state.current_screen = (screen_state.current_screen + 1) % 7;
  
  // Show new screen
  if (screen_state.boxes[screen_state.current_screen]) {
    lv_obj_clear_flag(screen_state.boxes[screen_state.current_screen], LV_OBJ_FLAG_HIDDEN);
  }
  
  // Update forecast label
  switch(screen_state.current_screen) {
    case 0: // Daily forecast
      lv_label_set_text(lbl_forecast, strings->seven_day_forecast);
      break;
    case 1: // Hourly forecast
      lv_label_set_text(lbl_forecast, strings->hourly_forecast);
      break;
    case 2: // Wind & Air Quality
      lv_label_set_text(lbl_forecast, "WIND & AIR QUALITY");
      break;
    case 3: // Precipitation & Humidity
      lv_label_set_text(lbl_forecast, "PRECIPITATION & HUMIDITY");
      break;
    case 4: // Sunrise & Sunset
      lv_label_set_text(lbl_forecast, "SUNRISE & SUNSET");
      break;
    case 5: // Moon Phase
      lv_label_set_text(lbl_forecast, "MOON PHASE");
      break;
    case 6: // Temperature Chart
      lv_label_set_text(lbl_forecast, "TEMPERATURE TREND");
      break;
  }
}


static void reset_wifi_event_handler(lv_event_t *e) {
  const LocalizedStrings* strings = get_strings(current_language);
  lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
  lv_obj_t *title = lv_msgbox_add_title(mbox, strings->reset);
  lv_obj_set_style_margin_left(title, 10, 0);
  lv_obj_set_style_text_font(title, get_font_16(), 0);

  lv_obj_t *text = lv_msgbox_add_text(mbox, strings->reset_confirmation);
  lv_obj_set_style_text_font(text, get_font_12(), 0);
  lv_msgbox_add_close_button(mbox);

  lv_obj_t *btn_no = lv_msgbox_add_footer_button(mbox, strings->cancel);
  lv_obj_set_style_text_font(btn_no, get_font_12(), 0);
  lv_obj_t *btn_yes = lv_msgbox_add_footer_button(mbox, strings->reset);
  lv_obj_set_style_text_font(btn_yes, get_font_12(), 0);

  lv_obj_set_style_bg_color(btn_yes, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_yes, lv_palette_darken(LV_PALETTE_RED, 1), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn_yes, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_set_width(mbox, 230);
  lv_obj_center(mbox);

  lv_obj_set_style_border_width(mbox, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(mbox, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_opa(mbox, LV_OPA_COVER,   LV_PART_MAIN);
  lv_obj_set_style_radius(mbox, 4, LV_PART_MAIN);

  lv_obj_add_event_cb(btn_yes, reset_confirm_yes_cb, LV_EVENT_CLICKED, mbox);
  lv_obj_add_event_cb(btn_no, reset_confirm_no_cb, LV_EVENT_CLICKED, mbox);
}

static void reset_confirm_yes_cb(lv_event_t *e) {
  lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
  Serial.println("Clearing Wi-Fi creds and rebooting");
  WiFiManager wm;
  wm.resetSettings();
  delay(100);
  esp_restart();
}

static void reset_confirm_no_cb(lv_event_t *e) {
  lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
  lv_obj_del(mbox);
}

static void change_location_event_cb(lv_event_t *e) {
  if (location_win) return;

  create_location_dialog();
}

void create_location_dialog() {
  const LocalizedStrings* strings = get_strings(current_language);
  location_win = lv_win_create(lv_scr_act());
  lv_obj_t *title = lv_win_add_title(location_win, strings->change_location);
  lv_obj_t *header = lv_win_get_header(location_win);
  lv_obj_set_style_height(header, 30, 0);
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_set_style_margin_left(title, 10, 0);
  lv_obj_set_size(location_win, 240, 320);
  lv_obj_center(location_win);

  lv_obj_t *cont = lv_win_get_content(location_win);

  lv_obj_t *lbl = lv_label_create(cont);
  lv_label_set_text(lbl, strings->city);
  lv_obj_set_style_text_font(lbl, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 5, 10);

  loc_ta = lv_textarea_create(cont);
  lv_textarea_set_one_line(loc_ta, true);
  lv_textarea_set_placeholder_text(loc_ta, strings->city_placeholder);
  lv_obj_set_width(loc_ta, 170);
  lv_obj_align_to(loc_ta, lbl, LV_ALIGN_OUT_RIGHT_MID, 5, 0);

  lv_obj_add_event_cb(loc_ta, ta_event_cb, LV_EVENT_CLICKED, kb);
  lv_obj_add_event_cb(loc_ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, kb);

  lv_obj_t *lbl2 = lv_label_create(cont);
  lv_label_set_text(lbl2, strings->search_results);
  lv_obj_set_style_text_font(lbl2, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl2, LV_ALIGN_TOP_LEFT, 5, 50);

  results_dd = lv_dropdown_create(cont);
  lv_obj_set_width(results_dd, 200);
  lv_obj_align(results_dd, LV_ALIGN_TOP_LEFT, 5, 70);
  lv_obj_set_style_text_font(results_dd, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(results_dd, get_font_14(), LV_PART_SELECTED | LV_STATE_DEFAULT);

  lv_obj_t *list = lv_dropdown_get_list(results_dd);
  lv_obj_set_style_text_font(list, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_dropdown_set_options(results_dd, "");
  lv_obj_clear_flag(results_dd, LV_OBJ_FLAG_CLICKABLE);

  btn_close_loc = lv_btn_create(cont);
  lv_obj_set_size(btn_close_loc, 80, 40);
  lv_obj_align(btn_close_loc, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  lv_obj_add_event_cb(btn_close_loc, location_save_event_cb, LV_EVENT_CLICKED, &geoResults);
  lv_obj_set_style_bg_color(btn_close_loc, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(btn_close_loc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_close_loc, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_clear_flag(btn_close_loc, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *lbl_close = lv_label_create(btn_close_loc);
  lv_label_set_text(lbl_close, strings->save);
  lv_obj_set_style_text_font(lbl_close, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_close);

  lv_obj_t *btn_cancel_loc = lv_btn_create(cont);
  lv_obj_set_size(btn_cancel_loc, 80, 40);
  lv_obj_align_to(btn_cancel_loc, btn_close_loc, LV_ALIGN_OUT_LEFT_MID, -5, 0);
  lv_obj_add_event_cb(btn_cancel_loc, location_cancel_event_cb, LV_EVENT_CLICKED, &geoResults);

  lv_obj_t *lbl_cancel = lv_label_create(btn_cancel_loc);
  lv_label_set_text(lbl_cancel, strings->cancel);
  lv_obj_set_style_text_font(lbl_cancel, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_cancel);
}

void create_settings_window() {
  if (settings_win) return;

  int vertical_element_spacing = 21;

  const LocalizedStrings* strings = get_strings(current_language);
  settings_win = lv_win_create(lv_scr_act());
  
  // Hide scrolling labels to prevent scrollbars
  if (lbl_forecast) lv_obj_add_flag(lbl_forecast, LV_OBJ_FLAG_HIDDEN);
  if (lbl_location_scroll) lv_obj_add_flag(lbl_location_scroll, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *header = lv_win_get_header(settings_win);
  lv_obj_set_style_height(header, 30, 0);

  lv_obj_t *title = lv_win_add_title(settings_win, strings->aura_settings);
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_set_style_margin_left(title, 10, 0);

  lv_obj_center(settings_win);
  lv_obj_set_width(settings_win, 240);

  lv_obj_t *cont = lv_win_get_content(settings_win);

  // Brightness
  lv_obj_t *lbl_b = lv_label_create(cont);
  lv_label_set_text(lbl_b, strings->brightness);
  lv_obj_set_style_text_font(lbl_b, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_b, LV_ALIGN_TOP_LEFT, 0, 5);
  lv_obj_t *slider = lv_slider_create(cont);
  lv_slider_set_range(slider, 1, 255);
  uint32_t saved_b = prefs.getUInt("brightness", 128);
  lv_slider_set_value(slider, saved_b, LV_ANIM_OFF);
  lv_obj_set_width(slider, 100);
  lv_obj_align_to(slider, lbl_b, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

  lv_obj_add_event_cb(slider, [](lv_event_t *e){
    lv_obj_t *s = (lv_obj_t*)lv_event_get_target(e);
    uint32_t v = lv_slider_get_value(s);
    
    // Manual brightness change should override night mode
    if (night_mode_active) {
      night_mode_active = false;
    }
    
    analogWrite(LCD_BACKLIGHT_PIN, v);
    prefs.putUInt("brightness", v);
  }, LV_EVENT_VALUE_CHANGED, NULL);

  // 'Night mode' switch
  lv_obj_t *lbl_night_mode = lv_label_create(cont);
  lv_label_set_text(lbl_night_mode, strings->use_night_mode);
  lv_obj_set_style_text_font(lbl_night_mode, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(lbl_night_mode, lbl_b, LV_ALIGN_OUT_BOTTOM_LEFT, 0, vertical_element_spacing);

  night_mode_switch = lv_switch_create(cont);
  lv_obj_align_to(night_mode_switch, lbl_night_mode, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
  if (use_night_mode) {
    lv_obj_add_state(night_mode_switch, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(night_mode_switch, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(night_mode_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // 'Use F' switch
  lv_obj_t *lbl_u = lv_label_create(cont);
  lv_label_set_text(lbl_u, strings->use_fahrenheit);
  lv_obj_set_style_text_font(lbl_u, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(lbl_u, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(lbl_u, lbl_night_mode, LV_ALIGN_OUT_BOTTOM_LEFT, 0, vertical_element_spacing);

  unit_switch = lv_switch_create(cont);
  lv_obj_align_to(unit_switch, lbl_u, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
  if (use_fahrenheit) {
    lv_obj_add_state(unit_switch, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(unit_switch, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(unit_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // 24-hr time switch
  lv_obj_t *lbl_24hr = lv_label_create(cont);
  lv_label_set_text(lbl_24hr, strings->use_24hr);
  lv_obj_set_style_text_font(lbl_24hr, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(lbl_24hr, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(lbl_24hr, unit_switch, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

  clock_24hr_switch = lv_switch_create(cont);
  lv_obj_align_to(clock_24hr_switch, lbl_24hr, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
  if (use_24_hour) {
    lv_obj_add_state(clock_24hr_switch, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(clock_24hr_switch, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(clock_24hr_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // Current Location label
  lv_obj_t *lbl_loc_l = lv_label_create(cont);
  lv_label_set_text(lbl_loc_l, strings->location);
  lv_obj_set_style_text_font(lbl_loc_l, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(lbl_loc_l, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(lbl_loc_l, lbl_u, LV_ALIGN_OUT_BOTTOM_LEFT, 0, vertical_element_spacing);

  lbl_loc = lv_label_create(cont);
  lv_label_set_text(lbl_loc, location.c_str());
  lv_obj_set_style_text_font(lbl_loc, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(lbl_loc, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(lbl_loc, lbl_loc_l, LV_ALIGN_OUT_RIGHT_MID, 5, 0);

  // Language selection
  lv_obj_t *lbl_lang = lv_label_create(cont);
  lv_label_set_text(lbl_lang, strings->language_label);
  lv_obj_set_style_text_font(lbl_lang, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(lbl_lang, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(lbl_lang, lbl_loc_l, LV_ALIGN_OUT_BOTTOM_LEFT, 0, vertical_element_spacing);

  language_dropdown = lv_dropdown_create(cont);
  lv_dropdown_set_options(language_dropdown, "English\nEspañol\nDeutsch\nFrançais\nTürkçe\nSvenska\nItaliano");
  lv_dropdown_set_selected(language_dropdown, current_language);
  lv_obj_set_width(language_dropdown, 120);
  lv_obj_set_style_text_font(language_dropdown, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(language_dropdown, get_font_12(), LV_PART_SELECTED | LV_STATE_DEFAULT);
  lv_obj_t *list = lv_dropdown_get_list(language_dropdown);
  lv_obj_set_style_text_font(list, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(language_dropdown, lbl_lang, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_obj_add_event_cb(language_dropdown, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // Location search button
  lv_obj_t *btn_change_loc = lv_btn_create(cont);
  lv_obj_align_to(btn_change_loc, lbl_lang, LV_ALIGN_OUT_BOTTOM_LEFT, 0, vertical_element_spacing);

  lv_obj_set_size(btn_change_loc, 100, 40);
  lv_obj_add_event_cb(btn_change_loc, change_location_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_chg = lv_label_create(btn_change_loc);
  lv_label_set_text(lbl_chg, strings->location_btn);
  lv_obj_set_style_text_font(lbl_chg, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_chg);

  // Hidden keyboard object
  if (!kb) {
    kb = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, NULL);
  }

  // Reset WiFi button
  lv_obj_t *btn_reset = lv_btn_create(cont);
  lv_obj_set_style_bg_color(btn_reset, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_reset, lv_palette_darken(LV_PALETTE_RED, 1), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn_reset, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_size(btn_reset, 100, 40);
  lv_obj_align_to(btn_reset, btn_change_loc, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

  lv_obj_add_event_cb(btn_reset, reset_wifi_event_handler, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_reset = lv_label_create(btn_reset);
  lv_label_set_text(lbl_reset, strings->reset_wifi);
  lv_obj_set_style_text_font(lbl_reset, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_reset);

  // Close Settings button
  btn_close_obj = lv_btn_create(cont);
  lv_obj_set_size(btn_close_obj, 80, 40);
  lv_obj_align_to(btn_close_obj, btn_reset, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);  // Tighter spacing
  lv_obj_add_event_cb(btn_close_obj, settings_event_handler, LV_EVENT_CLICKED, NULL);

  // Cancel button
  lv_obj_t *lbl_btn = lv_label_create(btn_close_obj);
  lv_label_set_text(lbl_btn, strings->close);
  lv_obj_set_style_text_font(lbl_btn, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_btn);
}

static void settings_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *tgt = (lv_obj_t *)lv_event_get_target(e);

  if (tgt == unit_switch && code == LV_EVENT_VALUE_CHANGED) {
    use_fahrenheit = lv_obj_has_state(unit_switch, LV_STATE_CHECKED);
  }

  if (tgt == clock_24hr_switch && code == LV_EVENT_VALUE_CHANGED) {
    use_24_hour = lv_obj_has_state(clock_24hr_switch, LV_STATE_CHECKED);
  }

  if (tgt == night_mode_switch && code == LV_EVENT_VALUE_CHANGED) {
    use_night_mode = lv_obj_has_state(night_mode_switch, LV_STATE_CHECKED);
  }

  if (tgt == language_dropdown && code == LV_EVENT_VALUE_CHANGED) {
    current_language = (Language)lv_dropdown_get_selected(language_dropdown);
    // Update the UI immediately to reflect language change
    lv_obj_del(settings_win);
    settings_win = nullptr;
    
    // Save preferences and recreate UI with new language
    prefs.putBool("useFahrenheit", use_fahrenheit);
    prefs.putBool("use24Hour", use_24_hour);
    prefs.putBool("useNightMode", use_night_mode);
    prefs.putUInt("language", current_language);

    lv_keyboard_set_textarea(kb, nullptr);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    
    // Recreate the main UI with the new language
    lv_obj_clean(lv_scr_act());
    create_ui();
    fetch_and_update_weather();
    return;
  }

  if (tgt == btn_close_obj && code == LV_EVENT_CLICKED) {
    prefs.putBool("useFahrenheit", use_fahrenheit);
    prefs.putBool("use24Hour", use_24_hour);
    prefs.putBool("useNightMode", use_night_mode);
    prefs.putUInt("language", current_language);

    lv_keyboard_set_textarea(kb, nullptr);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    lv_obj_del(settings_win);
    settings_win = nullptr;
    
    // Restore scrolling labels
    if (lbl_forecast) lv_obj_clear_flag(lbl_forecast, LV_OBJ_FLAG_HIDDEN);
    if (lbl_location_scroll) lv_obj_clear_flag(lbl_location_scroll, LV_OBJ_FLAG_HIDDEN);

    fetch_and_update_weather();
  }
}

// Screen dimming functions implementation
bool night_mode_should_be_active() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;

  if (!use_night_mode) return false;
  
  int hour = timeinfo.tm_hour;
  return (hour >= NIGHT_MODE_START_HOUR || hour < NIGHT_MODE_END_HOUR);
}

void activate_night_mode() {
  analogWrite(LCD_BACKLIGHT_PIN, 0);
  night_mode_active = true;
}

void deactivate_night_mode() {
  analogWrite(LCD_BACKLIGHT_PIN, prefs.getUInt("brightness", 128));
  night_mode_active = false;
}

void check_for_night_mode() {
  // Optimization: only check when hour changes (not every minute)
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  
  if (timeinfo.tm_hour == last_checked_hour) return;  // Same hour, skip check
  last_checked_hour = timeinfo.tm_hour;
  
  bool night_mode_time = night_mode_should_be_active();

  if (night_mode_time && !night_mode_active && !temp_screen_wakeup_active) {
    activate_night_mode();
  } else if (!night_mode_time && night_mode_active) {
    deactivate_night_mode();
  }
}

void handle_temp_screen_wakeup_timeout(lv_timer_t *timer) {
  if (temp_screen_wakeup_active) {
    temp_screen_wakeup_active = false;

    if (night_mode_should_be_active()) {
      activate_night_mode();
    }
  }
  
  if (temp_screen_wakeup_timer) {
    lv_timer_del(temp_screen_wakeup_timer);
    temp_screen_wakeup_timer = nullptr;
  }
}

void do_geocode_query(const char *q) {
  // Allocate JSON document only when needed to reduce heap fragmentation
  DynamicJsonDocument geoDoc(8 * 1024);
  
  String url = String("https://geocoding-api.open-meteo.com/v1/search?name=") + urlencode(q) + "&count=15";

  HTTPClient http;
  http.begin(url);
  if (http.GET() == HTTP_CODE_OK) {
    Serial.println("Completed location search at open-meteo: " + url);
    auto err = deserializeJson(geoDoc, http.getString());
    if (!err) {
      geoResults = geoDoc["results"].as<JsonArray>();
      populate_results_dropdown();
    } else {
        Serial.println("Failed to parse search response from open-meteo: " + url);
    }
  } else {
      Serial.println("Failed location search at open-meteo: " + url);
  }
  http.end();
  // geoDoc automatically freed when function exits
}

void fetch_and_update_weather() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi no longer connected. Attempting to reconnect...");
    WiFi.disconnect();
    WiFiManager wm;  
    wm.autoConnect(DEFAULT_CAPTIVE_SSID);
    delay(1000);  
    if (WiFi.status() != WL_CONNECTED) { 
      Serial.println("WiFi connection still unavailable.");
      return;   
    }
    Serial.println("WiFi connection reestablished.");
  }


  String url = String("http://api.open-meteo.com/v1/forecast?latitude=")
               + latitude + "&longitude=" + longitude
               + "&current=temperature_2m,apparent_temperature,is_day,weather_code,wind_speed_10m,wind_direction_10m,wind_gusts_10m,relative_humidity_2m,dew_point_2m,pressure_msl,visibility,uv_index,precipitation,cloud_cover"
               + "&daily=temperature_2m_min,temperature_2m_max,weather_code,sunrise,sunset,uv_index_max,precipitation_sum,precipitation_probability_max,wind_speed_10m_max,wind_gusts_10m_max"
               + "&hourly=temperature_2m,precipitation_probability,is_day,weather_code"
               + "&forecast_hours=7"
               + "&timezone=auto";

  HTTPClient http;
  http.begin(url);

  if (http.GET() == HTTP_CODE_OK) {
    Serial.println("Updated weather from open-meteo: " + url);

    String payload = http.getString();
    DynamicJsonDocument doc(32 * 1024);

    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      float t_now = doc["current"]["temperature_2m"].as<float>();
      float t_ap = doc["current"]["apparent_temperature"].as<float>();
      int code_now = doc["current"]["weather_code"].as<int>();
      int is_day = doc["current"]["is_day"].as<int>();
      
      // Wind & Air Quality data
      float wind_speed = doc["current"]["wind_speed_10m"].as<float>();
      float wind_direction = doc["current"]["wind_direction_10m"].as<float>();
      float wind_gusts = doc["current"]["wind_gusts_10m"].as<float>();
      float uv_index = doc["current"]["uv_index"].as<float>();
      int cloud_cover = doc["current"]["cloud_cover"].as<int>();
      
      // Precipitation & Humidity data
      float precipitation = doc["current"]["precipitation"].as<float>();
      int humidity = doc["current"]["relative_humidity_2m"].as<int>();
      float dew_point = doc["current"]["dew_point_2m"].as<float>();
      float visibility = doc["current"]["visibility"].as<float>();
      float pressure = doc["current"]["pressure_msl"].as<float>(); // Mean sea level pressure in hPa

      if (use_fahrenheit) {
        t_now = t_now * 9.0 / 5.0 + 32.0;
        t_ap = t_ap * 9.0 / 5.0 + 32.0;
      }
      const LocalizedStrings* strings = get_strings(current_language);

      int utc_offset_seconds = doc["utc_offset_seconds"].as<int>();
      configTime(utc_offset_seconds, 0, "pool.ntp.org", "time.nist.gov");
      Serial.print("Updating time from NTP with UTC offset: ");
      Serial.println(utc_offset_seconds);

      char unit = use_fahrenheit ? 'F' : 'C';
      lv_label_set_text_fmt(lbl_today_temp, "%.0f°%c", t_now, unit);
      lv_label_set_text_fmt(lbl_today_feels_like, "%s %.0f°%c", strings->feels_like_temp, t_ap, unit);
      
      // Update wind screen labels
      const char* wind_unit = use_fahrenheit ? "mph" : "km/h";
      lv_label_set_text_fmt(lbl_wind_speed_value, "%.1f %s", wind_speed, wind_unit);
      lv_label_set_text_fmt(lbl_wind_direction_value, "%s", degrees_to_cardinal(wind_direction));
      lv_label_set_text_fmt(lbl_wind_gusts_value, "%.1f %s", wind_gusts, wind_unit);
      lv_label_set_text_fmt(lbl_uv_index_value, "%.1f", uv_index);
      lv_label_set_text_fmt(lbl_cloud_cover_value, "%d%%", cloud_cover);
      
      // Fetch air quality data (separate API with separate HTTPClient)
      HTTPClient http_aqi;
      String aqi_url = String("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=")
                     + latitude + "&longitude=" + longitude
                     + "&current=european_aqi,alder_pollen";
      http_aqi.begin(aqi_url);
      if (http_aqi.GET() == HTTP_CODE_OK) {
        DynamicJsonDocument aqi_doc(2048);
        if (deserializeJson(aqi_doc, http_aqi.getString()) == DeserializationError::Ok) {
          int aqi = aqi_doc["current"]["european_aqi"].as<int>();
          const char* aqi_label = "Good";
          if (aqi > 100) aqi_label = "Very Poor";
          else if (aqi > 75) aqi_label = "Poor";
          else if (aqi > 50) aqi_label = "Moderate";
          else if (aqi > 25) aqi_label = "Fair";
          lv_label_set_text_fmt(lbl_air_quality_value, "%s", aqi_label);
          
          // Pollen count (alder pollen as representative)
          int pollen = aqi_doc["current"]["alder_pollen"].as<int>();
          const char* pollen_label = "Low";
          if (pollen > 100) pollen_label = "Very High";
          else if (pollen > 50) pollen_label = "High";
          else if (pollen > 20) pollen_label = "Moderate";
          lv_label_set_text_fmt(lbl_pollen_count_value, "%s", pollen_label);
        } else {
          lv_label_set_text(lbl_air_quality_value, "N/A");
          lv_label_set_text(lbl_pollen_count_value, "N/A");
        }
      } else {
        lv_label_set_text(lbl_air_quality_value, "N/A");
        lv_label_set_text(lbl_pollen_count_value, "N/A");
      }
      http_aqi.end();
      
      // Update precipitation screen labels
      const char* precip_unit = use_fahrenheit ? "in" : "mm";
      const char* vis_unit = use_fahrenheit ? "mi" : "km";
      const char* pressure_unit = use_fahrenheit ? "inHg" : "hPa";
      float precip_display = precipitation;
      float vis_display = visibility / 1000.0; // Convert meters to km
      float pressure_display = pressure;
      float dew_point_display = dew_point;
      if (use_fahrenheit) {
        precip_display = precipitation / 25.4; // mm to inches
        vis_display = visibility / 1609.34; // meters to miles
        pressure_display = pressure * 0.02953; // hPa to inHg
        dew_point_display = dew_point * 9.0 / 5.0 + 32.0; // C to F
      }
      lv_label_set_text_fmt(lbl_precipitation_value, "%.1f %s", precip_display, precip_unit);
      lv_label_set_text_fmt(lbl_humidity_value, "%d%%", humidity);
      lv_label_set_text_fmt(lbl_dew_point_value, "%.0f°%c", dew_point_display, unit);
      lv_label_set_text_fmt(lbl_visibility_value, "%.1f %s", vis_display, vis_unit);
      lv_label_set_text_fmt(lbl_pressure_value, "%.0f %s", pressure_display, pressure_unit);
      
      lv_img_set_src(img_today_icon, choose_image(code_now, is_day));

      JsonArray times = doc["daily"]["time"].as<JsonArray>();
      JsonArray tmin = doc["daily"]["temperature_2m_min"].as<JsonArray>();
      JsonArray tmax = doc["daily"]["temperature_2m_max"].as<JsonArray>();
      JsonArray weather_codes = doc["daily"]["weather_code"].as<JsonArray>();
      JsonArray sunrise_times = doc["daily"]["sunrise"].as<JsonArray>();
      JsonArray sunset_times = doc["daily"]["sunset"].as<JsonArray>();

      for (int i = 0; i < 7; i++) {
        const char *date = times[i];
        int year = atoi(date + 0);
        int mon = atoi(date + 5);
        int dayd = atoi(date + 8);
        int dow = day_of_week(year, mon, dayd);
        const char *dayStr = (i == 0 && current_language != LANG_FR) ? strings->today : strings->weekdays[dow];

        float mn = tmin[i].as<float>();
        float mx = tmax[i].as<float>();
        if (use_fahrenheit) {
          mn = mn * 9.0 / 5.0 + 32.0;
          mx = mx * 9.0 / 5.0 + 32.0;
        }

        lv_label_set_text_fmt(lbl_daily_day[i], "%s", dayStr);
        lv_label_set_text_fmt(lbl_daily_high[i], "%.0f°%c", mx, unit);
        lv_label_set_text_fmt(lbl_daily_low[i], "%.0f°%c", mn, unit);
        lv_img_set_src(img_daily[i], choose_icon(weather_codes[i].as<int>(), (i == 0) ? is_day : 1));
        
        // Populate Sun & Moon screen
        const char *sunrise = sunrise_times[i].as<const char*>();
        const char *sunset = sunset_times[i].as<const char*>();
        int sunrise_hour = atoi(sunrise + 11);
        int sunrise_min = atoi(sunrise + 14);
        int sunset_hour = atoi(sunset + 11);
        int sunset_min = atoi(sunset + 14);
        
        // Format times with R (Rise) and S (Set) prefixes
        char sunrise_str[12], sunset_str[12];
        if (use_24_hour) {
          snprintf(sunrise_str, sizeof(sunrise_str), "R %02d:%02d", sunrise_hour, sunrise_min);
          snprintf(sunset_str, sizeof(sunset_str), "S %02d:%02d", sunset_hour, sunset_min);
        } else {
          int sr_h = sunrise_hour % 12; if(sr_h == 0) sr_h = 12;
          int ss_h = sunset_hour % 12; if(ss_h == 0) ss_h = 12;
          snprintf(sunrise_str, sizeof(sunrise_str), "R %d:%02d%s", sr_h, sunrise_min, sunrise_hour < 12 ? "a" : "p");
          snprintf(sunset_str, sizeof(sunset_str), "S %d:%02d%s", ss_h, sunset_min, sunset_hour < 12 ? "a" : "p");
        }
        
        lv_label_set_text(lbl_sun_day[i], dayStr);
        lv_label_set_text(lbl_sunrise[i], sunrise_str);
        lv_label_set_text(lbl_sunset[i], sunset_str);
        
        // Populate Moon Phase screen
        MoonPhase moon_phase = calculate_moon_phase(year, mon, dayd);
        lv_label_set_text(lbl_moon_day[i], dayStr);
        lv_label_set_text(lbl_moon_phase_text[i], get_moon_phase_abbr(moon_phase));
        set_moon_icon(img_moon_phase[i], moon_phase);
      }
      
      // Calculate dynamic Y-axis range for temperature chart FIRST
      float temp_min = tmin[0].as<float>();
      float temp_max = tmax[0].as<float>();
      for (int i = 1; i < 7; i++) {
        float t_min = tmin[i].as<float>();
        float t_max = tmax[i].as<float>();
        if (t_min < temp_min) temp_min = t_min;
        if (t_max > temp_max) temp_max = t_max;
      }
      
      // Apply Fahrenheit conversion to min/max if needed
      if (use_fahrenheit) {
        temp_min = temp_min * 9.0 / 5.0 + 32.0;
        temp_max = temp_max * 9.0 / 5.0 + 32.0;
      }
      
      // Add 5-degree padding to range
      int y_min = (int)(temp_min - 5);
      int y_max = (int)(temp_max + 5);
      lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
      
      // NOW populate the chart data with converted temps
      for (int i = 0; i < 7; i++) {
        float mn = tmin[i].as<float>();
        float mx = tmax[i].as<float>();
        if (use_fahrenheit) {
          mn = mn * 9.0 / 5.0 + 32.0;
          mx = mx * 9.0 / 5.0 + 32.0;
        }
        temp_series_high->y_points[i] = (lv_coord_t)mx;
        temp_series_low->y_points[i] = (lv_coord_t)mn;
        
        // Update day letter labels for X-axis
        const char *date = times[i];
        int year = atoi(date + 0);
        int mon = atoi(date + 5);
        int dayd = atoi(date + 8);
        int dow = day_of_week(year, mon, dayd);
        const char *dayStr = strings->weekdays[dow];
        char dayLetter[2] = {dayStr[0], '\0'};  // First letter only
        lv_label_set_text(lbl_day_letters[i], dayLetter);
      }
      
      // Refresh chart first so internal coordinates are calculated
      lv_chart_refresh(temp_chart);
      
      // Update Y-axis labels with first day's temps (today's high/low)
      float first_high = tmax[0].as<float>();
      float first_low = tmin[0].as<float>();
      if (use_fahrenheit) {
        first_high = first_high * 9.0 / 5.0 + 32.0;
        first_low = first_low * 9.0 / 5.0 + 32.0;
      }
      lv_label_set_text_fmt(lbl_temp_high_val, "%.0f°", first_high);
      lv_label_set_text_fmt(lbl_temp_low_val, "%.0f°", first_low);
      
      // Position temp labels relative to the chart object, not the box
      // Chart has 10px padding, 115px usable height for plot area
      float chart_height = 115.0;
      float temp_range = (float)(y_max - y_min);
      
      // Calculate pixel position within chart plot area (0 = top, 115 = bottom)
      float high_pixel_pos = ((y_max - first_high) / temp_range) * chart_height;
      float low_pixel_pos = ((y_max - first_low) / temp_range) * chart_height;
      
      // Position labels relative to TOP of chart, accounting for padding
      lv_obj_set_parent(lbl_temp_high_val, box_temp_chart);
      lv_obj_set_parent(lbl_temp_low_val, box_temp_chart);
      lv_obj_align_to(lbl_temp_high_val, temp_chart, LV_ALIGN_OUT_LEFT_TOP, -5, (int)high_pixel_pos + 10);
      lv_obj_align_to(lbl_temp_low_val, temp_chart, LV_ALIGN_OUT_LEFT_TOP, -5, (int)low_pixel_pos + 10);
      
      // Position day letters relative to chart bottom, aligned with X positions
      // Chart width 180px - 20px padding = 160px plot width, 7 points across
      float plot_width = 160.0;
      float point_spacing = plot_width / 6.0;  // 6 gaps between 7 points
      
      for (int i = 0; i < 7; i++) {
        lv_obj_set_parent(lbl_day_letters[i], box_temp_chart);
        int x_pos = (int)(i * point_spacing);
        lv_obj_align_to(lbl_day_letters[i], temp_chart, LV_ALIGN_OUT_BOTTOM_LEFT, x_pos + 10, 2);
      }

      JsonArray hours = doc["hourly"]["time"].as<JsonArray>();
      JsonArray hourly_temps = doc["hourly"]["temperature_2m"].as<JsonArray>();
      JsonArray precipitation_probabilities = doc["hourly"]["precipitation_probability"].as<JsonArray>();
      JsonArray hourly_weather_codes = doc["hourly"]["weather_code"].as<JsonArray>();
      JsonArray hourly_is_day = doc["hourly"]["is_day"].as<JsonArray>();

      for (int i = 0; i < 7; i++) {
        const char *date = hours[i];  // "YYYY-MM-DD"
        int hour = atoi(date + 11);
        int minute = atoi(date + 14);
        String hour_name = hour_of_day(hour);

        float precipitation_probability = precipitation_probabilities[i].as<float>();
        float temp = hourly_temps[i].as<float>();
        if (use_fahrenheit) {
          temp = temp * 9.0 / 5.0 + 32.0;
        }

        if (i == 0 && current_language != LANG_FR) {
          lv_label_set_text(lbl_hourly[i], strings->now);
        } else {
          lv_label_set_text(lbl_hourly[i], hour_name.c_str());
        }
        lv_label_set_text_fmt(lbl_precipitation_probability[i], "%.0f%%", precipitation_probability);
        lv_label_set_text_fmt(lbl_hourly_temp[i], "%.0f°%c", temp, unit);
        lv_img_set_src(img_hourly[i], choose_icon(hourly_weather_codes[i].as<int>(), hourly_is_day[i].as<int>()));
      }


    } else {
      Serial.println("JSON parse failed on result from " + url);
    }
  } else {
    Serial.println("HTTP GET failed at " + url);
  }
  http.end();
}

const lv_img_dsc_t* choose_image(int code, int is_day) {
  switch (code) {
    // Clear sky
    case  0:
      return is_day
        ? &image_sunny
        : &image_clear_night;

    // Mainly clear
    case  1:
      return is_day
        ? &image_mostly_sunny
        : &image_mostly_clear_night;

    // Partly cloudy
    case  2:
      return is_day
        ? &image_partly_cloudy
        : &image_partly_cloudy_night;

    // Overcast
    case  3:
      return &image_cloudy;

    // Fog / mist
    case 45:
    case 48:
      return &image_haze_fog_dust_smoke;

    // Drizzle (light → dense)
    case 51:
    case 53:
    case 55:
      return &image_drizzle;

    // Freezing drizzle
    case 56:
    case 57:
      return &image_sleet_hail;

    // Rain: slight showers
    case 61:
      return is_day
        ? &image_scattered_showers_day
        : &image_scattered_showers_night;

    // Rain: moderate
    case 63:
      return &image_showers_rain;

    // Rain: heavy
    case 65:
      return &image_heavy_rain;

    // Freezing rain
    case 66:
    case 67:
      return &image_wintry_mix_rain_snow;

    // Snow fall (light, moderate, heavy) & snow showers (light)
    case 71:
    case 73:
    case 75:
    case 85:
      return &image_snow_showers_snow;

    // Snow grains
    case 77:
      return &image_flurries;

    // Rain showers (slight → moderate)
    case 80:
    case 81:
      return is_day
        ? &image_scattered_showers_day
        : &image_scattered_showers_night;

    // Rain showers: violent
    case 82:
      return &image_heavy_rain;

    // Heavy snow showers
    case 86:
      return &image_heavy_snow;

    // Thunderstorm (light)
    case 95:
      return is_day
        ? &image_isolated_scattered_tstorms_day
        : &image_isolated_scattered_tstorms_night;

    // Thunderstorm with hail
    case 96:
    case 99:
      return &image_strong_tstorms;

    // Fallback for any other code
    default:
      return is_day
        ? &image_mostly_cloudy_day
        : &image_mostly_cloudy_night;
  }
}

const lv_img_dsc_t* choose_icon(int code, int is_day) {
  switch (code) {
    // Clear sky
    case  0:
      return is_day
        ? &icon_sunny
        : &icon_clear_night;

    // Mainly clear
    case  1:
      return is_day
        ? &icon_mostly_sunny
        : &icon_mostly_clear_night;

    // Partly cloudy
    case  2:
      return is_day
        ? &icon_partly_cloudy
        : &icon_partly_cloudy_night;

    // Overcast
    case  3:
      return &icon_cloudy;

    // Fog / mist
    case 45:
    case 48:
      return &icon_haze_fog_dust_smoke;

    // Drizzle (light → dense)
    case 51:
    case 53:
    case 55:
      return &icon_drizzle;

    // Freezing drizzle
    case 56:
    case 57:
      return &icon_sleet_hail;

    // Rain: slight showers
    case 61:
      return is_day
        ? &icon_scattered_showers_day
        : &icon_scattered_showers_night;

    // Rain: moderate
    case 63:
      return &icon_showers_rain;

    // Rain: heavy
    case 65:
      return &icon_heavy_rain;

    // Freezing rain
    case 66:
    case 67:
      return &icon_wintry_mix_rain_snow;

    // Snow fall (light, moderate, heavy) & snow showers (light)
    case 71:
    case 73:
    case 75:
    case 85:
      return &icon_snow_showers_snow;

    // Snow grains
    case 77:
      return &icon_flurries;

    // Rain showers (slight → moderate)
    case 80:
    case 81:
      return is_day
        ? &icon_scattered_showers_day
        : &icon_scattered_showers_night;

    // Rain showers: violent
    case 82:
      return &icon_heavy_rain;

    // Heavy snow showers
    case 86:
      return &icon_heavy_snow;

    // Thunderstorm (light)
    case 95:
      return is_day
        ? &icon_isolated_scattered_tstorms_day
        : &icon_isolated_scattered_tstorms_night;

    // Thunderstorm with hail
    case 96:
    case 99:
      return &icon_strong_tstorms;

    // Fallback for any other code
    default:
      return is_day
        ? &icon_mostly_cloudy_day
        : &icon_mostly_cloudy_night;
  }
}
