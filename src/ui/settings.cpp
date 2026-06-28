/*
 * settings - Persistent settings and themes for vix editor
 *
 * Implements JSON config I/O, theme definitions, theme application,
 * and the interactive F2 settings panel.
 */

#include "settings.hpp"
#include <ncurses.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <cctype>

namespace fs = std::filesystem;

// Each entry defines fg/bg for CP_DEFAULT(1) through CP_SEARCH(14).
// Indices 0-13 map to CP_*-1.  -1 = terminal default background.
// Bright colors (8-15) used where available.

const int THEME_COUNT = 4;
const char* THEME_NAMES[4] = { "Monokai", "Dracula", "Nord", "Solarized Light" };

const Theme themes[4] = {
    {
        "Monokai",
        /* default  keyword  string   comment  linenum  status    orange  cyan    error  sidebar  select   ghost   match    search */
        { 7,       5,       3,       8,       8,       7,        1,      6,      7,     8,       0,       8,      7,       0       },
        { -1,      -1,      -1,      -1,      -1,      5,        -1,     -1,     1,     -1,      6,       -1,     5,       3       }
    },
    {
        "Dracula",
        /* default  keyword  string   comment  linenum  status    orange  cyan    error  sidebar  select   ghost   match    search */
        { 7,       13,      11,      14,      8,       15,       9,      6,      7,     8,       0,       8,      7,       0       },
        { -1,      -1,      -1,      -1,      -1,      13,       -1,     -1,     1,     -1,      6,       -1,     5,       11      }
    },
    {
        "Nord",
        /* default  keyword  string   comment  linenum  status    orange  cyan    error  sidebar  select   ghost   match    search */
        { 7,       6,       2,       8,       8,       7,        9,      14,     7,     8,       0,       15,     7,       0       },
        { -1,      -1,      -1,      -1,      -1,      4,        -1,     -1,     1,     -1,      4,       -1,     6,       3       }
    },
    {
        "Solarized Light",
        /* default  keyword  string   comment  linenum  status    orange  cyan    error  sidebar  select   ghost   match    search */
        { 0,       10,      6,       8,       8,       7,        1,      6,      7,     8,       0,       8,      0,       0       },
        { -1,      -1,      -1,      -1,      -1,      12,       -1,     -1,     1,     -1,      12,      -1,     6,       3       }
    }
};


static std::string ConfigPath()
{
    const char* home = getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.config/vix/settings.json";
}
// Handles a flat { "key": value, ... } object.
// Strings, integers, and booleans.  No nesting, no arrays, no escapes.

static void SkipWhitespace(const std::string& s, size_t& i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        i++;
}

static std::string ParseString(const std::string& s, size_t& i)
{
    // caller already consumed opening "
    std::string val;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
            i++; // skip backslash
            if (s[i] == 'n') val += '\n';
            else if (s[i] == 't') val += '\t';
            else val += s[i];
        } else {
            val += s[i];
        }
        i++;
    }
    if (i < s.size()) i++; // skip closing "
    return val;
}

static std::string ParseValue(const std::string& s, size_t& i)
{
    SkipWhitespace(s, i);
    if (i >= s.size()) return "";

    if (s[i] == '"') {
        i++;
        std::string val = ParseString(s, i);
        return "\"" + val + "\""; // keep the quote marker for type disambiguation
    }

    // Boolean or number
    std::string val;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']'
           && s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') {
        val += s[i];
        i++;
    }
    return val;
}

