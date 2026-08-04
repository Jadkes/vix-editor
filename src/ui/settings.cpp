/*
 * settings.cpp - Theme table, JSON-ish settings load/save, settings panel
 *
 * Settings live in ~/.config/vix/settings.json. The loader is a small
 * hand-rolled parser (no JSON dependency) that reads each known key and
 * clamps ranges, so a corrupt or hand-written file can never crash the
 * editor or put a value out of bounds.
 */
#include "settings.hpp"
#include <ncurses.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <cctype>

namespace fs = std::filesystem;

const int THEME_COUNT = 4;
const char* THEME_NAMES[4] = {"Monokai", "Dracula", "Nord", "Solarized Light"};

const Theme themes[4] = {
    {"Monokai",
     {7,5,3,8,8,7,1,6,7,8,0,8,7,0,8,7},
     {-1,-1,-1,-1,-1,5,-1,-1,1,-1,6,-1,5,3,-1,-1}},
    {"Dracula",
     {7,13,11,14,8,15,9,6,7,8,0,8,7,0,8,7},
     {-1,-1,-1,-1,-1,13,-1,-1,1,-1,6,-1,5,11,-1,-1}},
    {"Nord",
     {7,6,2,8,8,7,9,14,7,8,0,15,7,0,8,7},
     {-1,-1,-1,-1,-1,4,-1,-1,1,-1,4,-1,6,3,-1,-1}},
    {"Solarized Light",
     {0,10,6,8,8,7,1,6,7,8,0,8,0,0,0,0},
     {-1,-1,-1,-1,-1,12,-1,-1,1,-1,12,-1,6,3,-1,-1}}
};

static std::string ConfigPath() {
    const char* home = getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.config/vix/settings.json";
}

static void SkipWS(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
}

static std::string ParseStr(const std::string& s, size_t& i) {
    std::string val;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) { i++; val += s[i]; }
        else val += s[i];
        i++;
    }
    if (i < s.size()) i++;
    return val;
}

static std::string ParseVal(const std::string& s, size_t& i) {
    SkipWS(s, i);
    if (i >= s.size()) return "";
    if (s[i] == '"') { i++; return "\"" + ParseStr(s, i); }
    std::string val;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']'
           && s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') val += s[i++];
    return val;
}

void LoadSettings(Settings& s) {
    std::string path = ConfigPath();
    if (!fs::exists(path)) return;
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::ostringstream buf; buf << f.rdbuf();
    std::string text = buf.str();
    size_t start = text.find('{'), end = text.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start) return;
    start++; size_t i = start;
    while (i < end) {
        SkipWS(text, i);
        if (i >= end || text[i] == '}') break;
        if (text[i] != '"') { i++; continue; }
        i++;
        std::string key = ParseStr(text, i);
        SkipWS(text, i);
        if (i >= end || text[i] != ':') continue;
        i++;
        std::string val = ParseVal(text, i);
        SkipWS(text, i);
        if (i < end && text[i] == ',') i++;
        if (key == "tab_width" && !val.empty() && val[0] != '"') { try { s.tab_width = std::stoi(val); if (s.tab_width < 1) s.tab_width = 2; } catch(...) {} }
        else if (key == "auto_save_interval" && !val.empty() && val[0] != '"') { try { s.auto_save_interval = std::stoi(val); } catch(...) {} }
        else if (key == "line_numbers") s.line_numbers = (val == "true");
        else if (key == "auto_indent") s.auto_indent = (val == "true");
        else if (key == "theme" && !val.empty() && val[0] != '"') { try { int t = std::stoi(val); if (t >= 0 && t < THEME_COUNT) s.theme = t; } catch(...) {} }
        else if (key == "default_language" && !val.empty() && val[0] != '"') { try { int l = std::stoi(val); if (l >= 0 && l <= 6) s.default_language = l; } catch(...) {} }
        else if (key == "word_wrap") s.word_wrap = (val == "true");
    }
}

void SaveSettings(const Settings& s) {
    std::string path = ConfigPath();
    std::string dir = path.substr(0, path.find_last_of("/"));
    try { fs::create_directories(dir); } catch(...) { return; }
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "{\n";
    f << "    \"tab_width\": " << s.tab_width << ",\n";
    f << "    \"auto_save_interval\": " << s.auto_save_interval << ",\n";
    f << "    \"line_numbers\": " << (s.line_numbers ? "true" : "false") << ",\n";
    f << "    \"auto_indent\": " << (s.auto_indent ? "true" : "false") << ",\n";
    f << "    \"theme\": " << s.theme << ",\n";
    f << "    \"default_language\": " << s.default_language << ",\n";
    f << "    \"word_wrap\": " << (s.word_wrap ? "true" : "false") << "\n";
    f << "}\n";
}

void ApplyTheme(const Theme& t) {
    for (int cp = 1; cp <= 16; cp++) init_pair(cp, t.fg[cp - 1], t.bg[cp - 1]);
    bkgd(COLOR_PAIR(1));
    clearok(stdscr, TRUE);
}

