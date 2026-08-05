/*
 * editor.hpp - ncurses front-end and the main event loop
 *
 * Editor owns the tab list (each Tab couples a Buffer, its History and its
 * cursor/scroll state) and the whole-screen UI (tab bar, sidebar, status,
 * search). run() dispatches every keypress; a try/catch around it restores
 * the terminal if anything throws so the user is never left in raw mode.
 */
#pragma once
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <filesystem>
#include <regex>
#include <ncurses.h>
#include "core/buffer.hpp"
#include "history/history.hpp"
#include "ui/settings.hpp"

namespace fs = std::filesystem;

#ifndef VIX_VERSION
#define VIX_VERSION "1.0.3"
#endif
#define VIX_NAME "vix"

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
#define CP_MATCH   12
#define CP_SEARCH  13
#define CP_DIR     14
#define CP_TAR     15
#define CP_JSON    16

// Width of the line-number gutter when enabled: 3 digit columns + 1 space.
static constexpr int LINENUM_WIDTH = 4;

struct SyntaxRules {
    int lang;
    std::string name;
    std::vector<std::string> keywords;
};

struct Tab {
    Buffer buffer;
    History history;
    int x, y;
    int v_scroll;
    int h_scroll;
    SyntaxRules rules;
    std::string clipboard;
    bool in_block_comment;
    Tab() : x(0), y(0), v_scroll(0), h_scroll(0), in_block_comment(false) {}
};

struct SearchHit {
    int line;
    int col;
    int len;
};

class Editor {
public:
    Editor(int argc, char** argv);
    ~Editor();
    void run();

private:
    void detect_language(Tab& tab);
    void load_file(Tab& tab, const std::string& fname);
    bool save_file(Tab& tab);
    void draw();
    void draw_tab_bar(int mx);
    void draw_sidebar(int my, int mx);
    void draw_line(int row, int buf_idx, int max_x, int sidebar_w, Tab& tab);
    void draw_status(int my, int mx);
    void place_cursor(int my, int mx, Tab& tab);
    std::string prompt(const std::string& msg);
    void compile_run(Tab& tab);
    void update_sidebar();
    void find_match(Tab& tab);
    void find_all(Tab& tab, const std::string& q);
    std::regex build_regex(const std::string& q) const;
    void plain_search(const std::string& line, int line_idx, const std::string& q);
    void search_next(Tab& tab);
    void search_prev(Tab& tab);
    void clear_search(Tab& tab);
    void new_tab(const std::string& fname = "");
    void close_tab(int idx);
    void switch_tab(int idx);
    void fuzzy_finder();

    void set_status(const std::string& msg);
    void clamp_cursor(Tab& tab);
    static bool is_untitled(const Buffer& buffer);

    int current_tab;
    std::vector<std::unique_ptr<Tab>> tabs;

    bool running, show_sidebar, focus_sidebar, in_search_mode, search_regex;
    bool search_case_sensitive = true, search_whole_word = false;
    std::string current_dir, search_query, status_msg;
    std::vector<fs::path> sidebar_paths;
    int sidebar_sel, sidebar_scroll;
    std::vector<SearchHit> search_results;
    int search_idx;

    struct { int x, y; bool active; } match_pos;

    Settings settings;
    std::chrono::steady_clock::time_point msg_time, last_save_time;

    static constexpr int SIDEBAR_WIDTH = 22;
    static constexpr int SIDEBAR_LABEL_W = 18;
    static constexpr int SIDEBAR_LABEL_MAX = SIDEBAR_LABEL_W - 2;
    static constexpr int PROMPT_SIZE = 256;
    static constexpr int STATUS_TIMEOUT = 3;

    static constexpr int HELP_H = 17;
    static constexpr int HELP_W = 56;
    static constexpr int HELP_X = 2;
    static constexpr int HELP_COL_W = 24;
    static constexpr int HELP_DIV_R = 2;
    static constexpr int HELP_DIV_X = HELP_X + HELP_COL_W + 1;
    static constexpr int HELP_FOOTER_R = HELP_H - 2;
    static constexpr int HELP_DIV_H = HELP_FOOTER_R - 3;
};
