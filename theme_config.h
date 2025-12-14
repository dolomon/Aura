// theme_config.h - Color Theme Configuration
// Version: 1.3.0
// Forked from Surrey-Homeware/Aura

#ifndef THEME_CONFIG_H
#define THEME_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

// Theme color structure
struct ThemeColors {
  uint32_t bg_top;           // Background gradient top (0x4c8cb9)
  uint32_t bg_bottom;        // Background gradient bottom (0xa6cdec)
  uint32_t text_primary;     // Primary text color (0xFFFFFF)
  uint32_t text_secondary;   // Secondary text color (0xe4ffff)
  uint32_t text_tertiary;    // Tertiary text color (0xb9ecff)
  uint32_t text_low;         // Low temperature text color (0xb9ecff)
  uint32_t text_clock;       // Clock text color (0xb9ecff)
  uint32_t box_bg;           // Forecast box background (0x5e9bc8)
  uint32_t button_primary;   // Primary button color
  uint32_t button_secondary; // Secondary button color
};

// Default theme (original colors)
const ThemeColors DEFAULT_THEME = {
  0x4c8cb9,  // bg_top
  0xa6cdec,  // bg_bottom
  0xFFFFFF,  // text_primary
  0xe4ffff,  // text_secondary
  0xb9ecff,  // text_tertiary
  0xb9ecff,  // text_low
  0xb9ecff,  // text_clock
  0x5e9bc8,  // box_bg
  0x4CAF50,  // button_primary (green)
  0x9E9E9E   // button_secondary (grey)
};

// Preset themes
const ThemeColors THEME_SUNSET = {
  0xFF6B35, 0xFFAA80, 0xFFFFFF, 0xFFE5D9, 0xFFCCB3, 0xFFB399, 0xFFCC99, 0xFF8C5A, 0x4CAF50, 0x9E9E9E
};

const ThemeColors THEME_OCEAN = {
  0x006994, 0x33B5E5, 0xFFFFFF, 0xCCF2FF, 0x99E5FF, 0x66D9FF, 0x99E5FF, 0x0099CC, 0x4CAF50, 0x9E9E9E
};

const ThemeColors THEME_FOREST = {
  0x2D5016, 0x73A942, 0xFFFFFF, 0xE8F5E3, 0xD1EBCC, 0xBAE1B5, 0xD1EBCC, 0x4A7C2C, 0x4CAF50, 0x9E9E9E
};

const ThemeColors THEME_LAVENDER = {
  0x6A4C93, 0xA78BCC, 0xFFFFFF, 0xF4EEFF, 0xE5D4FF, 0xD6BFFF, 0xE5D4FF, 0x8B6BB7, 0x4CAF50, 0x9E9E9E
};

const ThemeColors THEME_DESERT = {
  0xC77026, 0xE8A87C, 0xFFFFFF, 0xFFEFE0, 0xFFDFC6, 0xFFCFAC, 0xFFDFC6, 0xD98E4A, 0x4CAF50, 0x9E9E9E
};

const ThemeColors THEME_ARCTIC = {
  0x4A90A4, 0xB4E1F0, 0xFFFFFF, 0xF0FAFF, 0xD9F2FF, 0xC2EAFF, 0xD9F2FF, 0x6BADC4, 0x4CAF50, 0x9E9E9E
};

class ThemeManager {
private:
  ThemeColors current;
  Preferences* prefs;
  
public:
  ThemeManager() {}
  
  void init(Preferences* p) {
    prefs = p;
    load();
  }
  
  void load() {
    current.bg_top = prefs->getUInt("theme_bg_top", DEFAULT_THEME.bg_top);
    current.bg_bottom = prefs->getUInt("theme_bg_bot", DEFAULT_THEME.bg_bottom);
    current.text_primary = prefs->getUInt("theme_txt_pri", DEFAULT_THEME.text_primary);
    current.text_secondary = prefs->getUInt("theme_txt_sec", DEFAULT_THEME.text_secondary);
    current.text_tertiary = prefs->getUInt("theme_txt_ter", DEFAULT_THEME.text_tertiary);
    current.text_low = prefs->getUInt("theme_txt_low", DEFAULT_THEME.text_low);
    current.text_clock = prefs->getUInt("theme_txt_clk", DEFAULT_THEME.text_clock);
    current.box_bg = prefs->getUInt("theme_box_bg", DEFAULT_THEME.box_bg);
    current.button_primary = prefs->getUInt("theme_btn_pri", DEFAULT_THEME.button_primary);
    current.button_secondary = prefs->getUInt("theme_btn_sec", DEFAULT_THEME.button_secondary);
  }
  
  void save() {
    prefs->putUInt("theme_bg_top", current.bg_top);
    prefs->putUInt("theme_bg_bot", current.bg_bottom);
    prefs->putUInt("theme_txt_pri", current.text_primary);
    prefs->putUInt("theme_txt_sec", current.text_secondary);
    prefs->putUInt("theme_txt_ter", current.text_tertiary);
    prefs->putUInt("theme_txt_low", current.text_low);
    prefs->putUInt("theme_txt_clk", current.text_clock);
    prefs->putUInt("theme_box_bg", current.box_bg);
    prefs->putUInt("theme_btn_pri", current.button_primary);
    prefs->putUInt("theme_btn_sec", current.button_secondary);
  }
  
  void setTheme(const ThemeColors& theme) {
    current = theme;
    save();
  }
  
  void setCustomColor(const char* key, uint32_t color) {
    if (strcmp(key, "bg_top") == 0) current.bg_top = color;
    else if (strcmp(key, "bg_bottom") == 0) current.bg_bottom = color;
    else if (strcmp(key, "text_primary") == 0) current.text_primary = color;
    else if (strcmp(key, "text_secondary") == 0) current.text_secondary = color;
    else if (strcmp(key, "text_tertiary") == 0) current.text_tertiary = color;
    else if (strcmp(key, "text_low") == 0) current.text_low = color;
    else if (strcmp(key, "text_clock") == 0) current.text_clock = color;
    else if (strcmp(key, "box_bg") == 0) current.box_bg = color;
    else if (strcmp(key, "button_primary") == 0) current.button_primary = color;
    else if (strcmp(key, "button_secondary") == 0) current.button_secondary = color;
    save();
  }
  
  ThemeColors get() { return current; }
  
  uint32_t getBgTop() { return current.bg_top; }
  uint32_t getBgBottom() { return current.bg_bottom; }
  uint32_t getTextPrimary() { return current.text_primary; }
  uint32_t getTextSecondary() { return current.text_secondary; }
  uint32_t getTextTertiary() { return current.text_tertiary; }
  uint32_t getTextLow() { return current.text_low; }
  uint32_t getTextClock() { return current.text_clock; }
  uint32_t getBoxBg() { return current.box_bg; }
  uint32_t getButtonPrimary() { return current.button_primary; }
  uint32_t getButtonSecondary() { return current.button_secondary; }
};

#endif
