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

namespace fs = std::filesystem;
using namespace std;

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

struct SyntaxRules {
    int lang;
    string name;
    vector<string> keywords;
};

class VixUltimate {
private:
    vector<string> buffer;
    string filename, current_dir, clipboard;
    int x, y, v_scroll, h_scroll, sidebar_scroll;
    bool running, modified, show_sidebar, focus_sidebar;
    SyntaxRules rules;
    vector<fs::path> sidebar_paths;
    int sidebar_sel;
    string status_msg;
    chrono::steady_clock::time_point msg_time;
    chrono::steady_clock::time_point last_save_time;
    
    PyObject *pModule = nullptr, *pFuncSuggest = nullptr, *pFuncLint = nullptr;
    string ghost_text;
    map<int, string> cpp_errors;
    struct { int x, y; bool active; } match_pos;

public:
    VixUltimate(string fname) : filename(fname), x(0), y(0), v_scroll(0), h_scroll(0), sidebar_scroll(0),
                               running(true), modified(false), show_sidebar(true), 
                               focus_sidebar(false), sidebar_sel(0) {
        current_dir = fs::current_path().string();
        last_save_time = chrono::steady_clock::now();
        match_pos.active = false; clipboard = "";
        InitPython(); 
        DetectLanguage();
        if(!fname.empty()) LoadFile(fname); else { buffer.push_back(""); filename="Untitled.cpp"; }
        UpdateSidebar();
    }

    ~VixUltimate() {
        if (pFuncSuggest) Py_DECREF(pFuncSuggest);
        if (pFuncLint) Py_DECREF(pFuncLint);
        if (pModule) Py_DECREF(pModule);
        if (Py_IsInitialized()) Py_Finalize();
    }

    void Notify(string msg, bool err = false) { 
        status_msg = (err ? "![ERR] " : ">> ") + msg; 
        msg_time = chrono::steady_clock::now(); 
    }

    void InitPython() {
        if (!Py_IsInitialized()) Py_Initialize();
        PyRun_SimpleString("import sys\nsys.path.append('/home/jad')");
        pModule = PyImport_ImportModule("vix_brain");
        if (pModule) {
            PyObject* pBrain = PyObject_GetAttrString(pModule, "vix_brain");
            if (pBrain) {
                pFuncSuggest = PyObject_GetAttrString(pBrain, "get_ghost_suggestion");
                pFuncLint = PyObject_GetAttrString(pBrain, "lint_cpp");
                Py_DECREF(pBrain);
            }
        }
    }

