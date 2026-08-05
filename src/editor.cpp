/*
 * editor.cpp - UI rendering, input dispatch, and file/project handling
 *
 * draw() paints tab bar, sidebar and the visible text each frame; run()
 * reads keys and applies edits through the History stack. Rendering only
 * touches the in-viewport portion of the buffer, and everything screen
 * state is restored by the catch handler if an exception escapes run().
 */
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
#include <cstring>
#include <cerrno>
#include <regex>
#include <climits>
#include <fstream>
#include <signal.h>

#ifndef CTRL
#define CTRL(c) ((c) & 0x1f)
#endif

// Score how well query matches text as a subsequence, or -1 when it cannot.
// Word starts (after / . _ -, or a lowercase->uppercase boundary) and
// consecutive matched runs score higher; the path length is subtracted so a
// tight, short match outranks a scattered one in a longer path.
static int fuzzy_score(const std::string& q, const std::string& s) {
    size_t qi = 0;
    int score = 0;
    for (size_t i = 0; i < s.size() && qi < q.size(); i++) {
        char lq = std::tolower((unsigned char)q[qi]);
        if (std::tolower((unsigned char)s[i]) != lq) continue;
        bool word_start = (i == 0) || s[i-1] == '/' || s[i-1] == '.' || s[i-1] == '_' || s[i-1] == '-' ||
                          (std::isupper((unsigned char)s[i]) && std::islower((unsigned char)s[i-1]));
        if (word_start) score += 10;
        else if (i > 0 && std::tolower((unsigned char)s[i-1]) == lq) score += 5;
        else score += 1;
        qi++;
    }
    if (qi < q.size()) return -1;
    return score - (int)s.size();
}

static bool iequal(char a, char b) {
    return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
}

// System clipboard bridge. Writes/pastes via wl-copy/wl-paste on Wayland,
// xclip/xsel-style tools on X11; anything not on PATH is silently ignored so
// the editor keeps working with only its internal clipboard. Detection runs
// once (popen, not system(), so the raw terminal is never handed to a shell).
static bool clip_tool_available = false;
static bool clip_tool_checked = false;

static bool have_clipboard_tool() {
    if (clip_tool_checked) return clip_tool_available;
    clip_tool_checked = true;
    const char* cmd = getenv("WAYLAND_DISPLAY")
        ? "command -v wl-copy && command -v wl-paste"
        : "command -v xclip || command -v xsel";
    FILE* p = popen(cmd, "r");
    if (!p) return false;
    char buf[32];
    bool ok = fread(buf, 1, sizeof buf, p) > 0;
    pclose(p);
    clip_tool_available = ok;
    return ok;
}

static void system_copy(const std::string& text) {
    if (!have_clipboard_tool() || text.empty()) return;
    const char* cmd = getenv("WAYLAND_DISPLAY") ? "wl-copy" : "xclip -selection clipboard";
    FILE* p = popen(cmd, "w");
    if (!p) return;
    fwrite(text.data(), 1, text.size(), p);
    pclose(p);
}

