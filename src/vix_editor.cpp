#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <ncurses.h>
#include <algorithm>
#include <filesystem>
#include <sys/wait.h>
#include <clocale>
#include <chrono>
#include <stdexcept>
#include <unistd.h>
#include <map>
#include <termios.h>
#include <cctype>
#include <memory>
#include <cstdio>
#include "core/buffer.hpp"
#include "history/history.hpp"
#include "ui/settings.hpp"

namespace fs = std::filesystem;

#ifndef CTRL
#define CTRL(c) ((c) & 0x1f)
#endif

// Theme Colors
#define CP_DEFAULT 1
#define CP_KEYWORD 2
#define CP_STRING  3
#define CP_COMMENT 4
#define CP_LINENUM 5
#define CP_STATUS  6
#define CP_ORANGE  7
#define CP_CYAN    8
#define CP_ERROR   9
#define CP_SIDEBAR 10
#define CP_SELECT  11
#define CP_GHOST   12
#define CP_MATCH   13
#define CP_SEARCH  14

struct SyntaxRules {
    int lang;
    std::string name;
    std::vector<std::string> keywords;
};

class Vix_ultimate
{
private:
    Buffer buffer;
    History history;
    std::string current_dir, clipboard, last_search;
    int x, y, v_scroll, h_scroll, sidebar_scroll;
    bool running, show_sidebar, focus_sidebar;
    SyntaxRules rules;
    std::vector<fs::path> sidebar_paths;
    int sidebar_sel;
    std::string status_msg;
    std::chrono::steady_clock::time_point msg_time;
    std::chrono::steady_clock::time_point last_save_time;

    PyObject *pModule = nullptr, *pFuncSuggest = nullptr, *pFuncLint = nullptr;
    std::string ghost_text;
    std::map<int, std::string> cpp_errors;
    struct {
        int x, y;
        bool active;
    } match_pos;

    // Search state
    std::vector<std::pair<int,int>> search_results;
    int search_idx;
    std::string search_query;
    bool in_search_mode;
    bool in_block_comment;

    Settings settings;

    // Named constants - extracted from magic numbers
    static constexpr int SIDEBAR_WIDTH = 22;
    static constexpr size_t HISTORY_MAX_LEVELS = 50;
    static constexpr int HELP_HEIGHT = 12;
    static constexpr int HELP_WIDTH = 50;
    static constexpr int GHOST_CONTEXT_LINES = 15;
    static constexpr int PROMPT_BUFFER_SIZE = 256;
    static constexpr int STATUS_MSG_TIMEOUT_SEC = 3;

public:
    Vix_ultimate(std::string fname) : history(HISTORY_MAX_LEVELS),
        x(0), y(0), v_scroll(0), h_scroll(0), sidebar_scroll(0),
        running(true), show_sidebar(true), focus_sidebar(false), sidebar_sel(0)
    {
        current_dir = fs::current_path().string();
        last_save_time = std::chrono::steady_clock::now();
        match_pos.active = false;
        search_idx = -1;
        in_search_mode = false;
        in_block_comment = false;
        clipboard = "";
        LoadSettings(settings);
        ApplyTheme(themes[settings.theme]);
        // Create config file on first run if it doesn't exist
        SaveSettings(settings);
        InitPython();
        DetectLanguage();
        if (!fname.empty()) LoadFile(fname);
        else {
            buffer.PushBack("");
            static const char* DEF_EXTS[] = { ".txt", ".cpp", ".py", ".js", ".rs", ".go", ".c" };
            int lang = settings.default_language;
            const char* ext = (lang >= 0 && lang <= 6) ? DEF_EXTS[lang] : ".cpp";
            buffer.SetFilename("Untitled" + std::string(ext));
        }
        UpdateSidebar();
    }

    ~Vix_ultimate()
    {
        if (pFuncSuggest) Py_DECREF(pFuncSuggest);
        if (pFuncLint) Py_DECREF(pFuncLint);
        if (pModule) Py_DECREF(pModule);
        if (Py_IsInitialized()) Py_Finalize();
    }

    void Notify(std::string msg, bool err = false)
    {
        status_msg = (err ? "![ERR] " : ">> ") + msg;
        msg_time = std::chrono::steady_clock::now();
    }

    void CopyToSystemClipboard(const std::string& text) {
        FILE* pipe = popen("xclip -selection clipboard", "w");
        if (pipe) {
            fwrite(text.c_str(), 1, text.size(), pipe);
            pclose(pipe);
        }
    }

    std::string PasteFromSystemClipboard() {
        std::string result;
        FILE* pipe = popen("xclip -selection clipboard -o", "r");
        if (pipe) {
            char buf[1024];
            while (fgets(buf, sizeof(buf), pipe)) result += buf;
            pclose(pipe);
        }
        return result;
    }

