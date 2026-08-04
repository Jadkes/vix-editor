#include "editor.hpp"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc > 1) {
        if (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "-v") == 0) {
            std::printf("%s %s\n", VIX_NAME, VIX_VERSION);
            return 0;
        }
        if (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
            std::printf("usage: %s [file]\n\n"
                        "  -v, --version  print version and exit\n"
                        "  -h, --help     show this help and exit\n",
                        argv[0]);
            return 0;
        }
    }
    Editor editor(argc, argv);
    editor.run();
    return 0;
}