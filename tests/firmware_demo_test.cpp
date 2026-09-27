#include "cli/firmware_demo.hpp"

#include <gtest/gtest.h>

#include <sstream>

// Keil's board drivers on the modelled board, with the trace. The run is
// deterministic, so the whole output is pinned; update it only on purpose.
TEST(FirmwareDemo, OutputIsExactlyAsExpected) {
    std::ostringstream out;
    latasim::cli::run_firmware_demo(out);
    EXPECT_EQ(out.str(),
        "Latasim MCB1700 firmware demo\n"
        "Keil's LED_MCB1700.c, Joystick_MCB1700.c and Buttons_MCB1700.c (unmodified C)\n"
        "on the modelled board. Indented lines are the hardware trace.\n"
        "\n"
        "LEDs: 0=UNDRIVEN 1=UNDRIVEN 2=UNDRIVEN 3=UNDRIVEN 4=UNDRIVEN 5=UNDRIVEN 6=UNDRIVEN 7=UNDRIVEN\n"
        "\n"
        "firmware: LED_Initialize(); Joystick_Initialize(); Buttons_Initialize();\n"
        "    #1-#50: 50 events, PCONP, pin directions, LED pins driven low\n"
        "LEDs: 0=OFF 1=OFF 2=OFF 3=OFF 4=OFF 5=OFF 6=OFF 7=OFF\n"
        "\n"
        "firmware: LED_On(0)\n"
        "    #51   write32 FIO1SET   0x10000000\n"
        "    #52   led     LED0      ON\n"
        "\n"
        "board:    joystick UP pressed\n"
        "    #53   input   P1.23     low\n"
        "firmware: Joystick_GetState() = 0x08 (JOYSTICK_UP)\n"
        "    #54   read32  FIO1PIN   0x5F7FC713\n"
        "    #55   read32  FIO1PIN   0x5F7FC713\n"
        "    #56   read32  FIO1PIN   0x5F7FC713\n"
        "    #57   read32  FIO1PIN   0x5F7FC713\n"
        "    #58   read32  FIO1PIN   0x5F7FC713\n"
        "\n"
        "board:    INT0 pressed\n"
        "    #59   input   P2.10     low\n"
        "firmware: Buttons_GetState() = 0x01 (INT0)\n"
        "    #60   read32  FIO2PIN   0x00003B83\n"
        "firmware: LED_SetOut(joystick | buttons)  (LED3: UP, LED0: INT0)\n"
        "    #61   write32 FIO1SET   0x10000000\n"
        "    #62   write32 FIO1CLR   0x20000000\n"
        "    #63   write32 FIO1CLR   0x80000000\n"
        "    #64   write32 FIO2SET   0x00000004\n"
        "    #65   led     LED3      ON\n"
        "    #66   write32 FIO2CLR   0x00000008\n"
        "    #67   write32 FIO2CLR   0x00000010\n"
        "    #68   write32 FIO2CLR   0x00000020\n"
        "    #69   write32 FIO2CLR   0x00000040\n"
        "\n"
        "board:    joystick and INT0 released\n"
        "    #70   input   P1.23     high\n"
        "    #71   input   P2.10     high\n"
        "\n"
        "LEDs: 0=ON 1=OFF 2=OFF 3=ON 4=OFF 5=OFF 6=OFF 7=OFF\n"
        "71 trace events; the same on every run.\n");
}

TEST(FirmwareDemo, RepeatedRunsAreIdentical) {
    std::ostringstream first, second;
    latasim::cli::run_firmware_demo(first);
    latasim::cli::run_firmware_demo(second);
    EXPECT_EQ(first.str(), second.str());
}
