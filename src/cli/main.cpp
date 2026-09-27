#include <cstdio>
#include <iostream>
#include <string_view>

#include "cli/gpio_demo.hpp"

namespace {

int usage() {
    std::puts("usage: vwb <command>\n"
              "\n"
              "commands:\n"
              "  gpio-demo   drive MCB1700 LEDs through registers and the board API\n"
              "  help        show this message");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return usage();
    const std::string_view command = argv[1];
    if (command == "gpio-demo") {
        latasim::cli::run_gpio_demo(std::cout);
        return 0;
    }
    if (command == "help") {
        usage();
        return 0;
    }
    return usage();
}