static std::string FileContents(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

void LoadSettings(Settings& s)
{
    std::string path = ConfigPath();
    if (!fs::exists(path)) return; // use defaults

    std::string text = FileContents(path);
    if (text.empty()) return;

    // Find outermost { }
    size_t start = text.find('{');
    size_t end = text.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start)
        return;

    start++;
    size_t i = start;
    while (i < end) {
        SkipWhitespace(text, i);
        if (i >= end || text[i] == '}') break;

        // Parse key
        if (text[i] != '"') { i++; continue; }
        i++;
        std::string key = ParseString(text, i);
        SkipWhitespace(text, i);
        if (i >= end || text[i] != ':') continue;
        i++; // skip ':'
        std::string val = ParseValue(text, i);
        SkipWhitespace(text, i);
        if (i < end && text[i] == ',') i++; // skip comma

        // Map key → Settings field
        if (key == "tab_width" && !val.empty() && val[0] != '"') {
            try { s.tab_width = std::stoi(val); if (s.tab_width < 1) s.tab_width = 2; } catch (...) {}
        } else if (key == "auto_save_interval" && !val.empty() && val[0] != '"') {
            try { s.auto_save_interval = std::stoi(val); } catch (...) {}
        } else if (key == "line_numbers" && !val.empty()) {
            s.line_numbers = (val == "true");
        } else if (key == "auto_indent" && !val.empty()) {
            s.auto_indent = (val == "true");
        } else if (key == "theme" && !val.empty() && val[0] != '"') {
            try { int t = std::stoi(val); if (t >= 0 && t < THEME_COUNT) s.theme = t; } catch (...) {}
        } else if (key == "default_language" && !val.empty() && val[0] != '"') {
            try { int l = std::stoi(val); if (l >= 0 && l <= 6) s.default_language = l; } catch (...) {}
        } else if (key == "word_wrap" && !val.empty()) {
            s.word_wrap = (val == "true");
        }
        // unknown keys silently ignored (forward compat)
    }
}

void SaveSettings(const Settings& s)
{
    std::string path = ConfigPath();
    std::string dir = path.substr(0, path.find_last_of("/"));

    try {
        fs::create_directories(dir);
    } catch (...) { return; }

    std::ostringstream json;
    json << "{\n";
    json << "    \"tab_width\": " << s.tab_width << ",\n";
    json << "    \"auto_save_interval\": " << s.auto_save_interval << ",\n";
    json << "    \"line_numbers\": " << (s.line_numbers ? "true" : "false") << ",\n";
    json << "    \"auto_indent\": " << (s.auto_indent ? "true" : "false") << ",\n";
    json << "    \"theme\": " << s.theme << ",\n";
    json << "    \"default_language\": " << s.default_language << ",\n";
    json << "    \"word_wrap\": " << (s.word_wrap ? "true" : "false") << "\n";
    json << "}\n";

    std::ofstream f(path);
    if (f.is_open()) {
        f << json.str();
    }
}

void ApplyTheme(const Theme& t)
{
    for (int cp = 1; cp <= 14; cp++) {
        init_pair(cp, t.fg[cp - 1], t.bg[cp - 1]);
    }
    bkgd(COLOR_PAIR(1));              // CP_DEFAULT = pair 1
    clearok(stdscr, TRUE);
}

// Interactive overlay opened with F2.
// Nested ncurses loop on its own WINDOW*.

static const char* SETTING_LABELS[] = {
    "Tab Width",
    "Auto-Save Interval",
    "Line Numbers",
    "Auto-Indent",
    "Theme",
    "Default Language",
    "Word Wrap"
};

static const int SETTING_COUNT = 7;

// Valid values for each setting row (for cycling)
static const int TAB_WIDTHS[]    = { 2, 4, 8 };
static const int SAVE_INTERVALS[] = { 0, 5, 10, 20, 30, 60 };
static const int LANG_IDS[]      = { 1, 6, 2, 3, 4, 5, 0 };
static const char* LANG_NAMES[]  = { "C++", "C", "Python", "JavaScript", "Rust", "Go", "Text" };

/* Format a setting's current value into a display string */
static std::string SettingValue(const Settings& s, int row)
{
    switch (row) {
    case 0: return std::to_string(s.tab_width);
    case 1:
        if (s.auto_save_interval == 0) return "OFF";
        return std::to_string(s.auto_save_interval) + "s";
    case 2: return s.line_numbers ? "Yes" : "No";
    case 3: return s.auto_indent ? "Yes" : "No";
    case 4: return (s.theme >= 0 && s.theme < THEME_COUNT) ? THEME_NAMES[s.theme] : "?";
    case 5:
        for (int i = 0; i < (int)(sizeof(LANG_IDS)/sizeof(LANG_IDS[0])); i++)
            if (LANG_IDS[i] == s.default_language) return LANG_NAMES[i];
        return "?";
    case 6: return s.word_wrap ? "Yes" : "No";
    default: return "";
    }
}