static std::string system_paste() {
    if (!have_clipboard_tool()) return "";
    const char* cmd = getenv("WAYLAND_DISPLAY") ? "wl-paste" : "xclip -o -selection clipboard";
    FILE* p = popen(cmd, "r");
    if (!p) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

static bool is_word_char(char c) {
    return std::isalnum((unsigned char)c) || c == '_';
}

// Word-aware wrap: given a line and a wrap width, return the column where each
// visual segment starts (first entry is always 0). Each segment is at most
// `text_w` columns, preferring to break after a space so words stay whole.
static std::vector<int> wrap_starts(const std::string& line, int text_w) {
    std::vector<int> starts;
    int len = (int)line.length();
    if (text_w < 1) text_w = 1;
    starts.push_back(0);
    int pos = 0;
    while (len - pos > text_w) {
        int end = pos + text_w;
        size_t sp_raw = line.rfind(' ', end - 1);
        if (sp_raw != std::string::npos) {
            int sp = (int)sp_raw;
            if (sp >= pos) end = sp + 1;
        }
        if (end <= pos) end = pos + 1;
        starts.push_back(end);
        pos = end;
    }
    return starts;
}

/* Filename extension -> lowercased string (no dot). */
static std::string file_ext(const std::string& fname) {
    size_t dot = fname.find_last_of(".");
    if (dot == std::string::npos || dot == fname.length() - 1) return "";
    std::string ext = fname.substr(dot + 1);
    for (auto& c : ext) c = std::tolower((unsigned char)c);
    return ext;
}

/* Exception-free test: is path a directory? */
static bool is_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

/* Run {argv} with working directory {wd}, no shell, and wait for exit.
 * Returns the child's exit code, or -1 on failure to spawn. */
static int run_process(const std::vector<std::string>& args, const std::string& wd) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    std::string dir = wd.empty() ? std::string(".") : wd;

    pid_t pid = fork();
    if (pid < 0) { std::fprintf(stderr, "vix: fork failed: %s\n", strerror(errno)); return -1; }
    if (pid == 0) {
        if (chdir(dir.c_str()) != 0) {
            std::fprintf(stderr, "vix: cannot enter %s: %s\n", dir.c_str(), strerror(errno));
            _exit(127);
        }
        execvp(argv[0], argv.data());
        std::fprintf(stderr, "vix: cannot run %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        std::fprintf(stderr, "vix: wait for %s failed: %s\n", argv[0], strerror(errno));
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
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

void Editor::save_session() {
    std::string dir = std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.config/vix";
    try { fs::create_directories(dir); } catch (...) { return; }
    std::ofstream f(fs::path(dir) / "session.json");
    if (!f.is_open()) return;
    f << "{\n";
    f << "    \"current_dir\": \"" << current_dir << "\",\n";
    f << "    \"current_tab\": " << current_tab << ",\n";
    f << "    \"files\": [\n";
    for (size_t i = 0; i < tabs.size(); i++) {
        f << "        \"" << tabs[i]->buffer.GetFilename() << "\"";
        if (i + 1 < tabs.size()) f << ",";
        f << "\n";
    }
    f << "    ]\n";
    f << "}\n";
}

void Editor::load_session() {
    std::string dir = std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.config/vix";
    std::ifstream f(fs::path(dir) / "session.json");
    if (!f.is_open()) return;
    std::ostringstream buf; buf << f.rdbuf();
    std::string text = buf.str();
    size_t start = text.find('{'), end = text.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start) return;
    start++; size_t i = start;

    std::string saved_dir;
    std::vector<std::string> files;
    int saved_tab = 0;
    bool have_dir = false;

    // Tiny hand-rolled scan: pull current_dir, current_tab, and each file name.
    auto skip_ws = [&]() { while (i < end && (text[i]==' '||text[i]=='\t'||text[i]=='\n'||text[i]=='\r')) i++; };
    auto read_str = [&]() {
        std::string val;
        while (i < end && text[i] != '"') { if (text[i]=='\\' && i+1<end) i++; val += text[i]; i++; }
        if (i < end) i++;
        return val;
    };
    while (i < end) {
        skip_ws();
        if (i >= end || text[i] != '"') { i++; continue; }
        i++;
        std::string key = read_str();
        skip_ws();
        if (i >= end || text[i] != ':') continue;
        i++;
        skip_ws();
        if (key == "current_dir" && i < end && text[i] == '"') { i++; saved_dir = read_str(); have_dir = true; }
        else if (key == "current_tab") {
            std::string val;
            while (i < end && text[i] != ',' && text[i] != '}' && text[i] != ' ' && text[i] != '\n' && text[i] != '\r') val += text[i++];
            try { saved_tab = std::stoi(val); } catch (...) {}
        }
        else if (key == "files") {
            if (i < end && text[i] == '[') i++;
            while (i < end) {
                skip_ws();
                if (i >= end || text[i] != '"') { if (text[i] == ']') i++; break; }
                i++;
                files.push_back(read_str());
                skip_ws();
                if (i < end && text[i] == ',') i++;
            }
        }
        skip_ws();
        if (i < end && text[i] == ',') i++;
    }

    if (have_dir && fs::is_directory(saved_dir)) {
        current_dir = saved_dir;
        update_sidebar();
    }

    tabs.clear();
    current_tab = 0;
    for (const auto& path : files) {
        std::error_code ec;
        if (!path.empty() && fs::is_regular_file(path, ec)) new_tab(path);
    }
    if (tabs.empty()) new_tab();
    current_tab = std::clamp(saved_tab, 0, (int)tabs.size() - 1);
    detect_language(*tabs[current_tab]);
    clear_search(*tabs[current_tab]);
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

    bool resume = false;
    std::string open_file;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--resume" || a == "-r") resume = true;
        else if (open_file.empty()) open_file = a;
    }

    if (resume) {
        load_session();
    } else if (!open_file.empty()) {
        new_tab(open_file);
    } else {
        new_tab();
    }
    update_sidebar();
    set_status(std::string(VIX_NAME) + " " + VIX_VERSION + " - ^H for help");
}

Editor::~Editor() {}

void Editor::set_status(const std::string& msg) {
    status_msg = msg;
    msg_time = std::chrono::steady_clock::now();
}

void Editor::clamp_cursor(Tab& tab) {
    if (tab.buffer.GetLineCount() == 0) return;
    tab.y = std::clamp(tab.y, 0, tab.buffer.GetLineCount() - 1);
    tab.x = std::clamp(tab.x, 0, (int)tab.buffer[tab.y].length());
}

bool Editor::is_untitled(const Buffer& buffer) {
    return buffer.GetFilename().empty() || buffer.GetFilename().rfind("Untitled", 0) == 0;
}

void Editor::new_tab(const std::string& fname) {
    auto tab = std::make_unique<Tab>();
    if (!fname.empty()) {
        tab->buffer.LoadFile(fname);
        tab->buffer.SetFilename(fname);
        if (tab->buffer.GetLineCount() > 0) tab->buffer.SetModified(false);
    } else {
        static const char* exts[] = {".txt", ".cpp", ".py", ".js", ".rs", ".go", ".c"};
        int lang = std::clamp(settings.default_language, 0, 6);
        tab->buffer.SetFilename("Untitled" + std::string(exts[lang]));
    }
    detect_language(*tab);
    clear_search(*tab);
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
        clear_search(*tabs[current_tab]);
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
    if (fname.empty()) { set_status("Error: empty file name"); return; }
    std::error_code ec;
    if (!fs::exists(fname, ec)) {
        set_status("Error: file not found: " + fname);
        return;
    }
    if (fs::is_directory(fname, ec)) {
        set_status("Error: is a directory: " + fname);
        return;
    }
    tab.buffer.LoadFile(fname);
    tab.buffer.SetFilename(fname);
    detect_language(tab);
    tab.buffer.SetModified(false);
    tab.y = 0; tab.x = 0;
    clear_search(tab);
    set_status("Opened " + fname);
}

bool Editor::save_file(Tab& tab) {
    auto& buf = tab.buffer;
    if (is_untitled(buf)) {
        std::string name = prompt("Save As: ");
        if (name.empty()) return false;
        buf.SetFilename(name);
    }
    if (buf.GetFilename().empty()) return false;

    fs::path parent = fs::path(buf.GetFilename()).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
    }
    if (buf.SaveFile()) {
        last_save_time = std::chrono::steady_clock::now();
        set_status("Saved!");
        return true;
    }
    set_status("Error: save failed: " + buf.GetFilename());
    return false;
}

