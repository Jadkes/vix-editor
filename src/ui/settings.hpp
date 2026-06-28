/*
 * settings - Persistent settings and themes for vix editor
 *
 * Configuration file I/O, theme definitions, and the interactive
 *          settings panel that opens inside the editor (F2).
 *
 * Settings live at ~/.config/vix/settings.json as a flat JSON object.
 *         A hand-written parser handles the 7 scalars — no JSON library needed.
 *         Themes are compile-time arrays of ncurses color pairs applied via
 *         init_pair(). The settings panel is a nested ncurses event loop on
 *         its own WINDOW*, same pattern as the Help popup.
 */

#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <string>

struct Theme {
    std::string name;
    int fg[14];  // foreground color index for each CP_* (indices 0-13 = CP_DEFAULT-1 .. CP_SEARCH-1)
    int bg[14];  // background (-1 = terminal default)
};

extern const Theme themes[4];
extern const int THEME_COUNT;
extern const char* THEME_NAMES[4];

void ApplyTheme(const Theme& t);

struct Settings {
    int tab_width             = 4;
    int auto_save_interval    = 20;    // 0 = disabled
    bool line_numbers         = true;
    bool auto_indent          = true;
    int  theme                = 0;     // index into themes[]
    int  default_language     = 1;     // lang_id (1=C++, 6=C, 2=Py, 3=JS, 4=Rust, 5=Go, 0=Text)
    bool word_wrap            = false; // stored but not wired (future)
};

void LoadSettings(Settings& s);
void SaveSettings(const Settings& s);

void ShowSettingsPanel(Settings& s);

#endif // SETTINGS_HPP
