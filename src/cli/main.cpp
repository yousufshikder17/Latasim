#include <cstdio>
#include <string_view>

namespace {

int usage() {
    std::puts("usage: vwb <command>\n"
              "\n"
              "commands:\n"
              "  help    show this message");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "help") {
        usage();
        return 0;
    }
    return usage();
}