    void UpdateLinter() {
        if (!pFuncLint || rules.lang != 1) return;
        string text = ""; for(auto& l : buffer) text += l + "\n";
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

    void FindMatch() {
        match_pos.active = false;
        if (y >= (int)buffer.size() || (y >= 0 && x >= (int)buffer[y].length())) return;
        char c = buffer[y][x];
        string open = "{([", close = ")}]";
        int dir = 0, pair_idx = -1;
        if ((pair_idx = (int)open.find(c)) != (int)string::npos) dir = 1;
        else if ((pair_idx = (int)close.find(c)) != (int)string::npos) dir = -1;
        if (dir == 0) return;
        char target = (dir == 1) ? close[pair_idx] : open[pair_idx];
        int depth = 0, cy = y, cx = x;
        while (cy >= 0 && cy < (int)buffer.size()) {
            cx += dir;
            if (cx < 0 || cx >= (int)buffer[cy].length()) {
                cy += dir;
                if (cy >= 0 && cy < (int)buffer.size()) cx = (dir == 1) ? 0 : (int)buffer[cy].length() - 1;
                else break;
                continue;
            }
            if (buffer[cy][cx] == c) depth++;
            else if (buffer[cy][cx] == target) {
                if (depth == 0) { match_pos = {cx, cy, true}; return; }
                depth--;
            }
        }
    }

    void UpdateSuggestion() {
        if (!pFuncSuggest) return;
        string context = "";
        int start = max(0, y - 15), end = min((int)buffer.size(), y + 15);
        for(int i = start; i < end; i++) context += buffer[i] + "\n";
        PyObject *pArgs = PyTuple_New(2);
        PyTuple_SetItem(pArgs, 0, PyUnicode_FromString(buffer[y].substr(0, x).c_str()));
        PyTuple_SetItem(pArgs, 1, PyUnicode_FromString(context.c_str()));
        PyObject *pRes = PyObject_CallObject(pFuncSuggest, pArgs);
        Py_DECREF(pArgs);
        if (pRes && PyUnicode_Check(pRes)) { ghost_text = PyUnicode_AsUTF8(pRes); Py_XDECREF(pRes); } else ghost_text = "";
    }

    void DetectLanguage() {
        size_t dot = filename.find_last_of(".");
        string ext = (dot != string::npos) ? filename.substr(dot + 1) : "";
        if (ext == "cpp" || ext == "c" || ext == "h" || ext == "hpp") rules = {1, "C++", {"int", "void", "return", "include", "iostream", "std", "cout", "endl", "using", "namespace", "class", "public", "private", "if", "else", "for", "while"}};
        else if (ext == "py") rules = {2, "Python", {"def", "class", "import", "from", "return", "if", "elif", "else", "for", "while", "print"}};
        else rules = {0, "Text", {}};
    }

    void LoadFile(string fname) {
        ifstream f(fname); if(!f.is_open()){ buffer.clear(); buffer.push_back(""); filename=fname; DetectLanguage(); return; }
        buffer.clear(); filename = fname; DetectLanguage();
        string l; while (getline(f, l)) buffer.push_back(l); f.close();
        if (buffer.empty()) buffer.push_back("");
        modified = false; y = 0; x = 0; Notify("Opened " + fname);
    }

    void UpdateSidebar() {
        sidebar_paths.clear(); sidebar_paths.push_back("..");
        try { for (const auto& entry : fs::directory_iterator(current_dir)) sidebar_paths.push_back(entry.path());
              sort(sidebar_paths.begin() + 1, sidebar_paths.end()); } catch(...) {}
    }

    void InitColors() {
        start_color(); use_default_colors();
        init_pair(CP_DEFAULT, COLOR_WHITE, -1);
        init_pair(CP_KEYWORD, COLOR_MAGENTA, -1);
        init_pair(CP_STRING, COLOR_YELLOW, -1);
        init_pair(CP_COMMENT, COLOR_GREEN, -1);
        init_pair(CP_LINENUM, COLOR_CYAN, -1);
        init_pair(CP_STATUS, COLOR_BLACK, COLOR_CYAN);
        init_pair(CP_ORANGE, COLOR_RED, -1);
        init_pair(CP_CYAN, COLOR_CYAN, -1);
        init_pair(CP_ERROR, COLOR_WHITE, COLOR_RED);
        init_pair(CP_SIDEBAR, 12, -1);
        init_pair(CP_SELECT, COLOR_BLACK, COLOR_CYAN);
        init_pair(CP_GHOST, 8, -1);
        init_pair(CP_MATCH, COLOR_WHITE, COLOR_MAGENTA);
    }

    void DrawLine(int row, int buf_idx, int max_x, int sidebar_w) {
        string line = buffer[buf_idx];
        bool has_err = cpp_errors.count(buf_idx);
        attron(COLOR_PAIR(has_err ? CP_ERROR : CP_LINENUM));
        mvprintw(row, sidebar_w, "%3d ", buf_idx + 1);
        attroff(COLOR_PAIR(has_err ? CP_ERROR : CP_LINENUM));
        int cur_x = sidebar_w + 4;
        for (int i = 0; i < (int)line.length() && cur_x < max_x; i++) {
            bool is_match = (match_pos.active && match_pos.y == buf_idx && match_pos.x == i);
            if (is_match) attron(COLOR_PAIR(CP_MATCH));
            if (line[i] == '"' || line[i] == '\'') {
                attron(COLOR_PAIR(CP_STRING)); char q = line[i]; addch(line[i++]); cur_x++;
                while(i < (int)line.length() && cur_x < max_x) {
                    addch(line[i]); cur_x++; if (line[i] == q && (i==0 || line[i-1]!='\\')) break; i++;
                }
                attroff(COLOR_PAIR(CP_STRING));
            } else if (isdigit(line[i])) {
                attron(COLOR_PAIR(CP_ORANGE)); addch(line[i]); cur_x++; attroff(COLOR_PAIR(CP_ORANGE));
            } else if (isalpha(line[i]) || line[i] == '#' || line[i] == '_') {
                string w = ""; while(i < (int)line.length() && (isalnum(line[i]) || line[i]=='#' || line[i]=='_')) w += line[i++];
                i--; bool is_kw = false; for(auto& k : rules.keywords) if(k == w) is_kw = true;
                if(is_kw) attron(COLOR_PAIR(CP_KEYWORD) | A_BOLD);
                else if (i+1 < (int)line.length() && (line[i+1] == '(')) attron(COLOR_PAIR(CP_CYAN));
                for(char c : w) if(cur_x < max_x) { addch(c); cur_x++; }
                attroff(COLOR_PAIR(CP_KEYWORD) | A_BOLD | COLOR_PAIR(CP_CYAN));
            } else { addch(line[i]); cur_x++; }
            if (is_match) attroff(COLOR_PAIR(CP_MATCH));
        }
        if (buf_idx == y && !ghost_text.empty() && !focus_sidebar) {
            attron(COLOR_PAIR(CP_GHOST)); for(char c : ghost_text) if(cur_x < max_x) { addch(c); cur_x++; } attroff(COLOR_PAIR(CP_GHOST));
        }
    }

    void SaveFile() {
        if (filename == "Untitled.cpp") filename = Prompt("Save As: ");
        if (filename.empty()) return;
        ofstream f(filename); if (f.is_open()) { for(auto& l:buffer) f<<l<<"\n"; f.close(); modified=false; last_save_time=chrono::steady_clock::now(); Notify("Saved!"); } 
    }

    string Prompt(string msg) {
        int max_y, max_x; getmaxyx(stdscr, max_y, max_x);
        attron(COLOR_PAIR(CP_STATUS)); mvhline(max_y-1, 0, ' ', max_x); mvprintw(max_y-1, 1, "%s", msg.c_str());
        echo(); char b[256]; getnstr(b, 255); noecho(); return string(b);
    }

    void CompileAndRun() {
        SaveFile(); def_prog_mode(); endwin(); system("reset -e && clear");
        string cmd = ""; if(filename.find(".cpp")!=string::npos) cmd="g++ "+filename+" -o run && ./run"; else if(filename.find(".py")!=string::npos) cmd="python3 "+filename;
        if(!cmd.empty()){ cout<<"\033[1;33m>> VIX EXECUTION: "<<cmd<<"\033[0m\n"; system(cmd.c_str()); }
        cout<<"\nPress Enter..."; cin.ignore(); cin.get(); reset_prog_mode(); refresh();
    }

    void Draw() {
        erase(); int my, mx; getmaxyx(stdscr, my, mx); int sw = show_sidebar ? 22 : 0;
        auto now = chrono::steady_clock::now();
        if (modified && chrono::duration_cast<chrono::seconds>(now - last_save_time).count() >= 20) SaveFile();
        if (show_sidebar) {
            attron(COLOR_PAIR(CP_SIDEBAR)); for(int i=0; i<my-1; i++) mvaddch(i, sw-1, '|');
            mvprintw(0, 1, focus_sidebar ? "✿ PROJECT *" : "✿ PROJECT");
            
            // Sidebar Scroll Logic
            if (sidebar_sel < sidebar_scroll) sidebar_scroll = sidebar_sel;
            if (sidebar_sel >= sidebar_scroll + my - 2) sidebar_scroll = sidebar_sel - (my - 2) + 1;

            for (int i = 0; i < my - 2; i++) {
                int idx = i + sidebar_scroll;
                if (idx >= (int)sidebar_paths.size()) break;
                
                if (focus_sidebar && idx == sidebar_sel) attron(COLOR_PAIR(CP_SELECT));
                string n = sidebar_paths[idx].filename().string(); if(n=="") n="..";
                string ex = sidebar_paths[idx].extension().string(); int c = CP_DEFAULT;
                if(ex==".py") c=CP_CYAN; else if(ex==".cpp"||ex==".h"||ex==".hpp") c=CP_SIDEBAR; else if(ex==".html") c=CP_ORANGE; else if(ex==".zip") c=CP_ERROR; else if(ex==".js"||ex==".json") c=CP_STRING;
                if(fs::is_directory(sidebar_paths[idx])) attron(A_BOLD | COLOR_PAIR(CP_KEYWORD)); else attron(COLOR_PAIR(c));
                mvprintw(i+1, 1, " %-18s", n.substr(0, min((size_t)18, n.length())).c_str());
                attroff(A_BOLD | COLOR_PAIR(CP_KEYWORD) | COLOR_PAIR(c) | COLOR_PAIR(CP_SELECT));
            }
            attroff(COLOR_PAIR(CP_SIDEBAR));
        }
        for (int i = 0; i < my - 1; i++) { int idx = i + v_scroll; if (idx < (int)buffer.size()) DrawLine(i, idx, mx, sw); }
        attron(COLOR_PAIR(CP_STATUS)); mvhline(my-1, 0, ' ', mx);
        if (cpp_errors.count(y)) mvprintw(my-1, 1, "![LINTER] %s", cpp_errors[y].c_str());
        else { if(chrono::duration_cast<chrono::seconds>(now-msg_time).count()<3) mvprintw(my-1, 1, "%s", status_msg.c_str());
               else mvprintw(my-1, 1, " VIX | %s | L:%d C:%d | ^H Help", filename.c_str(), y+1, x+1); }
        attroff(COLOR_PAIR(CP_STATUS));
        if (focus_sidebar) move(sidebar_sel - sidebar_scroll + 1, 1); else move(y-v_scroll, x+sw+4); refresh();
    }

    void Run() {
        struct termios ot, nt; tcgetattr(0, &ot); nt = ot; nt.c_iflag &= ~(IXON|IXOFF); tcsetattr(0, TCSANOW, &nt);
        setlocale(LC_ALL, ""); initscr(); noecho(); raw(); keypad(stdscr, 1); InitColors();
        mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL); // Enable Mouse
        
        while (running) {
            int my, mx; getmaxyx(stdscr, my, mx);
            if (y < v_scroll) v_scroll = y; if (y >= v_scroll + my - 1) v_scroll = y - (my-1) + 1;
            Draw(); int ch = getch();
            
            if (ch == KEY_MOUSE) {
                MEVENT event;
                if (getmouse(&event) == OK) {
                    if (event.bstate & BUTTON4_PRESSED) { // Scroll Up
                        if (show_sidebar && event.x < 22) {
                            if (sidebar_sel > 0) sidebar_sel--;
                        } else {
                            if (v_scroll > 0) v_scroll--;
                        }
                    } else if (event.bstate & BUTTON5_PRESSED) { // Scroll Down
                        if (show_sidebar && event.x < 22) {
                            if (sidebar_sel < (int)sidebar_paths.size() - 1) sidebar_sel++;
                        } else {
                            if (v_scroll < (int)buffer.size() - 1) v_scroll++;
                        }
                    } else if (event.bstate & BUTTON1_PRESSED) { // Left Click
                        if (show_sidebar && event.x < 22) {
                            focus_sidebar = true;
                            int clicked_idx = event.y - 1; // Adjust for header
                            if (clicked_idx >= 0 && clicked_idx < (int)sidebar_paths.size()) {
                                sidebar_sel = clicked_idx;
                            }
                        } else {
                            focus_sidebar = false;
                            int clicked_y = event.y + v_scroll;
                            int clicked_x = event.x - (show_sidebar ? 22 : 0) - 4; // Adjust for line num
                            if (clicked_y >= 0 && clicked_y < (int)buffer.size()) {
                                y = clicked_y;
                                x = max(0, min((int)buffer[y].length(), clicked_x));
                            }
                        }
                    }
                }
                continue;
            }

            if (ch == CTRL('q')) break;
            if (ch == CTRL('s')) SaveFile();
            if (ch == CTRL('r')) CompileAndRun();
            if (ch == CTRL('w')) focus_sidebar = !focus_sidebar;
            if (ch == CTRL('t')) show_sidebar = !show_sidebar;
            if (ch == CTRL('h')) {
                WINDOW* hw = newwin(12, 50, (my-12)/2, (mx-50)/2); box(hw, 0, 0); mvwprintw(hw, 0, 2, " HELP ");
                mvwprintw(hw, 2, 2, "^S: Save  ^Q: Quit  ^R: Run"); mvwprintw(hw, 3, 2, "^K: Kill  ^C: Copy  ^V: Paste");
                mvwprintw(hw, 4, 2, "^F: Find  ^G: GoTo  ^T: Sidebar"); mvwprintw(hw, 5, 2, "TAB: AI Ghost Accept");
                mvwprintw(hw, 7, 2, "Sidebar: 'a': New File  'd': Delete"); wrefresh(hw); wgetch(hw); delwin(hw);
            }
            if (focus_sidebar) {
                if(ch == KEY_UP && sidebar_sel > 0) sidebar_sel--;
                else if(ch == KEY_DOWN && sidebar_sel < (int)sidebar_paths.size()-1) sidebar_sel++;
                else if(ch == 'a') { string n = Prompt("New File: "); if(!n.empty()){ ofstream f(n); f.close(); UpdateSidebar(); } }
                else if(ch == 'd') { if(sidebar_sel>0){ fs::remove_all(sidebar_paths[sidebar_sel]); UpdateSidebar(); } }
                else if(ch == '\n') {
                    if(fs::is_directory(sidebar_paths[sidebar_sel])){ current_dir=fs::canonical(sidebar_paths[sidebar_sel]).string(); fs::current_path(current_dir); sidebar_sel=0; UpdateSidebar(); }
                    else { LoadFile(sidebar_paths[sidebar_sel].filename().string()); focus_sidebar=false; }
                }
            } else {
                if (ch == 9) { if(!ghost_text.empty()){ buffer[y].insert(x, ghost_text); x+=ghost_text.length(); ghost_text=""; modified=true; } else { buffer[y].insert(x, "  "); x+=2; } }
                else if (ch == CTRL('k')) { if(!buffer.empty()){ clipboard=buffer[y]; buffer.erase(buffer.begin()+y); if(buffer.empty())buffer.push_back(""); if(y>=(int)buffer.size())y=(int)buffer.size()-1; x=0; modified=true; } }
                else if (ch == CTRL('c')) { clipboard = buffer[y]; Notify("Copied"); }
                else if (ch == CTRL('v')) { if(!clipboard.empty()){ buffer.insert(buffer.begin()+(++y), clipboard); modified=true; } }
                else if (ch == KEY_UP && y>0) y--;
                else if (ch == KEY_DOWN && y<(int)buffer.size()-1) y++;
                else if (ch == KEY_LEFT && x>0) x--;
                else if (ch == KEY_RIGHT && x<(int)buffer[y].length()) x++;
                else if (ch == 127 || ch == KEY_BACKSPACE) {
                    if (x > 0 && x <= (int)buffer[y].length()) { buffer[y].erase(--x, 1); modified = true; }
                    else if (y > 0) { x = (int)buffer[y-1].length(); buffer[y-1] += buffer[y]; buffer.erase(buffer.begin() + y); y--; modified = true; }
                } else if (ch == '\n') {
                    string next = buffer[y].substr(x); buffer[y] = buffer[y].substr(0, x);
                    buffer.insert(buffer.begin()+(++y), next); x=0; modified=true;
                } else if (ch >= 32 && ch <= 126) {
                    if (x <= (int)buffer[y].length()) {
                        buffer[y].insert(x++, 1, (char)ch); modified=true;
                        if (ch == '(') { buffer[y].insert(x, ")"); modified=true; }
                        else if (ch == '{') { buffer[y].insert(x, "}"); modified=true; }
                        else if (ch == '[') { buffer[y].insert(x, "]"); modified=true; }
                        else if (ch == '"') { buffer[y].insert(x, "\""); modified=true; }
                    }
                }
            }
            if(!focus_sidebar){ FindMatch(); UpdateSuggestion(); UpdateLinter(); }
        }
        endwin(); tcsetattr(0, TCSANOW, &ot);
    }
};

int main(int argc, char** argv) { 
    VixUltimate vix((argc < 2) ? "" : argv[1]); 
    vix.Run(); 
    return 0; 
}