    void InitPython()
    {
        if (!Py_IsInitialized()) Py_Initialize();

        // Get path to executable
        char result[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
        std::string exePath = (count != -1) ? std::string(result, count) : "";
        std::string exeDir = exePath.substr(0, exePath.find_last_of("/"));

        PyRun_SimpleString("import sys");
        PyRun_SimpleString("import os");
        std::string addPathCmd = "sys.path.append('" + exeDir + "')";
        PyRun_SimpleString(addPathCmd.c_str());

        pModule = PyImport_ImportModule("vix_brain");
        if (pModule) {
            PyObject* pBrain = PyObject_GetAttrString(pModule, "brain");
            if (pBrain) {
                pFuncSuggest = PyObject_GetAttrString(pBrain, "get_ghost_suggestion");
                pFuncLint = PyObject_GetAttrString(pBrain, "lint_cpp");
                Py_DECREF(pBrain);
            }
        }
    }

    void UpdateLinter()
    {
        if (!pFuncLint || rules.lang != 1) return;
        std::string text = "";
        for(auto& l : buffer.GetAllLines()) text += l + "\n";
        PyObject *pArgs = PyTuple_New(1);
        PyTuple_SetItem(pArgs, 0, PyUnicode_FromString(text.c_str()));
        PyObject *pResult = PyObject_CallObject(pFuncLint, pArgs);
        Py_DECREF(pArgs);
        cpp_errors.clear();
        if (pResult && PyList_Check(pResult)) {
            for (Py_ssize_t i = 0; i < PyList_Size(pResult); i++) {
                PyObject* item = PyList_GetItem(pResult, i);
                PyObject* pL = PyDict_GetItemString(item, "line");
                PyObject* pM = PyDict_GetItemString(item, "msg");
                if (pL && pM) cpp_errors[PyLong_AsLong(pL)] = PyUnicode_AsUTF8(pM);
            }
        }
        Py_XDECREF(pResult);
    }

    void FindMatch()
    {
        match_pos.active = false;
        if (y >= buffer.GetLineCount() || (y >= 0 && x >= (int)buffer[y].length())) return;
        char c = buffer[y][x];
        std::string open = "{([", close = ")}]";
        int dir = 0, pair_idx = -1;
        if ((pair_idx = (int)open.find(c)) != (int)std::string::npos) dir = 1;
        else if ((pair_idx = (int)close.find(c)) != (int)std::string::npos) dir = -1;
        if (dir == 0) return;
        char target = (dir == 1) ? close[pair_idx] : open[pair_idx];
        int depth = 0, cy = y, cx = x;
        // Scan initial line: start one position past the bracket
        cx += dir;
        while (cy >= 0 && cy < buffer.GetLineCount()) {
            // Scan within the current line
            while (cx >= 0 && cx < (int)buffer[cy].length()) {
                if (buffer[cy][cx] == c) depth++;
                else if (buffer[cy][cx] == target) {
                    if (depth == 0) {
                        match_pos = {cx, cy, true};
                        return;
                    }
                    depth--;
                }
                cx += dir;
            }
            // Move to the next (or previous) line
            cy += dir;
            if (cy < 0 || cy >= buffer.GetLineCount()) break;
            cx = (dir == 1) ? 0 : (int)buffer[cy].length() - 1;
        }
    }

    void FindAll(const std::string& q)
    {
        search_results.clear();
        search_query = q;
        search_idx = -1;
        if (q.empty()) return;
        for (int i = 0; i < buffer.GetLineCount(); i++) {
            size_t pos = 0;
            while ((pos = buffer[i].find(q, pos)) != std::string::npos) {
                search_results.emplace_back(i, (int)pos);
                pos++;
            }
        }
        if (!search_results.empty()) {
            search_idx = 0;
            y = search_results[0].first;
            x = search_results[0].second;
        }
    }

    void SearchNext()
    {
        if (search_results.empty()) return;
        search_idx = (search_idx + 1) % (int)search_results.size();
        y = search_results[search_idx].first;
        x = search_results[search_idx].second;
    }

    void SearchPrev()
    {
        if (search_results.empty()) return;
        search_idx = (search_idx - 1 + (int)search_results.size()) % (int)search_results.size();
        y = search_results[search_idx].first;
        x = search_results[search_idx].second;
    }

    void ClearSearch()
    {
        if (!search_results.empty()) {
            search_results.clear();
            search_query.clear();
            search_idx = -1;
        }
    }

    void UpdateSuggestion()
    {
        if (!pFuncSuggest) return;
        std::string context = "";
        int line_count = buffer.GetLineCount();
    int start = (y > GHOST_CONTEXT_LINES) ? (y - GHOST_CONTEXT_LINES) : 0;
    int end = (y + GHOST_CONTEXT_LINES < line_count) ? (y + GHOST_CONTEXT_LINES) : line_count;
        for(int i = start; i < end; i++) context += buffer[i] + "\n";
        PyObject *pArgs = PyTuple_New(2);
        PyTuple_SetItem(pArgs, 0, PyUnicode_FromString(buffer[y].substr(0, x).c_str()));
        PyTuple_SetItem(pArgs, 1, PyUnicode_FromString(context.c_str()));
        PyObject *pRes = PyObject_CallObject(pFuncSuggest, pArgs);
        Py_DECREF(pArgs);
        if (pRes) {
            if (PyUnicode_Check(pRes)) {
                ghost_text = PyUnicode_AsUTF8(pRes);
            }
            Py_XDECREF(pRes);
        }
    }

    void DetectLanguage()
    {
        std::string fname = buffer.GetFilename();
        size_t dot = fname.find_last_of(".");
        std::string ext = "";
        if (dot != std::string::npos && dot < fname.length() - 1) ext = fname.substr(dot + 1);
        if (ext == "cpp" || ext == "hpp" || ext == "cc" || ext == "hh") rules = {1, "C++", {"int", "void", "return", "include", "iostream", "std", "cout", "endl", "using", "namespace", "class", "public", "private", "if", "else", "for", "while"}};
        else if (ext == "c" || ext == "h") rules = {6, "C", {
            "auto", "break", "case", "char", "const", "continue", "default", "do",
            "double", "else", "enum", "extern", "float", "for", "goto", "if",
            "int", "long", "register", "return", "short", "signed", "sizeof",
            "static", "struct", "switch", "typedef", "union", "unsigned", "void",
            "volatile", "while",
            "include", "define", "ifdef", "ifndef", "endif", "undef", "pragma",
            "printf", "scanf", "malloc", "calloc", "realloc", "free",
            "fopen", "fclose", "fread", "fwrite", "fprintf", "fscanf",
            "fgets", "fputs", "sprintf", "snprintf",
            "strlen", "strcpy", "strncpy", "strcmp", "strncmp", "strcat", "strncat",
            "strstr", "strchr", "strtok", "memcpy", "memmove", "memset", "memcmp",
            "atoi", "atol", "atof", "exit", "assert", "qsort", "bsearch",
            "NULL", "size_t", "FILE", "EOF", "EXIT_SUCCESS", "EXIT_FAILURE",
            "stdin", "stdout", "stderr", "main", "bool", "true", "false"
        }};
        else if (ext == "py") rules = {2, "Python", {"def", "class", "import", "from", "return", "if", "elif", "else", "for", "while", "print"}};
        else if (ext == "js" || ext == "mjs") rules = {3, "JavaScript", {"function", "const", "let", "var", "async", "await", "import", "export", "class", "extends", "if", "else", "for", "while", "return", "try", "catch", "throw", "new", "this", "super", "true", "false", "null", "undefined", "break", "continue", "switch", "case", "default"}};
        else if (ext == "rs") rules = {4, "Rust", {"fn", "let", "mut", "const", "struct", "impl", "trait", "pub", "mod", "use", "crate", "self", "super", "if", "else", "for", "while", "loop", "match", "return", "async", "await", "move", "ref", "type", "where", "unsafe", "static", "true", "false", "Some", "None", "Ok", "Err", "break", "continue", "enum", "dyn"}};
        else if (ext == "go") rules = {5, "Go", {"func", "var", "const", "type", "struct", "interface", "package", "import", "if", "else", "for", "switch", "case", "return", "go", "defer", "chan", "select", "true", "false", "nil", "make", "new", "len", "cap", "append", "copy", "delete", "map", "range", "panic", "recover", "break", "continue", "default", "fallthrough"}};
        else rules = {0, "Text", {}};
    }

    void LoadFile(std::string fname)
    {
        buffer.LoadFile(fname);
        if (buffer.IsEmpty()) {
            buffer.Clear();
            buffer.PushBack("");
            buffer.SetFilename(fname);
            DetectLanguage();
            return;
        }
        DetectLanguage();
        buffer.SetModified(false);
        y = 0;
        x = 0;
        Notify("Opened " + fname);
    }

    void UpdateSidebar()
    {
        sidebar_paths.clear();
        sidebar_paths.push_back("..");
        try {
            for (const auto& entry : fs::directory_iterator(current_dir)) sidebar_paths.push_back(entry.path());
            std::sort(sidebar_paths.begin() + 1, sidebar_paths.end());
        } catch(const std::exception& e) {
            std::cerr << "Sidebar error: " << e.what() << std::endl;
        }
    }

    void InitColors()
    {
        start_color();
        use_default_colors();
        ApplyTheme(themes[settings.theme]);
    }

    void DrawLine(int row, int buf_idx, int max_x, int sidebar_w)
    {
        std::string line = buffer[buf_idx];
        bool has_err = cpp_errors.count(buf_idx);
        if (settings.line_numbers) {
            attron(COLOR_PAIR(has_err ? CP_ERROR : CP_LINENUM));
            mvprintw(row, sidebar_w, "%3d ", buf_idx + 1);
            attroff(COLOR_PAIR(has_err ? CP_ERROR : CP_LINENUM));
        }
        int cur_x = sidebar_w + (settings.line_numbers ? 4 : 0);
        for (int i = 0; i < (int)line.length() && cur_x < max_x; i++) {
            bool is_search_start = false;
            int search_hit_idx = -1;
            if (!search_query.empty()) {
                for (int si = 0; si < (int)search_results.size(); si++) {
                    if (search_results[si].first == buf_idx && search_results[si].second == i) {
                        is_search_start = true;
                        search_hit_idx = si;
                        break;
                    }
                }
            }
            if (is_search_start) {
                int cp = (search_hit_idx == search_idx) ? CP_MATCH : CP_SEARCH;
                attron(COLOR_PAIR(cp));
                for (int j = 0; j < (int)search_query.length() && cur_x < max_x; j++) {
                    addch(line[i + j]);
                    cur_x++;
                }
                i += (int)search_query.length() - 1;
                attroff(COLOR_PAIR(cp));
                continue;
            }

            bool is_match = (match_pos.active && match_pos.y == buf_idx && match_pos.x == i);
            if (is_match) attron(COLOR_PAIR(CP_MATCH));
            bool is_c_cpp = (rules.lang == 1 || rules.lang == 6);
            if (line[i] == '"' || line[i] == '\'') {
                attron(COLOR_PAIR(CP_STRING));
                char q = line[i];
                addch(line[i++]);
                cur_x++;
                while(i < (int)line.length() && cur_x < max_x) {
                    addch(line[i]);
                    cur_x++;
                    if (line[i] == q && (i==0 || line[i-1]!='\\')) break;
                    i++;
                }
                attroff(COLOR_PAIR(CP_STRING));
            } else if (is_c_cpp && in_block_comment) {
                attron(COLOR_PAIR(CP_COMMENT));
                while(i < (int)line.length() && cur_x < max_x) {
                    if (line[i] == '*' && i+1 < (int)line.length() && line[i+1] == '/') {
                        addch(line[i]); cur_x++;
                        i++;
                        addch(line[i]); cur_x++;
                        in_block_comment = false;
                        break;
                    }
                    addch(line[i]); cur_x++;
                    i++;
                }
                attroff(COLOR_PAIR(CP_COMMENT));
            } else if (is_c_cpp && line[i] == '/' && i+1 < (int)line.length() && line[i+1] == '/') {
                attron(COLOR_PAIR(CP_COMMENT));
                while(i < (int)line.length() && cur_x < max_x) {
                    addch(line[i]); cur_x++; i++;
                }
                attroff(COLOR_PAIR(CP_COMMENT));
            } else if (is_c_cpp && line[i] == '/' && i+1 < (int)line.length() && line[i+1] == '*') {
                attron(COLOR_PAIR(CP_COMMENT));
                addch(line[i]); cur_x++;
                i++;
                addch(line[i]); cur_x++;
                i++;
                bool closed = false;
                while(i < (int)line.length() && cur_x < max_x) {
                    addch(line[i]); cur_x++;
                    if (line[i] == '*' && i+1 < (int)line.length() && line[i+1] == '/') {
                        i++;
                        addch(line[i]); cur_x++;
                        closed = true;
                        break;
                    }
                    i++;
                }
                if (!closed) in_block_comment = true;
                attroff(COLOR_PAIR(CP_COMMENT));
            } else if (is_c_cpp && line[i] == '#' && (i == 0 || line[i-1] == ' ' || line[i-1] == '\t')) {
                attron(COLOR_PAIR(CP_KEYWORD) | A_BOLD);
                while(i < (int)line.length() && cur_x < max_x) {
                    addch(line[i]); cur_x++; i++;
                }
                attroff(COLOR_PAIR(CP_KEYWORD) | A_BOLD);
            } else if (isdigit(line[i])) {
                attron(COLOR_PAIR(CP_ORANGE));
                addch(line[i]);
                cur_x++;
                attroff(COLOR_PAIR(CP_ORANGE));
            } else if (isalpha(line[i]) || line[i] == '_') {
                std::string w = "";
                while(i < (int)line.length() && (isalnum(line[i]) || line[i]=='_')) w += line[i++];
                i--;
                bool is_kw = false;
                for(auto& k : rules.keywords) if(k == w) is_kw = true;
                if(is_kw) attron(COLOR_PAIR(CP_KEYWORD) | A_BOLD);
                else if (i+1 < (int)line.length() && (line[i+1] == '(')) attron(COLOR_PAIR(CP_CYAN));
                for(char c : w) if(cur_x < max_x) {
                        addch(c);
                        cur_x++;
                    }
                attroff(COLOR_PAIR(CP_KEYWORD) | A_BOLD | COLOR_PAIR(CP_CYAN));
            } else {
                addch(line[i]);
                cur_x++;
            }
            if (is_match) attroff(COLOR_PAIR(CP_MATCH));
        }
        if (buf_idx == y && !ghost_text.empty() && !focus_sidebar) {
            attron(COLOR_PAIR(CP_GHOST));
            for(char c : ghost_text) if(cur_x < max_x) {
                    addch(c);
                    cur_x++;
                }
            attroff(COLOR_PAIR(CP_GHOST));
        }
    }

    void SaveFile()
    {
        if (buffer.GetFilename() == "Untitled.cpp") {
            buffer.SetFilename(Prompt("Save As: "));
        }
        if (buffer.GetFilename().empty()) return;
        buffer.SaveFile();
        buffer.SetModified(false);
        last_save_time = std::chrono::steady_clock::now();
        Notify("Saved!");
    }

    std::string Prompt(std::string msg)
    {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        attron(COLOR_PAIR(CP_STATUS));
        mvhline(max_y-1, 0, ' ', max_x);
        mvprintw(max_y-1, 1, "%s", msg.c_str());
        echo();
        char b[PROMPT_BUFFER_SIZE];
        getnstr(b, PROMPT_BUFFER_SIZE - 1);
        noecho();
        return std::string(b);
    }

    void CompileAndRun()
    {
        SaveFile();
        def_prog_mode();
        endwin();
        system("reset -e && clear");
        std::string curr_file = buffer.GetFilename();
        std::string cmd = "";
        // Extract extension after last dot for proper detection (not substring match)
        std::string ext = "";
        size_t dot = curr_file.find_last_of(".");
        if (dot != std::string::npos) ext = curr_file.substr(dot + 1);
        // Shell-safe quoting for filenames with spaces
        if (ext == "cpp" || ext == "hpp" || ext == "cc" || ext == "hh")
            cmd = "g++ \"" + curr_file + "\" -o run && ./run";
        else if (ext == "c")
            cmd = "gcc \"" + curr_file + "\" -o run && ./run";
        else if (ext == "py") cmd = "python3 \"" + curr_file + "\"";
        else if (ext == "rs") cmd = "rustc \"" + curr_file + "\" -o run && ./run";
        else if (ext == "go") cmd = "go run \"" + curr_file + "\"";
        else if (ext == "js" || ext == "mjs") cmd = "node \"" + curr_file + "\"";
        if(!cmd.empty()) {
            std::cout<<"\033[1;33m>> VIX EXECUTION: "<<cmd<<"\033[0m\n";
            system(cmd.c_str());
        }
        std::cout<<"\nPress Enter...";
        std::cin.ignore();
        std::cin.get();
        reset_prog_mode();
        refresh();
    }

    void Draw()
    {
        erase();
        int my, mx;
        getmaxyx(stdscr, my, mx);
        int sw = show_sidebar ? SIDEBAR_WIDTH : 0;
        auto now = std::chrono::steady_clock::now();
        if (settings.auto_save_interval > 0 && buffer.IsModified() && std::chrono::duration_cast<std::chrono::seconds>(now - last_save_time).count() >= settings.auto_save_interval && buffer.GetLineCount() > 0) SaveFile();
        if (show_sidebar) {
            attron(COLOR_PAIR(CP_SIDEBAR));
            for(int i=0; i<my-1; i++) mvaddch(i, sw-1, '|');
            mvprintw(0, 1, focus_sidebar ? "✿ PROJECT *" : "✿ PROJECT");

            // Sidebar Scroll Logic - prevent underflow
            int visible_lines = my - 2;
            if (visible_lines > 0) {
                if (sidebar_scroll < 0) sidebar_scroll = 0;
                if (sidebar_sel < sidebar_scroll) sidebar_scroll = sidebar_sel;
                if (sidebar_sel >= sidebar_scroll + visible_lines) sidebar_scroll = sidebar_sel - visible_lines + 1;
                if (sidebar_scroll < 0) sidebar_scroll = 0;
            }

            for (int i = 0; i < my - 2; i++) {
                int idx = i + sidebar_scroll;
                if (idx >= (int)sidebar_paths.size()) break;

                if (focus_sidebar && idx == sidebar_sel) attron(COLOR_PAIR(CP_SELECT));
                std::string n = sidebar_paths[idx].filename().string();
                if(n=="") n="..";
                std::string ex = sidebar_paths[idx].extension().string();
                int c = CP_DEFAULT;
                if(ex==".py") c=CP_CYAN;
                else if(ex==".cpp"||ex==".h"||ex==".hpp"||ex==".c") c=CP_SIDEBAR;
                else if(ex==".html") c=CP_ORANGE;
                else if(ex==".zip") c=CP_ERROR;
                else if(ex==".js"||ex==".json") c=CP_STRING;
                if(fs::is_directory(sidebar_paths[idx])) attron(A_BOLD | COLOR_PAIR(CP_KEYWORD));
                else attron(COLOR_PAIR(c));
                mvprintw(i+1, 1, " %-18s", n.substr(0, std::min((size_t)18, n.length())).c_str());
                attroff(A_BOLD | COLOR_PAIR(CP_KEYWORD) | COLOR_PAIR(c) | COLOR_PAIR(CP_SELECT));
            }
            attroff(COLOR_PAIR(CP_SIDEBAR));
        }
        in_block_comment = false;
        for (int i = 0; i < my - 1; i++) {
            int idx = i + v_scroll;
            if (idx < buffer.GetLineCount()) DrawLine(i, idx, mx, sw);
        }
        attron(COLOR_PAIR(CP_STATUS));
        mvhline(my-1, 0, ' ', mx);
        if (in_search_mode) {
            // Search bar (VS Code style)
            mvprintw(my-1, 1, " Find: %s", search_query.c_str());
            if (!search_results.empty()) {
                mvprintw(my-1, mx - 16, " %d/%d ", search_idx + 1, (int)search_results.size());
            } else if (!search_query.empty()) {
                mvprintw(my-1, mx - 16, " no matches ");
            }
        } else if (cpp_errors.count(y)) {
            mvprintw(my-1, 1, "![LINTER] %s%s", cpp_errors[y].c_str(),
                buffer.IsModified() ? " [modified]" : "");
        } else {
            if(std::chrono::duration_cast<std::chrono::seconds>(now-msg_time).count()<STATUS_MSG_TIMEOUT_SEC) {
                mvprintw(my-1, 1, "%s", status_msg.c_str());
            } else {
                // Show match count if search results are active
                std::string suffix = "";
                if (!search_results.empty()) {
                    suffix = " | [" + std::to_string(search_idx + 1) + "/" + std::to_string((int)search_results.size()) + "]";
                }
                mvprintw(my-1, 1, " VIX | %s%s | %s | L:%d C:%d/%d%s | ^H Help",
                    buffer.GetFilename().c_str(),
                    buffer.IsModified() ? " *" : "",
                    rules.name.c_str(),
                    y+1, x+1, buffer.GetLineCount(), suffix.c_str());
            }
        }
        attroff(COLOR_PAIR(CP_STATUS));
        // Cursor positioning
        if (focus_sidebar) move(sidebar_sel - sidebar_scroll + 1, 1);
        else if (in_search_mode) move(my-1, 8 + (int)search_query.length());
        else move(y-v_scroll, x+sw+4);
        refresh();
    }

    void Run()
    {
        struct termios ot, nt;
        tcgetattr(0, &ot);
        nt = ot;
        nt.c_iflag &= ~(IXON|IXOFF);
        tcsetattr(0, TCSANOW, &nt);
        setlocale(LC_ALL, "");
        initscr();
        noecho();
        raw();
        keypad(stdscr, 1);
        InitColors();
        mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL); // Enable Mouse

        while (running) {
            int my, mx;
            getmaxyx(stdscr, my, mx);
            if (y < v_scroll) v_scroll = y;
            if (y >= v_scroll + my - 1) v_scroll = y - (my-1) + 1;
            Draw();
            int ch = getch();

            if (ch == KEY_MOUSE) {
                MEVENT event;
                if (getmouse(&event) == OK) {
                    if (event.bstate & BUTTON4_PRESSED) { // Scroll Up
                        if (show_sidebar && event.x < SIDEBAR_WIDTH) {
                            if (sidebar_sel > 0) sidebar_sel--;
                        } else {
                            if (v_scroll > 0) v_scroll--;
                        }
                    } else if (event.bstate & BUTTON5_PRESSED) { // Scroll Down
                        if (show_sidebar && event.x < SIDEBAR_WIDTH) {
                            if (sidebar_sel < (int)sidebar_paths.size() - 1) sidebar_sel++;
                        } else {
                            if (v_scroll < buffer.GetLineCount() - 1) v_scroll++;
                        }
                    } else if (event.bstate & BUTTON1_PRESSED) { // Left Click
                        if (show_sidebar && event.x < SIDEBAR_WIDTH) {
                            focus_sidebar = true;
                            int clicked_idx = event.y - 1; // Adjust for header
                            if (clicked_idx >= 0 && clicked_idx < (int)sidebar_paths.size()) {
                                sidebar_sel = clicked_idx;
                            }
                        } else {
                            focus_sidebar = false;
                            int clicked_y = event.y + v_scroll;
                            int clicked_x = event.x - (show_sidebar ? SIDEBAR_WIDTH : 0) - 4; // Adjust for line num
                            if (clicked_y >= 0 && clicked_y < buffer.GetLineCount()) {
                                y = clicked_y;
                                x = std::max(0, std::min((int)buffer[y].length(), clicked_x));
                            }
                        }
                    }
                }
                continue;
            }

            if (in_search_mode) {
                if (ch == 27) { // Escape → cancel search
                    in_search_mode = false;
                    search_results.clear();
                    search_query.clear();
                    search_idx = -1;
                } else if (ch == '\n' || ch == KEY_ENTER) {
                    // Enter → confirm search, stay on current match
                    in_search_mode = false;
                } else if (ch == KEY_F(3)) {
                    if (!search_results.empty()) SearchNext();
                } else if (ch == KEY_F(13) || ch == KEY_BACKSPACE) {
                    // Shift+F3 → previous; Backspace → delete char
                    if (ch == KEY_BACKSPACE) {
                        if (!search_query.empty()) {
                            search_query.pop_back();
                            FindAll(search_query);
                        } else {
                            in_search_mode = false;
                            search_results.clear();
                            search_idx = -1;
                        }
                    } else {
                        if (!search_results.empty()) SearchPrev();
                    }
                } else if (ch >= 32 && ch <= 126) {
                    search_query += (char)ch;
                    FindAll(search_query);
                }
                continue;
            }

            if (ch == CTRL('q')) break;
            if (ch == CTRL('s')) SaveFile();
            if (ch == CTRL('r')) CompileAndRun();
            if (ch == CTRL('w')) focus_sidebar = !focus_sidebar;
            if (ch == CTRL('t')) show_sidebar = !show_sidebar;
            if (ch == KEY_F(2)) {
                ShowSettingsPanel(settings);
            }
            if (ch == CTRL('h')) {
                WINDOW* hw = newwin(HELP_HEIGHT, HELP_WIDTH, (my-HELP_HEIGHT)/2, (mx-HELP_WIDTH)/2);
                if (hw) {
                    box(hw, 0, 0);
                    mvwprintw(hw, 0, 2, " HELP ");
                    mvwprintw(hw, 2, 2, "^S: Save  ^Q: Quit  ^R: Run  F2: Settings");
                    mvwprintw(hw, 3, 2, "^K: Kill  ^C: Copy  ^V: Paste");
                    mvwprintw(hw, 4, 2, "^F: Search  F3: Next  S+F3: Prev");
                    mvwprintw(hw, 5, 2, "^D: Replace  TAB: Ghost  ^T: Sidebar");
                    mvwprintw(hw, 6, 2, "^Z: Undo  ^Y: Redo");
                    mvwprintw(hw, 7, 2, "Sidebar: 'a': New File  'd': Delete");
                    mvwprintw(hw, 8, 2, "^P: System Clipboard Paste");
                    wrefresh(hw);
                    wgetch(hw);
                    delwin(hw);
                }
            }
            if (ch == CTRL('z')) {
                if(history.undo()) Notify("[Undo]");
                else Notify("[Nothing to undo]");
            }
            if (ch == CTRL('y')) {
                if(history.redo()) Notify("[Redo]");
                else Notify("[Nothing to redo]");
            }
            if (focus_sidebar) {
                if(ch == KEY_UP && sidebar_sel > 0) sidebar_sel--;
                else if(ch == KEY_DOWN && sidebar_sel < (int)sidebar_paths.size()-1) sidebar_sel++;
                else if(ch == 'a') {
                    std::string n = Prompt("New File: ");
                    if(!n.empty()) {
                        std::ofstream f(n);
                        f.close();
                        UpdateSidebar();
                    }
                } else if(ch == 'd') {
                    if(sidebar_sel>0) {
                        fs::remove_all(sidebar_paths[sidebar_sel]);
                        UpdateSidebar();
                    }
                } else if(ch == '\n') {
                    if(fs::is_directory(sidebar_paths[sidebar_sel])) {
                        current_dir=fs::canonical(sidebar_paths[sidebar_sel]).string();
                        fs::current_path(current_dir);
                        sidebar_sel=0;
                        UpdateSidebar();
                    } else {
                        LoadFile(sidebar_paths[sidebar_sel].filename().string());
                        focus_sidebar=false;
                    }
                }
            } else {
                if (ch == 9) {
                    ClearSearch();
                    if(!ghost_text.empty()) {
                        auto cmd = std::make_unique<InsertCommand>(&buffer, ghost_text, y, x);
                        history.execute(std::move(cmd));
                        x+=ghost_text.length();
                        ghost_text="";
                    } else {
                        auto cmd = std::make_unique<InsertCommand>(&buffer, std::string(settings.tab_width, ' '), y, x);
                        history.execute(std::move(cmd));
                        x+=2;
                    }
                } else if (ch == CTRL('k')) {
                    ClearSearch();
                    if(!buffer.IsEmpty() && y < buffer.GetLineCount()) {
                        clipboard=buffer[y];
                        auto cmd = std::make_unique<DeleteCommand>(&buffer, clipboard, y, 0);
                        history.execute(std::move(cmd));
                        x=0;
                    }
                } else if (ch == CTRL('c')) {
                    clipboard = buffer[y];
                    CopyToSystemClipboard(clipboard);
                    Notify("Copied");
                } else if (ch == CTRL('v')) {
                    ClearSearch();
                    if(!clipboard.empty()) {
                        auto cmd = std::make_unique<InsertCommand>(&buffer, clipboard, y, x);
                        history.execute(std::move(cmd));
                        x += clipboard.length();
                    }
                } else if (ch == CTRL('p')) {
                    ClearSearch();
                    {
                        std::string sys_clip = PasteFromSystemClipboard();
                        if (!sys_clip.empty()) {
                            if (sys_clip.back() == '\n') sys_clip.pop_back();
                            clipboard = sys_clip;
                            auto cmd = std::make_unique<InsertCommand>(&buffer, clipboard, y, x);
                            history.execute(std::move(cmd));
                            x += clipboard.length();
                            Notify("Pasted from system clipboard");
                        } else {
                            Notify("No system clipboard content");
                        }
                    }
                } else if (ch == CTRL('f')) {
                    // Enter VS Code-style search mode (live search)
                    in_search_mode = true;
                    search_query.clear();
                    search_results.clear();
                    search_idx = -1;
                } else if (ch == KEY_F(3)) {
                    // F3 → next match (works after search mode is done)
                    if (!search_results.empty()) SearchNext();
                } else if (ch == KEY_F(13)) {
                    // Shift+F3 → previous match
                    if (!search_results.empty()) SearchPrev();
                } else if (ch == CTRL('d')) {
                    ClearSearch();
                    {
                        std::string q = Prompt("Find: ");
                        if (q.empty()) { Notify("Cancelled"); continue; }
                        std::string r = Prompt("Replace with: ");
                        int count = 0;
                        for (int i = 0; i < buffer.GetLineCount(); i++) {
                            size_t pos = 0;
                            while ((pos = buffer[i].find(q, pos)) != std::string::npos) {
                                buffer[i] = buffer[i].substr(0, pos) + r + buffer[i].substr(pos + q.length());
                                pos += r.length();
                                count++;
                            }
                        }
                        if (count > 0) {
                            buffer.SetModified(true);
                            Notify("Replaced " + std::to_string(count) + " occurrence" + (count != 1 ? "s" : ""));
                        } else {
                            Notify("Not found: " + q);
                        }
                    }
                } else if (ch == CTRL('g')) {
                    std::string input = Prompt("Go To Line: ");
                    if (input.empty()) { Notify("Cancelled"); continue; }
                    try {
                        int line = std::stoi(input);
                        if (line < 1 || line > buffer.GetLineCount()) {
                            Notify("Line out of range (1-" + std::to_string(buffer.GetLineCount()) + ")");
                        } else {
                            y = line - 1; x = 0;
                            Notify("Jumped to L:" + std::to_string(line));
                        }
                    } catch (...) { Notify("Invalid line number"); }
                } else if (ch == KEY_UP && y>0) y--;
                else if (ch == KEY_DOWN && y< buffer.GetLineCount()-1) y++;
                else if (ch == KEY_LEFT && x>0) x--;
                else if (ch == KEY_RIGHT && y < buffer.GetLineCount() && x<(int)buffer[y].length()) x++;
                else if (ch == 127 || ch == KEY_BACKSPACE) {
                    ClearSearch();
                    if (x > 0 && x <= (int)buffer[y].length()) {
                        char deleted = buffer[y][x-1];
                        auto cmd = std::make_unique<DeleteCommand>(&buffer, std::string(1, deleted), y, x-1);
                        history.execute(std::move(cmd));
                        x--;

                        // Auto-pair deletion: if we just deleted an opener and the
                        // next char matches, delete the closer too
                        char next = (x < (int)buffer[y].length()) ? buffer[y][x] : '\0';
                        if ((deleted == '(' && next == ')') ||
                            (deleted == '{' && next == '}') ||
                            (deleted == '[' && next == ']') ||
                            (deleted == '"' && next == '"')) {
                            auto cmd2 = std::make_unique<DeleteCommand>(&buffer, std::string(1, next), y, x);
                            history.execute(std::move(cmd2));
                        }
                    } else if (y > 0) {
                        // Line-join: not undoable via Command pattern
                        x = (int)buffer[y-1].length();
                        buffer[y-1] += buffer[y];
                        buffer.EraseLine(y);
                        y--;
                        buffer.SetModified(true);
                    }
                } else if (ch == '\n') {
                    ClearSearch();
                    if (y < buffer.GetLineCount()) {
                        std::string rest = (x < (int)buffer[y].length()) ? buffer[y].substr(x) : "";

                        // Auto-indent: copy leading whitespace from the current line
                        std::string indent = "";
                        for (int i = 0; i < x && i < (int)buffer[y].length(); i++) {
                            char c = buffer[y][i];
                            if (c == ' ' || c == '\t') indent += c;
                            else break;
                        }

                        // Extra indent after opening braces, brackets, parens, or colon
                        std::string trimmed = buffer[y].substr(0, x);
                        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
                            trimmed.pop_back();
                        if (!trimmed.empty()) {
                            char last = trimmed.back();
                            if (last == '{' || last == '(' || last == '[' || last == ':')
                                indent += std::string(settings.tab_width, ' ');
                        }

                        buffer[y] = (x < (int)buffer[y].length()) ? buffer[y].substr(0, x) : buffer[y];
                        auto cmd = std::make_unique<NewLineCommand>(&buffer, y + 1, indent + rest);
                        history.execute(std::move(cmd));
                        y++;
                        x = (int)indent.length();
                    }
                } else if (ch >= 32 && ch <= 126) {
                    ClearSearch();
                    if (y < buffer.GetLineCount() && x <= (int)buffer[y].length()) {
                        if (ch == '(') {
                            auto cmd = std::make_unique<InsertCommand>(&buffer, "()", y, x);
                            history.execute(std::move(cmd));
                            x++;
                        } else if (ch == '{') {
                            auto cmd = std::make_unique<InsertCommand>(&buffer, "{}", y, x);
                            history.execute(std::move(cmd));
                            x++;
                        } else if (ch == '[') {
                            auto cmd = std::make_unique<InsertCommand>(&buffer, "[]", y, x);
                            history.execute(std::move(cmd));
                            x++;
                        } else if (ch == '"') {
                            auto cmd = std::make_unique<InsertCommand>(&buffer, "\"\"", y, x);
                            history.execute(std::move(cmd));
                            x++;
                        } else {
                            auto cmd = std::make_unique<InsertCommand>(&buffer, std::string(1, (char)ch), y, x);
                            history.execute(std::move(cmd));
                            x++;
                        }
                    }
                }
            }
            if(!focus_sidebar) {
                FindMatch();
                UpdateSuggestion();
                UpdateLinter();
            }
        }
        noraw();
        echo();
        endwin();
        tcsetattr(0, TCSANOW, &ot);
        system("stty sane && clear"); // Master Reset to fix the 'pyramid' bug
    }
};

int main(int argc, char** argv)
{
    Vix_ultimate vix((argc < 2) ? "" : argv[1]);
    vix.Run();
    return 0;
}
