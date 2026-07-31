#include "editor.hpp"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <sys/wait.h>
#include <clocale>
#include <unistd.h>
#include <termios.h>
#include <cctype>
#include <cstdio>
#include <regex>
#include <climits>
#include <fstream>
#include <signal.h>

#ifndef CTRL
#define CTRL(c) ((c) & 0x1f)
#endif

static bool is_prefix(const std::string& s, const std::string& of) {
    if (s.size() > of.size()) return false;
    for (size_t i = 0; i < s.size(); i++)
        if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)of[i])) return false;
    return true;
}

static bool iequal(char a, char b) {
    return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
}

static bool is_word_char(char c) {
    return std::isalnum((unsigned char)c) || c == '_';
}

static size_t ifind(const std::string& hay, const std::string& needle, size_t pos) {
    for (size_t i = pos; i + needle.length() <= hay.length(); i++) {
        bool match = true;
        for (size_t j = 0; j < needle.length(); j++) {
            if (!iequal(hay[i + j], needle[j])) { match = false; break; }
        }
        if (match) return i;
    }
    return std::string::npos;
}

Editor::Editor(int argc, char** argv)
    : current_tab(0), running(true),
      show_sidebar(true), focus_sidebar(false),
      in_search_mode(false), search_regex(false),
      sidebar_sel(0), sidebar_scroll(0), search_idx(-1),
      match_pos{0,0,false}
{
    current_dir = fs::current_path().string();
    last_save_time = std::chrono::steady_clock::now();
    LoadSettings(settings);
    ApplyTheme(themes[settings.theme]);
    SaveSettings(settings);

    if (argc > 1) {
        new_tab(argv[1]);
    } else {
        new_tab();
    }
    update_sidebar();
}

Editor::~Editor() {}

void Editor::new_tab(const std::string& fname) {
    auto tab = std::make_unique<Tab>();
    if (!fname.empty()) {
        tab->buffer.LoadFile(fname);
        tab->buffer.SetFilename(fname);
    } else {
        static const char* exts[] = {".txt", ".cpp", ".py", ".js", ".rs", ".go", ".c"};
        int lang = settings.default_language;
        tab->buffer.PushBack("");
        tab->buffer.SetFilename("Untitled" + std::string(exts[lang]));
    }
    detect_language(*tab);
    tabs.push_back(std::move(tab));
    current_tab = (int)tabs.size() - 1;
}

void Editor::close_tab(int idx) {
    if (tabs.size() <= 1) {
        if ((*tabs[current_tab]).buffer.IsModified()) {
            std::string a = prompt("Unsaved changes. Save? (y/n): ");
            if (a == "y" || a == "Y") save_file(*tabs[current_tab]);
        }
        running = false;
        return;
    }
    if (idx < 0 || idx >= (int)tabs.size()) return;
    if ((*tabs[idx]).buffer.IsModified()) {
        std::string a = prompt("Unsaved changes. Save? (y/n): ");
        if (a == "y" || a == "Y") save_file(*tabs[idx]);
    }
    tabs.erase(tabs.begin() + idx);
    if (current_tab >= (int)tabs.size()) current_tab = (int)tabs.size() - 1;
}

void Editor::switch_tab(int idx) {
    if (idx >= 0 && idx < (int)tabs.size()) {
        current_tab = idx;
        detect_language(*tabs[current_tab]);
    }
}

void Editor::detect_language(Tab& tab) {
    std::string fname = tab.buffer.GetFilename();
    size_t dot = fname.find_last_of(".");
    std::string ext = (dot != std::string::npos && dot < fname.length() - 1) ? fname.substr(dot + 1) : "";
    std::string ext_lower = ext;
    for (auto& c : ext_lower) c = std::tolower((unsigned char)c);

    if (ext_lower == "cpp" || ext_lower == "hpp" || ext_lower == "cc" || ext_lower == "hh" || ext_lower == "cxx" || ext_lower == "hxx")
        tab.rules = {1, "C++", {"int","void","return","include","iostream","std","cout","endl","using","namespace","class","public","private","if","else","for","while","new","delete","const"}};
    else if (ext_lower == "c" || ext_lower == "h")
        tab.rules = {6, "C", {"auto","break","case","char","const","continue","default","do","double","else","enum","extern","float","for","goto","if","int","long","register","return","short","signed","sizeof","static","struct","switch","typedef","union","unsigned","void","volatile","while","include","define","ifdef","endif","printf","malloc","free","NULL","size_t","FILE","main"}};
    else if (ext_lower == "py")
        tab.rules = {2, "Python", {"def","class","import","from","return","if","elif","else","for","while","print","True","False","None","self","async","await","with","as","try","except","raise","lambda"}};
    else if (ext_lower == "js" || ext_lower == "mjs" || ext_lower == "jsx")
        tab.rules = {3, "JavaScript", {"function","const","let","var","async","await","import","export","class","extends","if","else","for","while","return","try","catch","throw","new","this","true","false","null","undefined","break","continue","switch","case","default"}};
    else if (ext_lower == "rs")
        tab.rules = {4, "Rust", {"fn","let","mut","const","struct","impl","trait","pub","mod","use","crate","self","if","else","for","while","loop","match","return","async","await","true","false","Some","None","Ok","Err","break","continue","enum","dyn"}};
    else if (ext_lower == "go")
        tab.rules = {5, "Go", {"func","var","const","type","struct","interface","package","import","if","else","for","switch","case","return","go","defer","chan","select","true","false","nil","make","len","cap","append"}};
    else if (ext_lower == "html" || ext_lower == "htm")
        tab.rules = {7, "HTML", {"html","head","body","div","span","class","id","style","href","src","alt","title","meta","link","script","style","h1","h2","h3","p","a","img","ul","ol","li","table","tr","td","th","form","input","button","label","textarea","select","option","nav","header","footer","section","article","main","aside"}};
    else if (ext_lower == "css")
        tab.rules = {8, "CSS", {"margin","padding","border","background","color","font","display","position","width","height","top","left","right","bottom","flex","grid","align","justify","overflow","z-index","opacity","transform","transition","animation","@media","@import","@keyframes","none","auto","inherit","important"}};
    else if (ext_lower == "json")
        tab.rules = {9, "JSON", {"true","false","null"}};
    else if (ext_lower == "yaml" || ext_lower == "yml")
        tab.rules = {10, "YAML", {"true","false","yes","no","on","off","null"}};
    else
        tab.rules = {0, "Text", {}};
}