void Editor::update_sidebar() {
    sidebar_paths.clear();
    sidebar_paths.push_back("..");
    std::vector<fs::path> dirs, files;
    try {
        for (const auto& entry : fs::directory_iterator(current_dir)) {
            auto name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            if (is_dir(entry.path())) dirs.push_back(entry.path());
            else files.push_back(entry.path());
        }
    } catch (const std::exception& e) {
        set_status("Error reading directory: " + std::string(e.what()));
        sidebar_sel = 0;
        sidebar_scroll = 0;
        return;
    }
    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());
    for (auto& d : dirs) sidebar_paths.push_back(d);
    for (auto& f : files) sidebar_paths.push_back(f);
    if (sidebar_sel >= (int)sidebar_paths.size()) sidebar_sel = (int)sidebar_paths.size() - 1;
    if (sidebar_sel < 0) sidebar_sel = 0;
    if (sidebar_scroll > sidebar_sel) sidebar_scroll = sidebar_sel;
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

    std::vector<std::pair<int, std::string>> scored;
    for (const auto& f : all_files) {
        int sc = fuzzy_score(q, f);
        if (sc >= 0) scored.emplace_back(sc, f);
    }

    if (scored.empty()) {
        status_msg = "No matches";
        msg_time = std::chrono::steady_clock::now();
        return;
    }

    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    });
    std::vector<std::string> matches;
    matches.reserve(scored.size());
    for (const auto& s : scored) matches.push_back(s.second);

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

