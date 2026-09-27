#include <cstdio>
#include <iostream>
#include <string_view>

#include "cli/gpio_demo.hpp"
#ifdef LATASIM_HAVE_KEIL_BOARD_DRIVERS
#include "cli/firmware_demo.hpp"
#endif

namespace {

int usage() {
    std::puts("usage: latasim <command>\n"
              "\n"
              "commands:\n"
              "  gpio-demo       drive MCB1700 LEDs through registers and the board API\n"
              "  firmware-demo   run Keil's MCB1700 board drivers with a hardware trace\n"
              "  help            show this message");
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
    if (command == "firmware-demo") {
#ifdef LATASIM_HAVE_KEIL_BOARD_DRIVERS
        latasim::cli::run_firmware_demo(std::cout);
        return 0;
#else
        std::fputs("firmware-demo needs Keil's MCB1700 board drivers, but this build was configured\n"
                   "without the Keil packs (see LATASIM_KEIL_PACKS_DIR).\n",
                   stderr);
        return 1;
#endif
    }
    if (command == "help") {
        usage();
        return 0;
    }
    return usage();
}