void Editor::load_file(Tab& tab, const std::string& fname) {
    tab.buffer.LoadFile(fname);
    tab.buffer.SetFilename(fname);
    detect_language(tab);
    tab.buffer.SetModified(false);
    tab.y = 0; tab.x = 0;
    status_msg = "Opened " + fname;
    msg_time = std::chrono::steady_clock::now();
}

void Editor::save_file(Tab& tab) {
    auto& buf = tab.buffer;
    if (buf.GetFilename().find("Untitled") == 0) {
        buf.SetFilename(prompt("Save As: "));
    }
    if (buf.GetFilename().empty()) return;
    if (buf.SaveFile()) {
        last_save_time = std::chrono::steady_clock::now();
        status_msg = "Saved!";
    } else {
        status_msg = "Error: save failed";
    }
    msg_time = std::chrono::steady_clock::now();
}

void Editor::update_sidebar() {
    sidebar_paths.clear();
    sidebar_paths.push_back("..");
    try {
        std::vector<fs::path> dirs, files;
        for (const auto& entry : fs::directory_iterator(current_dir)) {
            if (entry.path().filename().string()[0] == '.') continue;
            if (fs::is_directory(entry.path())) dirs.push_back(entry.path());
            else files.push_back(entry.path());
        }
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());
        for (auto& d : dirs) sidebar_paths.push_back(d);
        for (auto& f : files) sidebar_paths.push_back(f);
    } catch (...) {}
    if (sidebar_sel >= (int)sidebar_paths.size()) sidebar_sel = (int)sidebar_paths.size() - 1;
    if (sidebar_sel < 0) sidebar_sel = 0;
}

std::string Editor::prompt(const std::string& msg) {
    int my, mx;
    getmaxyx(stdscr, my, mx);
    attron(COLOR_PAIR(CP_STATUS));
    mvhline(my - 1, 0, ' ', mx);
    mvprintw(my - 1, 1, "%s", msg.c_str());
    echo();
    char buf[PROMPT_SIZE] = {0};
    if (getnstr(buf, PROMPT_SIZE - 1) == ERR) buf[0] = '\0';
    noecho();
    return std::string(buf);
}

void Editor::fuzzy_finder() {
    std::string q = prompt("Find File: ");
    if (q.empty()) return;

    std::vector<std::string> matches;
    std::vector<std::string> all_files;
    all_files.push_back("..");
    try {
        for (const auto& e : fs::recursive_directory_iterator(current_dir)) {
            if (fs::is_regular_file(e.path())) {
                std::string rel = fs::relative(e.path(), current_dir).string();
                if (rel.size() > 2 && rel.substr(0, 2) == "./") rel = rel.substr(2);
                all_files.push_back(rel);
            }
        }
    } catch (...) {}

    for (auto& f : all_files) {
        if (is_prefix(q, f)) matches.push_back(f);
    }

    if (matches.empty()) {
        status_msg = "No matches";
        msg_time = std::chrono::steady_clock::now();
        return;
    }

    std::sort(matches.begin(), matches.end());

    int my, mx;
    getmaxyx(stdscr, my, mx);
    int h = std::min((int)matches.size() + 2, my - 4);
    int w = std::min(60, mx - 4);
    int wy = (my - h) / 2;
    int wx = (mx - w) / 2;
    WINDOW* fw = newwin(h, w, wy, wx);
    if (!fw) return;
    keypad(fw, TRUE);

    int sel = 0;
    int scroll = 0;
    bool open = true;

    while (open) {
        werase(fw);
        box(fw, 0, 0);
        mvwprintw(fw, 0, 2, " Files (%zu) ", matches.size());

        int vis = h - 2;
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + vis) scroll = sel - vis + 1;

        for (int i = 0; i < vis && i + scroll < (int)matches.size(); i++) {
            int idx = i + scroll;
            if (idx == sel)
                wattron(fw, A_REVERSE);
            std::string name = matches[idx];
            if (name.size() > (size_t)(w - 4)) name = "..." + name.substr(name.size() - w + 7);
            mvwprintw(fw, i + 1, 2, " %-*s", w - 4, name.c_str());
            if (idx == sel) wattroff(fw, A_REVERSE);
        }

        wrefresh(fw);
        int ch = wgetch(fw);
        switch (ch) {
            case KEY_UP: sel = std::max(0, sel - 1); break;
            case KEY_DOWN: sel = std::min((int)matches.size() - 1, sel + 1); break;
            case '\n': {
                std::string path = matches[sel];
                if (path == "..") { open = false; break; }
                new_tab((fs::path(current_dir) / path).string());
                open = false;
                break;
            }
            case 27: open = false; break;
        }
    }
    delwin(fw);
    touchwin(stdscr);
    refresh();
}

std::regex Editor::build_regex(const std::string& q) const {
    std::string p = q;
    auto flags = std::regex::ECMAScript | std::regex::optimize;
    if (!search_case_sensitive) flags |= std::regex::icase;
    if (search_whole_word) p = "\\b(?:" + p + ")\\b";
    return std::regex(p, flags);
}