// Replace only the currently highlighted match (search_idx) via the history
// commands, then re-run the search so results stay in sync.
void Editor::replace_current(Tab& tab) {
    if (search_results.empty() || search_idx < 0 || search_idx >= (int)search_results.size()) {
        set_status("No match to replace");
        return;
    }
    std::string r = prompt("Replace with: ");
    if (r.empty()) return;
    auto hit = search_results[search_idx];
    if (hit.line < 0 || hit.line >= tab.buffer.GetLineCount()) return;
    std::string& line = tab.buffer[hit.line];
    if (hit.col < 0 || hit.col + hit.len > (int)line.length()) return;

    auto del = std::make_unique<DeleteCommand>(&tab.buffer, line.substr(hit.col, hit.len), hit.line, hit.col);
    tab.history.execute(std::move(del));
    auto ins = std::make_unique<InsertCommand>(&tab.buffer, r, hit.line, hit.col);
    tab.history.execute(std::move(ins));

    tab.y = hit.line;
    tab.x = hit.col + (int)r.length();
    find_all(tab, search_query);
    if (tab.y >= tab.buffer.GetLineCount()) tab.y = tab.buffer.GetLineCount() - 1;
    if (tab.x > (int)tab.buffer[tab.y].length()) tab.x = (int)tab.buffer[tab.y].length();
    set_status("Replaced match " + std::to_string(search_idx + 1));
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
    if (!save_file(tab) || is_untitled(tab.buffer))
        return;

    std::string cf = tab.buffer.GetFilename();
    fs::path fpath(cf);
    std::string ext = file_ext(cf);
    std::string wd = fpath.parent_path().string();
    std::error_code ec;
    if (!fs::exists(cf, ec)) {
        set_status("Error: file not found: " + cf);
        return;
    }

    std::vector<std::string> run;
    bool has_compile = true;
    if (ext == "cpp" || ext == "cc" || ext == "cxx") {
        run = {"g++", "-Wall", "-Wextra", cf, "-o", "run"};
    } else if (ext == "c") {
        run = {"gcc", "-Wall", "-Wextra", cf, "-o", "run"};
    } else if (ext == "rs") {
        run = {"rustc", cf, "-o", "run"};
    } else if (ext == "py") {
        run = {"python3", cf}; has_compile = false;
    } else if (ext == "go") {
        run = {"go", "run", cf}; has_compile = false;
    } else if (ext == "js" || ext == "mjs") {
        run = {"node", cf}; has_compile = false;
    } else if (ext == "sh" || ext == "bash") {
        run = {"bash", cf}; has_compile = false;
    } else if (ext == "txt" || ext == "md" || ext == "json") {
        set_status("No runner for: " + cf);
        return;
    } else {
        set_status("Unknown file type: " + cf);
        return;
    }

    def_prog_mode();
    endwin();
    printf("\033[2J\033[H");

    int rc = 0;
    if (has_compile) {
        printf(">> compile: %s ", run[0].c_str());
        for (size_t i = 1; i < run.size(); i++) printf("%s ", run[i].c_str());
        printf("\n");
        fflush(stdout);
        int c = run_process(run, wd);
        if (c != 0) {
            printf("\033[1;31m>> build failed (exit %d)\033[0m\n", c);
        } else {
            rc = run_process({"./run"}, wd);
        }
    } else {
        printf(">> %s %s\n", run[0].c_str(), run.back().c_str());
        fflush(stdout);
        rc = run_process(run, wd);
    }

    if (rc == 127)
        printf("\033[1;31m>> error: '%s' not installed or not runnable\033[0m\n", run[0].c_str());

    if (isatty(0)) {
        printf("\nPress Enter...");
        fflush(stdout);
        int ch;
        do { ch = getchar(); } while (ch != '\n' && ch != EOF);
    }
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
    if (settings.auto_save_interval > 0 && tab.buffer.IsModified() && !is_untitled(tab.buffer) &&
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

        std::string n = sidebar_paths[idx].filename().string();
        if (n.empty()) n = "..";
        std::string nl = n;
        for (auto& cc : nl) cc = std::tolower((unsigned char)cc);
        std::string ex = sidebar_paths[idx].extension().string();
        for (auto& ec : ex) ec = std::tolower((unsigned char)ec);
        bool is_dir_entry = is_dir(sidebar_paths[idx]) || n == "..";
        bool is_arch = nl.ends_with(".tar") || nl.ends_with(".tar.gz") || nl.ends_with(".tar.xz") ||
                       nl.ends_with(".tar.bz2") || nl.ends_with(".tgz") || nl.ends_with(".txz") ||
                       nl.ends_with(".zip") || nl.ends_with(".gz") || nl.ends_with(".xz") ||
                       nl.ends_with(".bz2") || nl.ends_with(".7z") || nl.ends_with(".rar");
        int c = CP_DEFAULT;
        if (is_arch) c = CP_TAR;
        else if (ex == ".py") c = CP_CYAN;
        else if (ex == ".cpp" || ex == ".cc" || ex == ".cxx") c = CP_KEYWORD;
        else if (ex == ".hpp" || ex == ".h" || ex == ".hh") c = CP_KEYWORD;
        else if (ex == ".c") c = CP_ORANGE;
        else if (ex == ".js" || ex == ".jsx" || ex == ".mjs") c = CP_JSON;
        else if (ex == ".ts" || ex == ".tsx") c = CP_KEYWORD;
        else if (ex == ".rs") c = CP_ORANGE;
        else if (ex == ".go") c = CP_CYAN;
        else if (ex == ".json") c = CP_JSON;
        else if (ex == ".yaml" || ex == ".yml") c = CP_STRING;
        else if (ex == ".html" || ex == ".css") c = CP_ORANGE;
        else if (ex == ".sh" || ex == ".bash") c = CP_COMMENT;
        else if (ex == ".md" || ex == ".txt") c = CP_LINENUM;

        std::string icon;
        if (n == "..") { icon = "<"; c = CP_DIR; }
        else if (is_dir(sidebar_paths[idx])) icon = ">";
        else if (ex == ".cpp" || ex == ".hpp") icon = "C";
        else if (ex == ".c") icon = "c";
        else if (ex == ".py") icon = "P";
        else if (ex == ".js" || ex == ".jsx" || ex == ".mjs") icon = "J";
        else if (ex == ".ts" || ex == ".tsx") icon = "T";
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

        std::string label = icon + " " + n;
        if (label.length() > SIDEBAR_LABEL_W) label = label.substr(0, SIDEBAR_LABEL_MAX) + "..";

        if (sel) {
            attron(COLOR_PAIR(CP_SELECT));
        } else if (is_dir_entry) {
            attron(A_BOLD | COLOR_PAIR(CP_DIR));
        } else {
            attron(COLOR_PAIR(c));
        }
        mvprintw(i + 2, 1, " %-*s", SIDEBAR_LABEL_W, label.c_str());
        if (sel) {
            attroff(COLOR_PAIR(CP_SELECT));
        } else if (is_dir_entry) {
            attroff(A_BOLD | COLOR_PAIR(CP_DIR));
        } else {
            attroff(COLOR_PAIR(c));
        }
    }
    attroff(COLOR_PAIR(CP_SIDEBAR));
}