/* Cycle a setting in the given direction (+1 or -1) */
static void CycleSetting(Settings& s, int row, int dir)
{
    switch (row) {
    case 0: { // tab_width: cycle 2,4,8
        int cur = 0;
        for (int i = 0; i < (int)(sizeof(TAB_WIDTHS)/sizeof(TAB_WIDTHS[0])); i++)
            if (TAB_WIDTHS[i] == s.tab_width) { cur = i; break; }
        cur = (cur + dir + (int)(sizeof(TAB_WIDTHS)/sizeof(TAB_WIDTHS[0]))) % (sizeof(TAB_WIDTHS)/sizeof(TAB_WIDTHS[0]));
        s.tab_width = TAB_WIDTHS[cur];
        break;
    }
    case 1: { // auto_save_interval: cycle 0,5,10,20,30,60
        int cur = 0;
        for (int i = 0; i < (int)(sizeof(SAVE_INTERVALS)/sizeof(SAVE_INTERVALS[0])); i++)
            if (SAVE_INTERVALS[i] == s.auto_save_interval) { cur = i; break; }
        cur = (cur + dir + (int)(sizeof(SAVE_INTERVALS)/sizeof(SAVE_INTERVALS[0]))) % (sizeof(SAVE_INTERVALS)/sizeof(SAVE_INTERVALS[0]));
        s.auto_save_interval = SAVE_INTERVALS[cur];
        break;
    }
    case 2: s.line_numbers = !s.line_numbers; break;
    case 3: s.auto_indent = !s.auto_indent; break;
    case 4: { // theme
        int old = s.theme;
        s.theme = (s.theme + dir + THEME_COUNT) % THEME_COUNT;
        if (s.theme != old) ApplyTheme(themes[s.theme]);
        break;
    }
    case 5: { // default_language
        int lang_count = sizeof(LANG_IDS)/sizeof(LANG_IDS[0]);
        int cur = 0;
        for (int i = 0; i < lang_count; i++)
            if (LANG_IDS[i] == s.default_language) { cur = i; break; }
        cur = (cur + dir + lang_count) % lang_count;
        s.default_language = LANG_IDS[cur];
        break;
    }
    case 6: s.word_wrap = !s.word_wrap; break;
    }
    SaveSettings(s);
}

void ShowSettingsPanel(Settings& s)
{
    int my, mx;
    getmaxyx(stdscr, my, mx);

    const int PANEL_H = 14;
    const int PANEL_W = 50;
    int win_y = (my - PANEL_H) / 2;
    int win_x = (mx - PANEL_W) / 2;
    if (win_y < 0) win_y = 0;
    if (win_x < 0) win_x = 0;

    WINDOW* pw = newwin(PANEL_H, PANEL_W, win_y, win_x);
    if (!pw) return;
    keypad(pw, TRUE);

    int sel = 0;
    bool open = true;

    while (open) {
        werase(pw);
        box(pw, 0, 0);
        mvwprintw(pw, 0, 2, " VIX SETTINGS ");

        // Draw settings rows
        for (int i = 0; i < SETTING_COUNT; i++) {
            int row_y = 2 + i;
            std::string val = SettingValue(s, i);

            if (i == sel) {
                wattron(pw, A_REVERSE);
                mvwprintw(pw, row_y, 2, "%-24s %-16s", SETTING_LABELS[i], val.c_str());
                wattroff(pw, A_REVERSE);
            } else {
                mvwprintw(pw, row_y, 2, " %-23s %-16s", SETTING_LABELS[i], val.c_str());
            }
        }

        // Footer
        mvwprintw(pw, 10, 2, " %-46s", "");
        mvwprintw(pw, 11, 2, " %-46s", "");
        mvwprintw(pw, 10, 2, " \x18\x19 Navigate  \x1b\x1a/Enter Change  Esc Close");
        mvwprintw(pw, 12, 2, " Settings saved to ~/.config/vix/settings.json");

        wrefresh(pw);

        int ch = wgetch(pw);
        switch (ch) {
        case KEY_UP:
            sel = (sel - 1 + SETTING_COUNT) % SETTING_COUNT;
            break;
        case KEY_DOWN:
            sel = (sel + 1) % SETTING_COUNT;
            break;
        case KEY_LEFT:
            CycleSetting(s, sel, -1);
            break;
        case KEY_RIGHT:
        case '\n':
        case KEY_ENTER:
            CycleSetting(s, sel, 1);
            break;
        case 27: // Escape
            open = false;
            break;
        default:
            break;
        }
    }

    delwin(pw);
    touchwin(stdscr);
    refresh();
}