void Editor::plain_search(const std::string& line, int line_idx, const std::string& q) {
    size_t pos = 0;
    while (true) {
        size_t found = search_case_sensitive ? line.find(q, pos) : ifind(line, q, pos);
        if (found == std::string::npos) break;
        if (search_whole_word) {
            bool left = (found == 0) || !is_word_char(line[found - 1]);
            bool right = (found + q.length() >= line.length()) || !is_word_char(line[found + q.length()]);
            if (!(left && right)) { pos = found + 1; continue; }
        }
        search_results.push_back({line_idx, (int)found, (int)q.length()});
        pos = found + 1;
    }
}

void Editor::find_all(Tab& tab, const std::string& q) {
    search_results.clear();
    search_query = q;
    search_idx = -1;
    if (q.empty()) return;
    if (search_regex) {
        try {
            std::regex re = build_regex(q);
            for (int i = 0; i < tab.buffer.GetLineCount(); i++) {
                auto words_begin = std::sregex_iterator(tab.buffer[i].begin(), tab.buffer[i].end(), re);
                auto words_end = std::sregex_iterator();
                for (auto it = words_begin; it != words_end; ++it)
                    search_results.push_back({i, (int)it->position(), (int)it->length()});
            }
        } catch (const std::regex_error&) {
            for (int i = 0; i < tab.buffer.GetLineCount(); i++)
                plain_search(tab.buffer[i], i, q);
        }
    } else {
        for (int i = 0; i < tab.buffer.GetLineCount(); i++)
            plain_search(tab.buffer[i], i, q);
    }
    if (!search_results.empty()) {
        search_idx = 0;
        tab.y = search_results[0].line;
        tab.x = search_results[0].col;
    }
}

void Editor::search_next(Tab& tab) {
    if (search_results.empty()) return;
    search_idx = (search_idx + 1) % (int)search_results.size();
    tab.y = search_results[search_idx].line;
    tab.x = search_results[search_idx].col;
}

void Editor::search_prev(Tab& tab) {
    if (search_results.empty()) return;
    search_idx = (search_idx - 1 + (int)search_results.size()) % (int)search_results.size();
    tab.y = search_results[search_idx].line;
    tab.x = search_results[search_idx].col;
}

void Editor::clear_search(Tab&) {
    search_results.clear();
    search_query.clear();
    search_idx = -1;
}

void Editor::find_match(Tab& tab) {
    match_pos.active = false;
    if (tab.y >= tab.buffer.GetLineCount() || tab.x >= (int)tab.buffer[tab.y].length()) return;
    char c = tab.buffer[tab.y][tab.x];
    std::string open = "{([", close = "})]";
    int dir = 0, pair_idx = -1;
    if ((pair_idx = (int)open.find(c)) != (int)std::string::npos) dir = 1;
    else if ((pair_idx = (int)close.find(c)) != (int)std::string::npos) dir = -1;
    if (dir == 0) return;
    char target = (dir == 1) ? close[pair_idx] : open[pair_idx];
    int depth = 0, cy = tab.y, cx = tab.x;
    cx += dir;
    while (cy >= 0 && cy < tab.buffer.GetLineCount()) {
        while (cx >= 0 && cx < (int)tab.buffer[cy].length()) {
            if (tab.buffer[cy][cx] == c) depth++;
            else if (tab.buffer[cy][cx] == target) {
                if (depth == 0) { match_pos = {cx, cy, true}; return; }
                depth--;
            }
            cx += dir;
        }
        cy += dir;
        if (cy < 0 || cy >= tab.buffer.GetLineCount()) break;
        cx = (dir == 1) ? 0 : (int)tab.buffer[cy].length() - 1;
    }
}

void Editor::compile_run(Tab& tab) {
    save_file(tab);
    def_prog_mode();
    endwin();
    system("reset -e && clear");
    std::string cf = tab.buffer.GetFilename();
    std::string cmd;
    size_t dot = cf.find_last_of(".");
    std::string ext = (dot != std::string::npos) ? cf.substr(dot + 1) : "";
    if (ext == "cpp" || ext == "cc") cmd = "g++ \"" + cf + "\" -o run && ./run";
    else if (ext == "c") cmd = "gcc \"" + cf + "\" -o run && ./run";
    else if (ext == "py") cmd = "python3 \"" + cf + "\"";
    else if (ext == "rs") cmd = "rustc \"" + cf + "\" -o run && ./run";
    else if (ext == "go") cmd = "go run \"" + cf + "\"";
    else if (ext == "js" || ext == "mjs") cmd = "node \"" + cf + "\"";
    else if (ext == "sh") cmd = "bash \"" + cf + "\"";
    if (!cmd.empty()) {
        std::cout << "\033[1;33m>> RUN: " << cmd << "\033[0m\n";
        system(cmd.c_str());
    }
    std::cout << "\nPress Enter...";
    std::cin.ignore();
    std::cin.get();
    reset_prog_mode();
    refresh();
}

void Editor::draw_tab_bar(int mx) {
    int sw = show_sidebar ? SIDEBAR_WIDTH : 0;
    attron(COLOR_PAIR(CP_STATUS));
    mvhline(0, sw, ' ', mx - sw);

    int xpos = sw + 1;
    for (size_t i = 0; i < tabs.size(); i++) {
        auto& t = *tabs[i];
        std::string label = t.buffer.GetFilename();
        size_t slash = label.find_last_of("/\\");
        if (slash != std::string::npos) label = label.substr(slash + 1);
        if (t.buffer.IsModified()) label += " *";
        std::string tab_str = " " + label + " ";
        if (xpos + (int)tab_str.size() > mx) { tab_str = " ..."; break; }

        if ((int)i == current_tab) {
            attron(A_REVERSE);
            mvprintw(0, xpos, "%s", tab_str.c_str());
            attroff(A_REVERSE);
        } else {
            mvprintw(0, xpos, "%s", tab_str.c_str());
        }
        xpos += (int)tab_str.size();
    }
    attroff(COLOR_PAIR(CP_STATUS));

    auto now = std::chrono::steady_clock::now();
    auto& tab = *tabs[current_tab];
    if (settings.auto_save_interval > 0 && tab.buffer.IsModified() &&
        std::chrono::duration_cast<std::chrono::seconds>(now - last_save_time).count() >= settings.auto_save_interval)
        save_file(tab);
}

