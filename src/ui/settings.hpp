/*
 * settings.hpp - User preferences and theme table
 *
 * Settings are struct fields with default values; they are loaded from and
 * saved to a JSON file and edited from the F2 panel. The theme array maps
 * the color-pair constants used by the editor to fg/bg curses indices.
 */
#ifndef VIX_SETTINGS_HPP
#define VIX_SETTINGS_HPP

#include <string>
struct Theme {
    std::string name;
    int fg[16];
    int bg[16];
};

extern const Theme themes[4];
extern const int THEME_COUNT;
extern const char* THEME_NAMES[4];

void ApplyTheme(const Theme& t);

struct Settings {
    int tab_width = 4;
    int auto_save_interval = 20;
    bool line_numbers = true;
    bool auto_indent = true;
    int theme = 0;
    int default_language = 1;
    bool word_wrap = false;
};

void LoadSettings(Settings& s);
void SaveSettings(const Settings& s);
void ShowSettingsPanel(Settings& s);

#endif