void Editor::draw_line(int row, int buf_idx, int max_x, int sw, Tab& tab, int start_col, int end_col) {
    auto& line = tab.buffer[buf_idx];
    if (end_col < 0 || end_col > (int)line.length()) end_col = (int)line.length();
    if (settings.line_numbers) {
        attron(COLOR_PAIR(CP_LINENUM));
        mvprintw(row, sw, "%3d ", buf_idx + 1);
        attroff(COLOR_PAIR(CP_LINENUM));
    }
    int cur_x = sw + (settings.line_numbers ? LINENUM_WIDTH : 0);
    int i = start_col;
    while (i < end_col && cur_x < max_x) {
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
            for (int j = 0; j < search_hit_len && i + j < end_col && cur_x < max_x; j++) {
                addch(line[i + j]); cur_x++;
            }
            i += search_hit_len;
            attroff(COLOR_PAIR(cp));
            continue;
        }

        bool is_match = (match_pos.active && match_pos.y == buf_idx && match_pos.x == i);
        if (is_match) attron(COLOR_PAIR(CP_MATCH));

        char c = line[i];
        bool is_c_cpp = (tab.rules.lang == 1 || tab.rules.lang == 6);

        if (c == '"' || c == '\'') {
            attron(COLOR_PAIR(CP_STRING));
            addch(c); cur_x++; i++;
            while (i < end_col && cur_x < max_x) {
                addch(line[i]); cur_x++;
                if (line[i] == c && (i == 0 || line[i-1] != '\\')) break;
                i++;
            }
            i++;
            attroff(COLOR_PAIR(CP_STRING));
        } else if (is_c_cpp && tab.in_block_comment) {
            attron(COLOR_PAIR(CP_COMMENT));
            while (i < end_col && cur_x < max_x) {
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
            while (i < end_col && cur_x < max_x) { addch(line[i]); cur_x++; i++; }
            attroff(COLOR_PAIR(CP_COMMENT));
        } else if (is_c_cpp && c == '/' && i+1 < (int)line.length() && line[i+1] == '*') {
            attron(COLOR_PAIR(CP_COMMENT));
            addch(line[i]); cur_x++; i++;
            addch(line[i]); cur_x++; i++;
            bool closed = false;
            while (i < end_col && cur_x < max_x) {
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
            while (i < end_col && cur_x < max_x) { addch(line[i]); cur_x++; i++; }
            attroff(COLOR_PAIR(CP_KEYWORD) | A_BOLD);
        } else if (std::isdigit((unsigned char)c)) {
            attron(COLOR_PAIR(CP_ORANGE));
            addch(c); cur_x++; i++;
            attroff(COLOR_PAIR(CP_ORANGE));
        } else if (std::isalpha((unsigned char)c) || c == '_') {
            std::string w;
            while (i < end_col && (std::isalnum((unsigned char)line[i]) || line[i] == '_')) w += line[i++];
            bool is_kw = false;
            for (auto& k : tab.rules.keywords) if (k == w) { is_kw = true; break; }
            if (is_kw) attron(COLOR_PAIR(CP_KEYWORD) | A_BOLD);
            else if (i < end_col && line[i] == '(') attron(COLOR_PAIR(CP_CYAN));
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
    if (settings.word_wrap) {
        int text_w = mx - sw - (settings.line_numbers ? LINENUM_WIDTH : 0);
        if (text_w < 1) text_w = 1;
        int row = 1;
        int line_idx = tab.v_scroll;
        int first_seg = 0;
        if (line_idx < tab.buffer.GetLineCount() && tab.v_seg > 0) {
            first_seg = std::min(tab.v_seg, wrap_rows(line_idx, text_w, tab) - 1);
        }
        while (row < my - 1 && line_idx < tab.buffer.GetLineCount()) {
            int segs = wrap_rows(line_idx, text_w, tab);
            for (int s = first_seg; s < segs && row < my - 1; s++) {
                int start = wrap_col(line_idx, s, text_w, tab);
                int end = (s + 1 < segs) ? wrap_col(line_idx, s + 1, text_w, tab)
                                         : (int)tab.buffer[line_idx].length();
                draw_line(row, line_idx, mx, sw, tab, start, end);
                row++;
            }
            first_seg = 0;
            line_idx++;
        }
    } else {
        for (int i = 1; i < my - 1; i++) {
            int idx = (i - 1) + tab.v_scroll;
            if (idx < tab.buffer.GetLineCount()) draw_line(i, idx, mx, sw, tab);
        }
    }

    draw_status(my, mx);

    place_cursor(my, mx, tab);
    refresh();
}

int Editor::wrap_rows(int buf_idx, int text_w, Tab& tab) const {
    if (!settings.word_wrap) return 1;
    if (buf_idx < 0 || buf_idx >= tab.buffer.GetLineCount()) return 1;
    return (int)wrap_starts(tab.buffer[buf_idx], text_w).size();
}

int Editor::wrap_col(int buf_idx, int seg, int text_w, Tab& tab) const {
    if (!settings.word_wrap) return 0;
    if (buf_idx < 0 || buf_idx >= tab.buffer.GetLineCount()) return 0;
    auto starts = wrap_starts(tab.buffer[buf_idx], text_w);
    if (seg < 0) seg = 0;
    if (seg >= (int)starts.size()) seg = (int)starts.size() - 1;
    return starts[seg];
}

void Editor::place_cursor(int my, int mx, Tab& tab) {
    int sw = show_sidebar ? SIDEBAR_WIDTH : 0;
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
    } else if (settings.word_wrap) {
        int text_w = mx - sw - (settings.line_numbers ? LINENUM_WIDTH : 0);
        if (text_w < 1) text_w = 1;
        int vis = 0;
        for (int li = tab.v_scroll; li < tab.y; li++) vis += wrap_rows(li, text_w, tab);
        auto starts = wrap_starts(tab.buffer[tab.y], text_w);
        int seg = 0;
        for (int s = 0; s < (int)starts.size(); s++) if (starts[s] <= tab.x) seg = s; else break;
        vis += seg - tab.v_seg;
        int cy = std::clamp(vis + 1, 1, my - 2);
        int cx = std::clamp(tab.x - starts[seg] + sw + (settings.line_numbers ? LINENUM_WIDTH : 0), 0, mx - 1);
        move(cy, cx);
    } else {
        int cy = std::clamp(tab.y - tab.v_scroll + 1, 1, my - 2);
        int cx = std::clamp(tab.x - tab.h_scroll + sw + (settings.line_numbers ? LINENUM_WIDTH : 0), 0, mx - 1);
        move(cy, cx);
    }
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

    auto cleanup = [&]() {
        noraw();
        echo();
        if (curscr) endwin();
        tcsetattr(0, TCSANOW, &ot);
        printf("\033[0m");
        fflush(stdout);
    };

    try {
    bool redraw = true;
    bool drawn_match_active = false;
    int drawn_match_x = 0, drawn_match_y = 0;
    int drawn_vs = -1, drawn_hs = -1;

    while (running) {
        int my, mx;
        getmaxyx(stdscr, my, mx);
        auto& tab = *tabs[current_tab];

        tab.y = std::clamp(tab.y, 0, std::max(0, tab.buffer.GetLineCount() - 1));
        tab.x = std::min(tab.x, std::max(0, (int)tab.buffer[tab.y].length()));

        int sw = show_sidebar ? SIDEBAR_WIDTH : 0;
        int text_w = mx - sw - (settings.line_numbers ? LINENUM_WIDTH : 0);
        if (text_w < 1) text_w = 1;

        if (settings.word_wrap) {
            // Horizontal scroll is meaningless under wrap: the line already
            // fills the viewport, so force h_scroll off and keep the wrap
            // segment offset of the top line in range.
            tab.h_scroll = 0;
            int top_rows = wrap_rows(tab.v_scroll, text_w, tab);
            if (tab.v_seg >= top_rows) tab.v_seg = std::max(0, top_rows - 1);
            if (tab.y < tab.v_scroll) { tab.v_scroll = tab.y; tab.v_seg = 0; }
            int cursor_row = 0;
            for (int li = tab.v_scroll; li < tab.y; li++) cursor_row += wrap_rows(li, text_w, tab);
            cursor_row -= tab.v_seg;
            int row_in_line = 0;
            auto starts = wrap_starts(tab.buffer[tab.y], text_w);
            for (int s = 0; s < (int)starts.size(); s++) if (starts[s] <= tab.x) row_in_line = s; else break;
            cursor_row += row_in_line;
            if (cursor_row < 0) { tab.v_scroll = tab.y; tab.v_seg = 0; }
            else if (cursor_row >= my - 2) {
                // Advance the top line until the cursor row fits on screen.
                while (cursor_row >= my - 2 && tab.v_scroll < tab.y) {
                    cursor_row -= wrap_rows(tab.v_scroll, text_w, tab);
                    tab.v_scroll++;
                    tab.v_seg = 0;
                }
                if (cursor_row >= my - 2) {
                    // A single line taller than the viewport: scroll inside it.
                    int overflow = cursor_row - (my - 3);
                    tab.v_seg += overflow;
                    int rows = wrap_rows(tab.y, text_w, tab);
                    tab.v_seg = std::min(tab.v_seg, rows - 1);
                }
            }
        } else {
            if (tab.y < tab.v_scroll) tab.v_scroll = tab.y;
            if (tab.y >= tab.v_scroll + my - 2) tab.v_scroll = tab.y - (my - 2) + 1;
            if (tab.h_scroll < 0) tab.h_scroll = 0;
            if (tab.x < tab.h_scroll) tab.h_scroll = tab.x;
            if (tab.x >= tab.h_scroll + text_w) tab.h_scroll = tab.x - text_w + 1;
        }

        // Pure cursor moves repaint only the status row and cursor instead of
        // the whole viewport; anything that changes text, scroll, the sidebar
        // or the match highlight takes the full redraw path below.
        bool match_changed = (match_pos.active != drawn_match_active) ||
                             (match_pos.active && (match_pos.x != drawn_match_x || match_pos.y != drawn_match_y));
        if (redraw || match_changed || tab.v_scroll != drawn_vs || tab.h_scroll != drawn_hs) {
            draw();
            redraw = false;
            drawn_vs = tab.v_scroll;
            drawn_hs = tab.h_scroll;
            drawn_match_active = match_pos.active;
            drawn_match_x = match_pos.x;
            drawn_match_y = match_pos.y;
        } else {
            draw_status(my, mx);
            place_cursor(my, mx, tab);
            refresh();
        }
        int ch = getch();

        if (ch == KEY_RESIZE) {
            resizeterm(0, 0);
            redraw = true;
            refresh();
            continue;
        }

        if (ch == KEY_MOUSE) {
            redraw = true;
            MEVENT event;
            if (getmouse(&event) == OK) {
                if (event.bstate & BUTTON4_PRESSED) {
                    if (show_sidebar && event.x < SIDEBAR_WIDTH) {
                        if (sidebar_sel > 0) sidebar_sel--;
                    } else if (settings.word_wrap) {
                        if (tab.v_seg > 0) tab.v_seg--;
                        else if (tab.v_scroll > 0) { tab.v_scroll--; tab.v_seg = 0; }
                    } else {
                        if (tab.v_scroll > 0) tab.v_scroll--;
                    }
                } else if (event.bstate & BUTTON5_PRESSED) {
                    if (show_sidebar && event.x < SIDEBAR_WIDTH) {
                        if (sidebar_sel < (int)sidebar_paths.size() - 1) sidebar_sel++;
                    } else if (settings.word_wrap) {
                        int top_rows = wrap_rows(tab.v_scroll, text_w, tab);
                        if (tab.v_seg < top_rows - 1) tab.v_seg++;
                        else if (tab.v_scroll < tab.buffer.GetLineCount() - 1) { tab.v_scroll++; tab.v_seg = 0; }
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
                        int clicked_y = event.y - 1;
                        if (settings.word_wrap) {
                            // Walk lines from the top of the viewport to the
                            // clicked row, using the wrapped row counts.
                            int vis = -tab.v_seg;
                            int line_idx = tab.v_scroll;
                            for (; line_idx < tab.buffer.GetLineCount(); line_idx++) {
                                int rows = wrap_rows(line_idx, text_w, tab);
                                if (clicked_y < vis + rows) break;
                                vis += rows;
                            }
                            if (line_idx >= tab.buffer.GetLineCount()) line_idx = tab.buffer.GetLineCount() - 1;
                            tab.y = line_idx;
                            auto starts = wrap_starts(tab.buffer[tab.y], text_w);
                            int col_in_line = std::clamp(clicked_y - vis, 0, (int)starts.size() - 1);
                            tab.x = std::clamp(starts[col_in_line], 0, (int)tab.buffer[tab.y].length());
                        } else {
                            int line = clicked_y + tab.v_scroll;
                            int sw = show_sidebar ? SIDEBAR_WIDTH : 0;
                            int clicked_x = event.x - sw - (settings.line_numbers ? LINENUM_WIDTH : 0) + tab.h_scroll;
                            if (line >= 0 && line < tab.buffer.GetLineCount()) {
                                tab.y = line;
                                tab.x = std::max(0, std::min((int)tab.buffer[tab.y].length(), clicked_x));
                            }
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
            } else if (ch == CTRL('d')) {
                if (search_results.empty()) { set_status("Nothing to replace"); }
                else replace_current(tab);
            } else if (ch >= 32 && ch <= 126) {
                search_query += (char)ch;
                find_all(tab, search_query);
            }
            redraw = true;
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
            save_session();
            running = false;
        }
        else if (ch == CTRL('s')) { save_file(tab); redraw = true; }
        else if (ch == CTRL('r')) { compile_run(tab); redraw = true; }
        else if (ch == CTRL('w')) { focus_sidebar = !focus_sidebar; redraw = true; }
        else if (ch == CTRL('t')) { show_sidebar = !show_sidebar; redraw = true; }
        else if (ch == KEY_F(2)) {
            def_prog_mode();
            endwin();
            ShowSettingsPanel(settings);
            reset_prog_mode();
            refresh();
            redraw = true;
        }
        else if (ch == CTRL('n')) { new_tab(); redraw = true; }
        else if (ch == 9) {
            auto cmd = std::make_unique<InsertCommand>(&tab.buffer, std::string(settings.tab_width, ' '), tab.y, tab.x);
            tab.history.execute(std::move(cmd));
            tab.x += settings.tab_width;
            redraw = true;
        }
        else if (ch == CTRL('k')) {
            if (tab.y < tab.buffer.GetLineCount()) {
                tab.clipboard = tab.buffer[tab.y];
                system_copy(tab.clipboard);
                auto cmd = std::make_unique<DeleteCommand>(&tab.buffer, tab.clipboard, tab.y, 0);
                tab.history.execute(std::move(cmd));
                tab.x = 0;
            }
            redraw = true;
        }
        else if (ch == CTRL('c')) {
            if (tab.y < tab.buffer.GetLineCount()) {
                tab.clipboard = tab.buffer[tab.y];
                system_copy(tab.clipboard);
                status_msg = "Copied";
                msg_time = std::chrono::steady_clock::now();
            }
            redraw = true;
        }
        else if (ch == CTRL('v')) {
            std::string text = system_paste();
            if (text.empty()) text = tab.clipboard;
            if (!text.empty()) {
                if (text.find('\n') != std::string::npos) {
                    auto cmd = std::make_unique<PasteCommand>(&tab.buffer, text, tab.y, tab.x);
                    tab.history.execute(std::move(cmd));
                } else {
                    auto cmd = std::make_unique<InsertCommand>(&tab.buffer, text, tab.y, tab.x);
                    tab.history.execute(std::move(cmd));
                    tab.x += (int)text.length();
                }
                tab.clipboard = text;
            }
            redraw = true;
        }
        else if (ch == CTRL('f')) {
            in_search_mode = true;
            search_query.clear();
            search_results.clear();
            search_idx = -1;
            redraw = true;
        }
        else if (ch == KEY_F(3)) {
            if (!search_results.empty()) search_next(tab);
            redraw = true;
        }
        else if (ch == KEY_F(13)) {
            if (!search_results.empty()) search_prev(tab);
            redraw = true;
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
            redraw = true;
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
            redraw = true;
        }
        else if (ch == CTRL('p')) {
            fuzzy_finder();
            redraw = true;
        }
        else if (ch == CTRL('h')) {
            int myw, mxw;
            getmaxyx(stdscr, myw, mxw);
            int wy = (myw - HELP_H) / 2, wx = (mxw - HELP_W) / 2;
            if (wy < 0) wy = 0;
            if (wx < 0) wx = 0;
            WINDOW* hw = newwin(HELP_H, HELP_W, wy, wx);
            if (hw) {
                auto row = [&](int r, const char* left, const char* right) {
                    mvwprintw(hw, r, HELP_X, "%-*s  %s", HELP_COL_W, left, right);
                };
                auto head = [&](int r, const char* left, const char* right) {
                    wattron(hw, A_BOLD);
                    mvwprintw(hw, r, HELP_X, "%-*s  %s", HELP_COL_W, left, right);
                    wattroff(hw, A_BOLD);
                };

                box(hw, 0, 0);
                mvwprintw(hw, 0, HELP_X, " %s %s - VIX HELP ", VIX_NAME, VIX_VERSION);
                mvwaddch(hw, 1, 0, ACS_LTEE);
                mvwhline(hw, 1, 1, ACS_HLINE, HELP_W - 2);
                mvwaddch(hw, 1, HELP_W - 1, ACS_RTEE);

                head(2,  " FILES",             " NAVIGATION");
                row(3,   "  ^S Save",          "  ^G Go To Line");
                row(4,   "  ^Q Quit",          "  ^P Find File");
                row(5,   "  ^N New Tab",       "  ^D Find & Replace");
                row(6,   "  ^\\ Close Tab",    "  F3 Next   S-F3 Prev");
                row(7,   "  F5 / S-Tab Switch","  ^H Help");
                head(9,  " EDIT",              " COMPILE / MISC");
                row(10,  "  ^Z Undo  ^Y Redo", "  ^R Compile & Run");
                row(11,  "  ^K Cut Line",      "  F2 Settings");
                row(12,  "  ^C Copy Line",     "  ^T Sidebar  ^W Focus");
                row(13,  "  ^V Paste (clipbd)","  'a' New   'd' Delete");
                mvwvline(hw, HELP_DIV_R, HELP_DIV_X, ACS_VLINE, HELP_DIV_H);
                mvwprintw(hw, HELP_FOOTER_R - 1, HELP_X, " In search: ^D replaces the highlighted match only");
                mvwprintw(hw, HELP_FOOTER_R, HELP_X, " Search: ^F  |  flags: ^R regex  ^C case  ^W word");
                wrefresh(hw);
                wgetch(hw);
                delwin(hw);
                redraw = true;
            }
        }
        else if (ch == CTRL('z')) {
            if (tab.history.undo()) {
                status_msg = "[Undo]";
                tab.y = std::clamp(tab.y, 0, std::max(0, tab.buffer.GetLineCount() - 1));
                tab.x = std::min(tab.x, std::max(0, (int)tab.buffer[tab.y].length()));
            } else status_msg = "[Nothing to undo]";
            msg_time = std::chrono::steady_clock::now();
            redraw = true;
        }
        else if (ch == CTRL('y')) {
            if (tab.history.redo()) {
                status_msg = "[Redo]";
                tab.y = std::clamp(tab.y, 0, std::max(0, tab.buffer.GetLineCount() - 1));
                tab.x = std::min(tab.x, std::max(0, (int)tab.buffer[tab.y].length()));
            } else status_msg = "[Nothing to redo]";
            msg_time = std::chrono::steady_clock::now();
            redraw = true;
        }
        else if (ch == KEY_BTAB) {
            int prev = (current_tab - 1 + (int)tabs.size()) % (int)tabs.size();
            switch_tab(prev);
            redraw = true;
        }
        else if (ch == KEY_F(5)) {
            int next = (current_tab + 1) % (int)tabs.size();
            switch_tab(next);
            redraw = true;
        }
        else if (ch == 28) {
            close_tab(current_tab);
            if (!running) break;
            redraw = true;
            continue;
        }
        else if (focus_sidebar) {
            redraw = true;
            if (ch == KEY_UP && sidebar_sel > 0) sidebar_sel--;
            else if (ch == KEY_DOWN && sidebar_sel < (int)sidebar_paths.size() - 1) sidebar_sel++;
            else if (ch == 'a') {
                std::string n = prompt("New File: ");
                if (!n.empty()) {
                    std::ofstream f(n);
                    if (f.is_open()) {
                        f.close();
                        set_status("Created " + n);
                        update_sidebar();
                    } else {
                        set_status("Error: cannot create " + n);
                    }
                }
            } else if (ch == 'd') {
                if (sidebar_sel > 0) {
                    std::error_code ec;
                    std::uintmax_t removed = fs::remove_all(sidebar_paths[sidebar_sel], ec);
                    if (ec || removed == 0)
                        set_status("Error: cannot delete " + sidebar_paths[sidebar_sel].filename().string());
                    else {
                        set_status("Deleted " + sidebar_paths[sidebar_sel].filename().string());
                        update_sidebar();
                    }
                }
            } else if (ch == '\n') {
                if (is_dir(sidebar_paths[sidebar_sel])) {
                    std::error_code ec;
                    fs::path target = fs::canonical(sidebar_paths[sidebar_sel], ec);
                    if (ec) {
                        set_status("Error: cannot enter directory");
                    } else {
                        fs::path old_cwd = fs::current_path();
                        fs::current_path(target, ec);
                        if (ec) {
                            fs::current_path(old_cwd);
                            set_status("Error: cannot change directory");
                        } else {
                            current_dir = target.string();
                            sidebar_sel = 0;
                            sidebar_scroll = 0;
                            update_sidebar();
                        }
                    }
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
                redraw = true;
            } else if (ch == '\n') {
                if (tab.y < tab.buffer.GetLineCount()) {
                    std::string first_half = tab.buffer[tab.y].substr(0, tab.x);
                    std::string rest = tab.buffer[tab.y].substr(tab.x);
                    std::string indent;
                    for (int i = 0; i < tab.x && i < (int)tab.buffer[tab.y].length(); i++) {
                        if (tab.buffer[tab.y][i] == ' ' || tab.buffer[tab.y][i] == '\t') indent += tab.buffer[tab.y][i];
                        else break;
                    }
                    if (settings.auto_indent) {
                        std::string trimmed = first_half;
                        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
                            trimmed.pop_back();
                        if (!trimmed.empty()) {
                            char last = trimmed.back();
                            if (last == '{' || last == '(' || last == '[' || last == ':')
                                indent += std::string(settings.tab_width, ' ');
                        }
                    }
                    auto cmd = std::make_unique<NewLineCommand>(&tab.buffer, tab.y + 1,
                                                               first_half, indent + rest,
                                                               tab.buffer[tab.y]);
                    if (tab.history.execute(std::move(cmd))) {
                        tab.y++;
                        tab.x = (int)indent.length();
                    }
                }
                redraw = true;
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
                redraw = true;
            }
        }

        if (!focus_sidebar && !in_search_mode) {
            find_match(tab);
        }
    }
    } catch (const std::exception& e) {
        cleanup();
        std::fprintf(stderr, "vix: internal error: %s\n", e.what());
        return;
    } catch (...) {
        cleanup();
        std::fprintf(stderr, "vix: unknown internal error\n");
        return;
    }

    cleanup();
    system("stty sane && clear");
}