void Editor::draw_sidebar(int my, int mx) {
    (void)mx;
    attron(COLOR_PAIR(CP_SIDEBAR));
    for (int i = 1; i < my - 1; i++) mvaddch(i, SIDEBAR_WIDTH - 1, '|');
    mvprintw(1, 1, focus_sidebar ? " * PROJECT" : "  PROJECT");
    int vis = my - 4;
    if (sidebar_sel < sidebar_scroll) sidebar_scroll = sidebar_sel;
    if (sidebar_sel >= sidebar_scroll + vis) sidebar_scroll = sidebar_sel - vis + 1;
    if (sidebar_scroll < 0) sidebar_scroll = 0;
    for (int i = 0; i < vis; i++) {
        int idx = i + sidebar_scroll;
        if (idx >= (int)sidebar_paths.size()) break;
        bool sel = (focus_sidebar && idx == sidebar_sel);
        if (sel) attron(COLOR_PAIR(CP_SELECT));

        std::string n = sidebar_paths[idx].filename().string();
        if (n.empty()) n = "..";
        std::string ex = sidebar_paths[idx].extension().string();
        for (auto& ec : ex) ec = std::tolower((unsigned char)ec);
        int c = CP_DEFAULT;
        if (ex == ".py") c = CP_CYAN;
        else if (ex == ".cpp" || ex == ".cc" || ex == ".cxx") c = CP_KEYWORD;
        else if (ex == ".hpp" || ex == ".h" || ex == ".hh") c = CP_KEYWORD;
        else if (ex == ".c") c = CP_ORANGE;
        else if (ex == ".js" || ex == ".jsx" || ex == ".mjs") c = CP_STRING;
        else if (ex == ".rs") c = CP_ORANGE;
        else if (ex == ".go") c = CP_CYAN;
        else if (ex == ".json" || ex == ".yaml" || ex == ".yml") c = CP_STRING;
        else if (ex == ".html" || ex == ".css") c = CP_ORANGE;
        else if (ex == ".sh" || ex == ".bash") c = CP_COMMENT;
        else if (ex == ".md" || ex == ".txt") c = CP_LINENUM;

        std::string icon;
        if (n == "..") { icon = "\xE2\x86\xA2"; c = CP_KEYWORD; }
        else if (fs::is_directory(sidebar_paths[idx])) icon = ">";
        else if (ex == ".cpp" || ex == ".hpp") icon = "C";
        else if (ex == ".c") icon = "c";
        else if (ex == ".py") icon = "P";
        else if (ex == ".js" || ex == ".jsx" || ex == ".mjs") icon = "J";
        else if (ex == ".rs") icon = "R";
        else if (ex == ".go") icon = "G";
        else if (ex == ".html") icon = "H";
        else if (ex == ".css") icon = "#";
        else if (ex == ".json") icon = "{";
        else if (ex == ".sh" || ex == ".bash") icon = ">";
        else if (ex == ".md") icon = "m";
        else if (ex == ".toml") icon = "T";
        else if (ex == ".yaml" || ex == ".yml") icon = "Y";
        else icon = ".";

        if (fs::is_directory(sidebar_paths[idx]) || n == "..") {
            attron(A_BOLD | COLOR_PAIR(CP_KEYWORD));
        } else {
            attron(COLOR_PAIR(c));
        }

        std::string label = icon + " " + n;
        if (label.length() > 18) label = label.substr(0, 16) + "..";
        mvprintw(i + 2, 1, " %-18s", label.c_str());

        if (fs::is_directory(sidebar_paths[idx]) || n == "..") {
            attroff(A_BOLD | COLOR_PAIR(CP_KEYWORD));
        } else {
            attroff(COLOR_PAIR(c));
        }
        if (sel) attroff(COLOR_PAIR(CP_SELECT));
    }
    attroff(COLOR_PAIR(CP_SIDEBAR));
}

