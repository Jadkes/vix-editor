// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * main.cpp - entry point: arg parsing, terminal-restore signal guards
 *
 * On SIGTERM/SIGHUP/SIGINT the ncurses teardown in Editor::run() never
 * executes, which used to leave the terminal in raw/no-echo mode. The
 * handler below restores the termios captured before the editor starts
 * (async-signal-safe calls only) and exits with 128+signum, matching
 * shell convention.
 */
#include "editor.hpp"
#include <cstdio>
#include <cstring>
#include <csignal>
#include <termios.h>
#include <unistd.h>

static struct termios saved_termios;
static volatile sig_atomic_t termios_saved = 0;

static void restore_terminal_and_die(int sig) {
    if (termios_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
    // Full terminal reset (RIS): heavy-handed but guarantees a sane screen
    // no matter what state ncurses left behind.
    const char reset[] = "\033c\033[?25h";
    ssize_t ignored = write(STDOUT_FILENO, reset, sizeof(reset) - 1);
    (void)ignored;
    _exit(128 + sig);
}

int main(int argc, char** argv) {
    if (argc > 1) {
        if (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "-v") == 0) {
            std::printf("%s %s\n", VIX_NAME, VIX_VERSION);
            return 0;
        }
        if (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
            std::printf("usage: %s [file]\n\n"
                        "  -r, --resume    reopen the files from the last session\n"
                        "  -v, --version   print version and exit\n"
                        "  -h, --help      show this help and exit\n",
                        argv[0]);
            return 0;
        }
    }
    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &saved_termios) == 0) termios_saved = 1;
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = restore_terminal_and_die;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGHUP, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
    }
    Editor editor(argc, argv);
    editor.run();
    return 0;
}