static const char* LABELS[] = {
    "Tab Width", "Auto-Save Interval", "Line Numbers",
    "Auto-Indent", "Theme", "Default Language", "Word Wrap"
};
static constexpr int LABEL_COUNT = 7;
static constexpr int LANG_COUNT  = 7;
static constexpr int TAB_W_COUNT = 3;
static constexpr int SAVE_I_COUNT = 6;

static const int TAB_W[] = {2, 4, 8};
static const int SAVE_I[] = {0, 5, 10, 20, 30, 60};
static const int LANG_IDS[] = {1, 6, 2, 3, 4, 5, 0};
static const char* LANG_NS[] = {"C++", "C", "Python", "JavaScript", "Rust", "Go", "Text"};

// Settings-panel geometry.
static constexpr int PANEL_W  = 50;
static constexpr int PANEL_H  = 14;
static constexpr int LABEL_W  = 24;  // setting-name column width
static constexpr int VALUE_W  = 16;  // setting-value column width
static constexpr int HINT_ROW = 10;  // first row of the footer key hints

static std::string ValStr(const Settings& s, int row) {
    switch (row) {
        case 0: return std::to_string(s.tab_width);
        case 1: return s.auto_save_interval == 0 ? "OFF" : std::to_string(s.auto_save_interval) + "s";
        case 2: return s.line_numbers ? "Yes" : "No";
        case 3: return s.auto_indent ? "Yes" : "No";
        case 4: return (s.theme >= 0 && s.theme < THEME_COUNT) ? THEME_NAMES[s.theme] : "?";
        case 5: for (int i = 0; i < LANG_COUNT; i++) if (LANG_IDS[i] == s.default_language) return LANG_NS[i]; return "?";
        case 6: return s.word_wrap ? "Yes" : "No";
        default: return "";
    }
}

static void Cycle(Settings& s, int row, int dir) {
    auto cycle = [&](int& val, const int* arr, int n) {
        int cur = 0;
        for (int i = 0; i < n; i++) if (arr[i] == val) { cur = i; break; }
        cur = (cur + dir + n) % n;
        val = arr[cur];
    };
    switch (row) {
        case 0: cycle(s.tab_width, TAB_W, TAB_W_COUNT); break;
        case 1: cycle(s.auto_save_interval, SAVE_I, SAVE_I_COUNT); break;
        case 2: s.line_numbers = !s.line_numbers; break;
        case 3: s.auto_indent = !s.auto_indent; break;
        case 4: { int old = s.theme; s.theme = (s.theme + dir + THEME_COUNT) % THEME_COUNT; if (s.theme != old) ApplyTheme(themes[s.theme]); break; }
        case 5: { int cur = 0; for (int i = 0; i < LANG_COUNT; i++) if (LANG_IDS[i] == s.default_language) { cur = i; break; } cur = (cur + dir + LANG_COUNT) % LANG_COUNT; s.default_language = LANG_IDS[cur]; break; }
        case 6: s.word_wrap = !s.word_wrap; break;
    }
    SaveSettings(s);
}

void ShowSettingsPanel(Settings& s) {
    int my, mx;
    getmaxyx(stdscr, my, mx);
    int wy = (my - PANEL_H) / 2, wx = (mx - PANEL_W) / 2;
    if (wy < 0) wy = 0;
    if (wx < 0) wx = 0;
    WINDOW* pw = newwin(PANEL_H, PANEL_W, wy, wx);
    if (!pw) return;
    keypad(pw, TRUE);
    int sel = 0;
    bool open = true;
    while (open) {
        werase(pw); box(pw, 0, 0);
        mvwprintw(pw, 0, 2, " VIX SETTINGS ");
        for (int i = 0; i < LABEL_COUNT; i++) {
            int ry = 2 + i;
            std::string val = ValStr(s, i);
            if (i == sel) { wattron(pw, A_REVERSE); mvwprintw(pw, ry, 2, "%-*s %-*s", LABEL_W, LABELS[i], VALUE_W, val.c_str()); wattroff(pw, A_REVERSE); }
            else mvwprintw(pw, ry, 2, " %-*s %-*s", LABEL_W - 1, LABELS[i], VALUE_W, val.c_str());
        }
        mvwprintw(pw, HINT_ROW, 2, " %-*s", PANEL_W - 4, "");
        mvwprintw(pw, HINT_ROW + 1, 2, " %-*s", PANEL_W - 4, "");
        mvwprintw(pw, HINT_ROW, 2, " \x18\x19 Navigate  \x1b\x1a/Enter Change  Esc Close");
        mvwprintw(pw, HINT_ROW + 2, 2, " Settings saved to ~/.config/vix/settings.json");
        wrefresh(pw);
        int ch = wgetch(pw);
        switch (ch) {
            case KEY_UP: sel = (sel - 1 + LABEL_COUNT) % LABEL_COUNT; break;
            case KEY_DOWN: sel = (sel + 1) % LABEL_COUNT; break;
            case KEY_LEFT: Cycle(s, sel, -1); break;
            case KEY_RIGHT: case '\n': case KEY_ENTER: Cycle(s, sel, 1); break;
            case 27: open = false; break;
        }
    }
    delwin(pw);
    touchwin(stdscr);
    refresh();
}