void Editor::draw_line(int row, int buf_idx, int max_x, int sw, Tab& tab) {
    auto& line = tab.buffer[buf_idx];
    if (settings.line_numbers) {
        attron(COLOR_PAIR(CP_LINENUM));
        mvprintw(row, sw, "%3d ", buf_idx + 1);
        attroff(COLOR_PAIR(CP_LINENUM));
    }
    int cur_x = sw + (settings.line_numbers ? 4 : 0);
    int i = tab.h_scroll;
    while (i < (int)line.length() && cur_x < max_x) {
        bool is_search_start = false;
        int search_hit_len = 0;
        int search_hit_idx = -1;
        if (!search_query.empty()) {
            for (int si = 0; si < (int)search_results.size(); si++) {
                if (search_results[si].line == buf_idx && search_results[si].col == i &&
                    search_results[si].col + search_results[si].len <= (int)line.length()) {
                    is_search_start = true;
                    search_hit_len = search_results[si].len;
                    search_hit_idx = si;
                    break;
                }
            }
        }
        if (is_search_start) {
            int cp = (search_hit_idx == search_idx) ? CP_MATCH : CP_SEARCH;
            attron(COLOR_PAIR(cp));
            for (int j = 0; j < search_hit_len && cur_x < max_x; j++) {
                addch(line[i + j]); cur_x++;
            }
            i += search_hit_len - 1;
            attroff(COLOR_PAIR(cp));
            i++; continue;
        }

        bool is_match = (match_pos.active && match_pos.y == buf_idx && match_pos.x == i);
        if (is_match) attron(COLOR_PAIR(CP_MATCH));

        char c = line[i];
        bool is_c_cpp = (tab.rules.lang == 1 || tab.rules.lang == 6);

        if (c == '"' || c == '\'') {
            attron(COLOR_PAIR(CP_STRING));
            addch(c); cur_x++; i++;
            while (i < (int)line.length() && cur_x < max_x) {
                addch(line[i]); cur_x++;
                if (line[i] == c && (i == 0 || line[i-1] != '\\')) break;
                i++;
            }
            i++;
            attroff(COLOR_PAIR(CP_STRING));
        } else if (is_c_cpp && tab.in_block_comment) {
            attron(COLOR_PAIR(CP_COMMENT));
            while (i < (int)line.length() && cur_x < max_x) {
                if (line[i] == '*' && i+1 < (int)line.length() && line[i+1] == '/') {
                    addch(line[i]); cur_x++; i++;
                    addch(line[i]); cur_x++; i++;
                    tab.in_block_comment = false;
                    break;
                }
                addch(line[i]); cur_x++; i++;
            }
            attroff(COLOR_PAIR(CP_COMMENT));
        } else if (is_c_cpp && c == '/' && i+1 < (int)line.length() && line[i+1] == '/') {
            attron(COLOR_PAIR(CP_COMMENT));
            while (i < (int)line.length() && cur_x < max_x) { addch(line[i]); cur_x++; i++; }
            attroff(COLOR_PAIR(CP_COMMENT));
        } else if (is_c_cpp && c == '/' && i+1 < (int)line.length() && line[i+1] == '*') {
            attron(COLOR_PAIR(CP_COMMENT));
            addch(line[i]); cur_x++; i++;
            addch(line[i]); cur_x++; i++;
            bool closed = false;
            while (i < (int)line.length() && cur_x < max_x) {
                addch(line[i]); cur_x++;
                if (line[i] == '*' && i+1 < (int)line.length() && line[i+1] == '/') {
                    i++;
                    addch(line[i]); cur_x++;
                    closed = true;
                    break;
                }
                i++;
            }
            if (!closed) tab.in_block_comment = true;
            attroff(COLOR_PAIR(CP_COMMENT));
        } else if (is_c_cpp && c == '#' && (i == 0 || line[i-1] == ' ' || line[i-1] == '\t')) {
            attron(COLOR_PAIR(CP_KEYWORD) | A_BOLD);
            while (i < (int)line.length() && cur_x < max_x) { addch(line[i]); cur_x++; i++; }
            attroff(COLOR_PAIR(CP_KEYWORD) | A_BOLD);
        } else if (std::isdigit((unsigned char)c)) {
            attron(COLOR_PAIR(CP_ORANGE));
            addch(c); cur_x++; i++;
            attroff(COLOR_PAIR(CP_ORANGE));
        } else if (std::isalpha((unsigned char)c) || c == '_') {
            std::string w;
            while (i < (int)line.length() && (std::isalnum((unsigned char)line[i]) || line[i] == '_')) w += line[i++];
            bool is_kw = false;
            for (auto& k : tab.rules.keywords) if (k == w) { is_kw = true; break; }
            if (is_kw) attron(COLOR_PAIR(CP_KEYWORD) | A_BOLD);
            else if (i < (int)line.length() && line[i] == '(') attron(COLOR_PAIR(CP_CYAN));
            for (char cc : w) { if (cur_x < max_x) { addch(cc); cur_x++; } }
            attroff(COLOR_PAIR(CP_KEYWORD) | A_BOLD | COLOR_PAIR(CP_CYAN));
        } else {
            addch(c); cur_x++; i++;
        }
        if (is_match) attroff(COLOR_PAIR(CP_MATCH));
    }
}

void Editor::draw_status(int my, int mx) {
    auto now = std::chrono::steady_clock::now();
    auto& tab = *tabs[current_tab];
    attron(COLOR_PAIR(CP_STATUS));
    mvhline(my - 1, 0, ' ', mx);
    if (in_search_mode) {
        std::string flags;
        if (search_regex) flags += "RE ";
        if (!search_case_sensitive) flags += "IC ";
        if (search_whole_word) flags += "WW ";
        mvprintw(my - 1, 1, " %sFind: %s", flags.c_str(), search_query.c_str());
        if (!search_results.empty())
            mvprintw(my - 1, mx - 12, " %d/%d ", search_idx + 1, (int)search_results.size());
        else if (!search_query.empty())
            mvprintw(my - 1, mx - 14, " no matches ");
    } else {
        if (std::chrono::duration_cast<std::chrono::seconds>(now - msg_time).count() < STATUS_TIMEOUT) {
            mvprintw(my - 1, 1, "%s", status_msg.c_str());
        } else {
            std::string suf;
            if (!search_results.empty())
                suf = " | [" + std::to_string(search_idx + 1) + "/" + std::to_string((int)search_results.size()) + "]";
            mvprintw(my - 1, 1, " %s%s | %s | Tab %d/%zu | L:%d/%d C:%d%s | ^H Help",
                tab.buffer.GetFilename().c_str(),
                tab.buffer.IsModified() ? " *" : "",
                tab.rules.name.c_str(),
                current_tab + 1, tabs.size(),
                tab.y + 1, tab.buffer.GetLineCount(), tab.x + 1, suf.c_str());
        }
    }
    attroff(COLOR_PAIR(CP_STATUS));
}

void Editor::draw() {
    erase();
    int my, mx;
    getmaxyx(stdscr, my, mx);
    int sw = show_sidebar ? SIDEBAR_WIDTH : 0;
    auto& tab = *tabs[current_tab];

    draw_tab_bar(mx);

    if (show_sidebar) draw_sidebar(my, mx);

    tab.in_block_comment = false;
    for (int i = 1; i < my - 1; i++) {
        int idx = (i - 1) + tab.v_scroll;
        if (idx < tab.buffer.GetLineCount()) draw_line(i, idx, mx, sw, tab);
    }

    draw_status(my, mx);

    if (focus_sidebar) {
        int sy = std::clamp(sidebar_sel - sidebar_scroll + 2, 1, my - 2);
        move(sy, 1);
    } else if (in_search_mode) {
        int flag_len = 0;
        if (search_regex) flag_len += 3;
        if (!search_case_sensitive) flag_len += 3;
        if (search_whole_word) flag_len += 3;
        int sx = std::clamp(7 + flag_len + (int)search_query.length(), 0, mx - 1);
        move(my - 1, sx);
    } else {
        int cy = std::clamp(tab.y - tab.v_scroll + 1, 1, my - 2);
        int cx = std::clamp(tab.x - tab.h_scroll + sw + (settings.line_numbers ? 4 : 0), 0, mx - 1);
        move(cy, cx);
    }
    refresh();
}

