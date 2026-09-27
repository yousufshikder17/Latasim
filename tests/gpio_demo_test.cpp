#include "cli/gpio_demo.hpp"

#include <gtest/gtest.h>

#include <sstream>

// The demo is deterministic, so its whole output is pinned. A change here means
// the model's behaviour changed; update this text only on purpose.
TEST(GpioDemo, OutputIsExactlyAsExpected) {
    std::ostringstream out;
    latasim::cli::run_gpio_demo(out);
    EXPECT_EQ(out.str(),
        "MCB1700 GPIO demo: LED0 = P1.28, LED3 = P2.2, active-high\n"
        "\n"
        "Direct register access (firmware-style stores)\n"
        "  reset                                        LED0 UNDRIVEN  FIO1PIN=FFFFC713 FIO2PIN=00003FFF\n"
        "  FIO1DIR 2009C020 <- 10000000                 LED0 OFF       FIO1PIN=EFFFC713 FIO2PIN=00003FFF\n"
        "  FIO1SET 2009C038 <- 10000000                 LED0 ON        FIO1PIN=FFFFC713 FIO2PIN=00003FFF\n"
        "  FIO1CLR 2009C03C <- 10000000                 LED0 OFF       FIO1PIN=EFFFC713 FIO2PIN=00003FFF\n"
        "  alias   233806F0 <- 00000001 P1.28           LED0 ON        FIO1PIN=FFFFC713 FIO2PIN=00003FFF\n"
        "  alias   233806EC <- 00000000 P1.27!          LED0 ON        FIO1PIN=FFFFC713 FIO2PIN=00003FFF\n"
        "  alias   233806F0 <- 00000000 P1.28           LED0 OFF       FIO1PIN=EFFFC713 FIO2PIN=00003FFF\n"
        "  (P1.27! is the wrong-pin alias from Phase 0: LED0 is unaffected)\n"
        "\n"
        "Keil board API (LED_*), same register model\n"
        "  LED_Initialize()                             LED0 OFF       FIO1PIN=4FFFC713 FIO2PIN=00003F83\n"
        "  LED_On(0)                                    LED0 ON        FIO1PIN=5FFFC713 FIO2PIN=00003F83\n"
        "  LED_On(3)                                    LED3 ON        FIO1PIN=5FFFC713 FIO2PIN=00003F87\n"
        "  LED_Off(0)                                   LED0 OFF       FIO1PIN=4FFFC713 FIO2PIN=00003F87\n"
        "  FIO2CLR <- 00000004 (register store)         LED3 OFF       FIO1PIN=4FFFC713 FIO2PIN=00003F83\n"
        "\n"
        "LEDs: 0=OFF 1=OFF 2=OFF 3=OFF 4=OFF 5=OFF 6=OFF 7=OFF\n");
}

TEST(GpioDemo, RepeatedRunsAreIdentical) {
    std::ostringstream first, second;
    latasim::cli::run_gpio_demo(first);
    latasim::cli::run_gpio_demo(second);
    EXPECT_EQ(first.str(), second.str());
}