void Editor::run() {
    struct termios ot, nt;
    tcgetattr(0, &ot);
    nt = ot;
    nt.c_iflag &= ~(IXON | IXOFF);
    nt.c_cc[VQUIT] = _POSIX_VDISABLE;
    tcsetattr(0, TCSANOW, &nt);
    setlocale(LC_ALL, "");
    initscr();
    noecho();
    raw();
    keypad(stdscr, 1);
    set_escdelay(30);
    start_color();
    use_default_colors();
    ApplyTheme(themes[settings.theme]);
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);

    while (running) {
        int my, mx;
        getmaxyx(stdscr, my, mx);
        auto& tab = *tabs[current_tab];

        tab.y = std::clamp(tab.y, 0, std::max(0, tab.buffer.GetLineCount() - 1));
        tab.x = std::min(tab.x, std::max(0, (int)tab.buffer[tab.y].length()));

        if (tab.y < tab.v_scroll) tab.v_scroll = tab.y;
        if (tab.y >= tab.v_scroll + my - 2) tab.v_scroll = tab.y - (my - 2) + 1;

        int sw = show_sidebar ? SIDEBAR_WIDTH : 0;
        int text_w = mx - sw - (settings.line_numbers ? 4 : 0);
        if (text_w < 1) text_w = 1;
        if (tab.h_scroll < 0) tab.h_scroll = 0;
        if (tab.x < tab.h_scroll) tab.h_scroll = tab.x;
        if (tab.x >= tab.h_scroll + text_w) tab.h_scroll = tab.x - text_w + 1;

        draw();
        int ch = getch();

        if (ch == KEY_MOUSE) {
            MEVENT event;
            if (getmouse(&event) == OK) {
                if (event.bstate & BUTTON4_PRESSED) {
                    if (show_sidebar && event.x < SIDEBAR_WIDTH) {
                        if (sidebar_sel > 0) sidebar_sel--;
                    } else {
                        if (tab.v_scroll > 0) tab.v_scroll--;
                    }
                } else if (event.bstate & BUTTON5_PRESSED) {
                    if (show_sidebar && event.x < SIDEBAR_WIDTH) {
                        if (sidebar_sel < (int)sidebar_paths.size() - 1) sidebar_sel++;
                    } else {
                        if (tab.v_scroll < tab.buffer.GetLineCount() - 1) tab.v_scroll++;
                    }
                } else if (event.bstate & BUTTON1_PRESSED) {
                    if (show_sidebar && event.x < SIDEBAR_WIDTH) {
                        focus_sidebar = true;
                        int clicked_idx = event.y - 2;
                        if (clicked_idx >= 0 && clicked_idx < (int)sidebar_paths.size())
                            sidebar_sel = clicked_idx;
                    } else if (event.y == 0 && show_sidebar && event.x >= SIDEBAR_WIDTH) {
                        int xpos = SIDEBAR_WIDTH + 1;
                        for (size_t i = 0; i < tabs.size(); i++) {
                            auto& t = *tabs[i];
                            std::string label = t.buffer.GetFilename();
                            size_t slash = label.find_last_of("/\\");
                            if (slash != std::string::npos) label = label.substr(slash + 1);
                            if (t.buffer.IsModified()) label += " *";
                            std::string tab_str = " " + label + " ";
                            if (event.x >= xpos && event.x < xpos + (int)tab_str.size()) {
                                switch_tab((int)i);
                                break;
                            }
                            xpos += (int)tab_str.size();
                        }
                    } else {
                        focus_sidebar = false;
                        int clicked_y = event.y - 1 + tab.v_scroll;
                        int sw = show_sidebar ? SIDEBAR_WIDTH : 0;
                        int clicked_x = event.x - sw - (settings.line_numbers ? 4 : 0) + tab.h_scroll;
                        if (clicked_y >= 0 && clicked_y < tab.buffer.GetLineCount()) {
                            tab.y = clicked_y;
                            tab.x = std::max(0, std::min((int)tab.buffer[tab.y].length(), clicked_x));
                        }
                    }
                }
            }
            continue;
        }

        if (in_search_mode) {
            if (ch == 27) {
                in_search_mode = false;
                clear_search(tab);
            } else if (ch == '\n' || ch == KEY_ENTER) {
                in_search_mode = false;
            } else if (ch == KEY_F(3)) {
                if (!search_results.empty()) search_next(tab);
            } else if (ch == KEY_BACKSPACE) {
                if (!search_query.empty()) {
                    search_query.pop_back();
                    find_all(tab, search_query);
                } else {
                    in_search_mode = false;
                    clear_search(tab);
                }
            } else if (ch == CTRL('r')) {
                search_regex = !search_regex;
                if (!search_query.empty()) find_all(tab, search_query);
            } else if (ch == CTRL('c')) {
                search_case_sensitive = !search_case_sensitive;
                if (!search_query.empty()) find_all(tab, search_query);
            } else if (ch == CTRL('w')) {
                search_whole_word = !search_whole_word;
                if (!search_query.empty()) find_all(tab, search_query);
            } else if (ch >= 32 && ch <= 126) {
                search_query += (char)ch;
                find_all(tab, search_query);
            }
            continue;
        }

        if (ch == 27) {
            continue;
        }

        if (ch == CTRL('q')) {
            if (tab.buffer.IsModified()) {
                std::string a = prompt("Unsaved changes. Save? (y/n): ");
                if (a == "y" || a == "Y") save_file(tab);
            }
            running = false;
        }
        else if (ch == CTRL('s')) save_file(tab);
        else if (ch == CTRL('r')) compile_run(tab);
        else if (ch == CTRL('w')) focus_sidebar = !focus_sidebar;
        else if (ch == CTRL('t')) show_sidebar = !show_sidebar;
        else if (ch == KEY_F(2)) {
            def_prog_mode();
            endwin();
            ShowSettingsPanel(settings);
            reset_prog_mode();
            refresh();
        }
        else if (ch == CTRL('n')) new_tab();
        else if (ch == 9) {
            auto cmd = std::make_unique<InsertCommand>(&tab.buffer, std::string(settings.tab_width, ' '), tab.y, tab.x);
            tab.history.execute(std::move(cmd));
            tab.x += settings.tab_width;
        }
        else if (ch == CTRL('k')) {
            if (tab.y < tab.buffer.GetLineCount()) {
                tab.clipboard = tab.buffer[tab.y];
                auto cmd = std::make_unique<DeleteCommand>(&tab.buffer, tab.clipboard, tab.y, 0);
                tab.history.execute(std::move(cmd));
                tab.x = 0;
            }
        }
        else if (ch == CTRL('c')) {
            if (tab.y < tab.buffer.GetLineCount()) {
                tab.clipboard = tab.buffer[tab.y];
                status_msg = "Copied";
                msg_time = std::chrono::steady_clock::now();
            }
        }
        else if (ch == CTRL('v')) {
            if (!tab.clipboard.empty()) {
                auto cmd = std::make_unique<InsertCommand>(&tab.buffer, tab.clipboard, tab.y, tab.x);
                tab.history.execute(std::move(cmd));
                tab.x += (int)tab.clipboard.length();
            }
        }
        else if (ch == CTRL('f')) {
            in_search_mode = true;
            search_query.clear();
            search_results.clear();
            search_idx = -1;
        }
        else if (ch == KEY_F(3)) {
            if (!search_results.empty()) search_next(tab);
        }
        else if (ch == KEY_F(13)) {
            if (!search_results.empty()) search_prev(tab);
        }
        else if (ch == CTRL('d')) {
            std::string q = prompt("Find: ");
            if (q.empty()) continue;
            std::string r = prompt("Replace with: ");
            int count = 0;
            for (int i = 0; i < tab.buffer.GetLineCount(); i++) {
                size_t pos = 0;
                while ((pos = tab.buffer[i].find(q, pos)) != std::string::npos) {
                    tab.buffer[i] = tab.buffer[i].substr(0, pos) + r + tab.buffer[i].substr(pos + q.length());
                    pos += r.length();
                    count++;
                }
            }
            if (count > 0) {
                tab.buffer.SetModified(true);
                status_msg = "Replaced " + std::to_string(count) + " occurrence" + (count != 1 ? "s" : "");
            } else status_msg = "Not found: " + q;
            msg_time = std::chrono::steady_clock::now();
        }
        else if (ch == CTRL('g')) {
            std::string input = prompt("Go To Line: ");
            if (!input.empty()) {
                try {
                    int line = std::stoi(input);
                    if (line < 1 || line > tab.buffer.GetLineCount())
                        status_msg = "Line out of range (1-" + std::to_string(tab.buffer.GetLineCount()) + ")";
                    else { tab.y = line - 1; tab.x = 0; status_msg = "Jumped to L:" + std::to_string(line); }
                } catch (...) { status_msg = "Invalid line number"; }
                msg_time = std::chrono::steady_clock::now();
            }
        }
        else if (ch == CTRL('p')) {
            fuzzy_finder();
        }
        else if (ch == CTRL('h')) {
            int myw, mxw;
            getmaxyx(stdscr, myw, mxw);
            WINDOW* hw = newwin(12, 50, (myw - 12) / 2, (mxw - 50) / 2);
            if (hw) {
                box(hw, 0, 0);
                mvwprintw(hw, 0, 2, " VIX HELP ");
                mvwprintw(hw, 2, 2, "^S: Save   ^Q: Quit   ^R: Run   F2: Settings");
                mvwprintw(hw, 3, 2, "^K: Kill   ^C: Copy   ^V: Paste   ^P: Find");
                mvwprintw(hw, 4, 2, "^F: Search F3: Next  S+F3: Prev");
                mvwprintw(hw, 5, 2, "Search: ^R Regex  ^C Case  ^W Word");
                mvwprintw(hw, 6, 2, "^D: Replace  ^G: Go To Line");
                mvwprintw(hw, 7, 2, "^Z: Undo  ^Y: Redo  ^N: New Tab");
                mvwprintw(hw, 8, 2, "Tab: F5 Next  S+Tab Prev  ^\\ Close");
                mvwprintw(hw, 9, 2, "Sidebar: 'a' New  'd' Delete  ^W Focus");
                wrefresh(hw);
                wgetch(hw);
                delwin(hw);
            }
        }
        else if (ch == CTRL('z')) {
            if (tab.history.undo()) {
                status_msg = "[Undo]";
                tab.y = std::clamp(tab.y, 0, std::max(0, tab.buffer.GetLineCount() - 1));
                tab.x = std::min(tab.x, std::max(0, (int)tab.buffer[tab.y].length()));
            } else status_msg = "[Nothing to undo]";
            msg_time = std::chrono::steady_clock::now();
        }
        else if (ch == CTRL('y')) {
            if (tab.history.redo()) {
                status_msg = "[Redo]";
                tab.y = std::clamp(tab.y, 0, std::max(0, tab.buffer.GetLineCount() - 1));
                tab.x = std::min(tab.x, std::max(0, (int)tab.buffer[tab.y].length()));
            } else status_msg = "[Nothing to redo]";
            msg_time = std::chrono::steady_clock::now();
        }
        else if (ch == KEY_BTAB) {
            int prev = (current_tab - 1 + (int)tabs.size()) % (int)tabs.size();
            switch_tab(prev);
        }
        else if (ch == KEY_F(5)) {
            int next = (current_tab + 1) % (int)tabs.size();
            switch_tab(next);
        }
        else if (ch == 28) {
            close_tab(current_tab);
            if (!running) break;
            continue;
        }
        else if (focus_sidebar) {
            if (ch == KEY_UP && sidebar_sel > 0) sidebar_sel--;
            else if (ch == KEY_DOWN && sidebar_sel < (int)sidebar_paths.size() - 1) sidebar_sel++;
            else if (ch == 'a') {
                std::string n = prompt("New File: ");
                if (!n.empty()) {
                    std::ofstream f(n);
                    f.close();
                    update_sidebar();
                }
            } else if (ch == 'd') {
                if (sidebar_sel > 0) {
                    fs::remove_all(sidebar_paths[sidebar_sel]);
                    update_sidebar();
                }
            } else if (ch == '\n') {
                if (fs::is_directory(sidebar_paths[sidebar_sel])) {
                    current_dir = fs::canonical(sidebar_paths[sidebar_sel]).string();
                    fs::current_path(current_dir);
                    sidebar_sel = 0;
                    update_sidebar();
                } else {
                    load_file(tab, sidebar_paths[sidebar_sel].string());
                    focus_sidebar = false;
                }
            }
        } else {
            if (ch == KEY_UP && tab.y > 0) tab.y--;
            else if (ch == KEY_DOWN && tab.y < tab.buffer.GetLineCount() - 1) tab.y++;
            else if (ch == KEY_LEFT && tab.x > 0) tab.x--;
            else if (ch == KEY_RIGHT && tab.y < tab.buffer.GetLineCount() && tab.x < (int)tab.buffer[tab.y].length()) tab.x++;
            else if (ch == KEY_BACKSPACE || ch == 127) {
                if (tab.x > 0 && tab.x <= (int)tab.buffer[tab.y].length()) {
                    char deleted = tab.buffer[tab.y][tab.x - 1];
                    auto cmd = std::make_unique<DeleteCommand>(&tab.buffer, std::string(1, deleted), tab.y, tab.x - 1);
                    tab.history.execute(std::move(cmd));
                    tab.x--;
                    char next = (tab.x < (int)tab.buffer[tab.y].length()) ? tab.buffer[tab.y][tab.x] : '\0';
                    if ((deleted == '(' && next == ')') || (deleted == '{' && next == '}') ||
                        (deleted == '[' && next == ']') || (deleted == '"' && next == '"')) {
                        auto cmd2 = std::make_unique<DeleteCommand>(&tab.buffer, std::string(1, next), tab.y, tab.x);
                        tab.history.execute(std::move(cmd2));
                    }
                } else if (tab.y > 0) {
                    tab.x = (int)tab.buffer[tab.y - 1].length();
                    tab.buffer[tab.y - 1] += tab.buffer[tab.y];
                    tab.buffer.EraseLine(tab.y);
                    tab.y--;
                    tab.buffer.SetModified(true);
                }
            } else if (ch == '\n') {
                if (tab.y < tab.buffer.GetLineCount()) {
                    std::string rest = (tab.x < (int)tab.buffer[tab.y].length()) ? tab.buffer[tab.y].substr(tab.x) : "";
                    std::string indent;
                    for (int i = 0; i < tab.x && i < (int)tab.buffer[tab.y].length(); i++) {
                        if (tab.buffer[tab.y][i] == ' ' || tab.buffer[tab.y][i] == '\t') indent += tab.buffer[tab.y][i];
                        else break;
                    }
                    std::string trimmed = tab.buffer[tab.y].substr(0, tab.x);
                    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
                        trimmed.pop_back();
                    if (!trimmed.empty()) {
                        char last = trimmed.back();
                        if (last == '{' || last == '(' || last == '[' || last == ':')
                            indent += std::string(settings.tab_width, ' ');
                    }
                    std::string orig = tab.buffer[tab.y];
                    tab.buffer[tab.y] = (tab.x < (int)tab.buffer[tab.y].length()) ?
                        tab.buffer[tab.y].substr(0, tab.x) : tab.buffer[tab.y];
                    auto cmd = std::make_unique<NewLineCommand>(&tab.buffer, tab.y + 1, indent + rest, orig);
                    tab.history.execute(std::move(cmd));
                    tab.y++;
                    tab.x = (int)indent.length();
                }
            } else if (ch >= 32 && ch <= 126) {
                if (tab.y < tab.buffer.GetLineCount() && tab.x <= (int)tab.buffer[tab.y].length()) {
                    if (ch == '(') {
                        auto cmd = std::make_unique<InsertCommand>(&tab.buffer, "()", tab.y, tab.x);
                        tab.history.execute(std::move(cmd));
                        tab.x++;
                    } else if (ch == '{') {
                        auto cmd = std::make_unique<InsertCommand>(&tab.buffer, "{}", tab.y, tab.x);
                        tab.history.execute(std::move(cmd));
                        tab.x++;
                    } else if (ch == '[') {
                        auto cmd = std::make_unique<InsertCommand>(&tab.buffer, "[]", tab.y, tab.x);
                        tab.history.execute(std::move(cmd));
                        tab.x++;
                    } else if (ch == '"') {
                        auto cmd = std::make_unique<InsertCommand>(&tab.buffer, "\"\"", tab.y, tab.x);
                        tab.history.execute(std::move(cmd));
                        tab.x++;
                    } else {
                        auto cmd = std::make_unique<InsertCommand>(&tab.buffer, std::string(1, (char)ch), tab.y, tab.x);
                        tab.history.execute(std::move(cmd));
                        tab.x++;
                    }
                }
            }
        }

        if (!focus_sidebar && !in_search_mode) {
            find_match(tab);
        }
    }

    noraw();
    echo();
    endwin();
    tcsetattr(0, TCSANOW, &ot);
    system("stty sane && clear");
}
