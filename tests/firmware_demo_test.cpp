#include "cli/firmware_demo.hpp"

#include "blinky_ulp_fixture.hpp"

#include <gtest/gtest.h>

#include <sstream>

// Keil's board drivers and timed Blinky_ULp on modelled boards, with the trace.
// The run is deterministic, so the whole output is pinned; update it only on
// purpose. As in a new `latasim` process, IRQ.c's statics start from their
// initial values (blinky_ulp_fixture.hpp).
TEST(FirmwareDemo, OutputIsExactlyAsExpected) {
    latasim::test::reset_irq_statics();
    std::ostringstream out;
    latasim::cli::run_firmware_demo(out);
    EXPECT_EQ(out.str(),
        "Latasim MCB1700 firmware demo\n"
        "Keil's MCB1700 sources on modelled boards. Indented lines are the hardware trace;\n"
        "t is virtual time in core clock cycles (100 MHz). Every run is identical.\n"
        "\n"
        "Part 1: board drivers\n"
        "Keil's LED_MCB1700.c, Joystick_MCB1700.c and Buttons_MCB1700.c (unmodified C).\n"
        "\n"
        "LEDs: 0=UNDRIVEN 1=UNDRIVEN 2=UNDRIVEN 3=UNDRIVEN 4=UNDRIVEN 5=UNDRIVEN 6=UNDRIVEN 7=UNDRIVEN\n"
        "\n"
        "firmware: LED_Initialize(); Joystick_Initialize(); Buttons_Initialize();\n"
        "    #1-#50: 50 events, PCONP, pin directions, LED pins driven low\n"
        "LEDs: 0=OFF 1=OFF 2=OFF 3=OFF 4=OFF 5=OFF 6=OFF 7=OFF\n"
        "\n"
        "firmware: LED_On(0)\n"
        "    #51   t=0          write32 FIO1SET   0x10000000\n"
        "    #52   t=0          led     LED0      ON\n"
        "\n"
        "board:    joystick UP pressed\n"
        "    #53   t=0          input   P1.23     low\n"
        "firmware: Joystick_GetState() = 0x08 (JOYSTICK_UP)\n"
        "    #54   t=0          read32  FIO1PIN   0x5F7FC713\n"
        "    #55   t=0          read32  FIO1PIN   0x5F7FC713\n"
        "    #56   t=0          read32  FIO1PIN   0x5F7FC713\n"
        "    #57   t=0          read32  FIO1PIN   0x5F7FC713\n"
        "    #58   t=0          read32  FIO1PIN   0x5F7FC713\n"
        "\n"
        "board:    INT0 pressed\n"
        "    #59   t=0          input   P2.10     low\n"
        "firmware: Buttons_GetState() = 0x01 (INT0)\n"
        "    #60   t=0          read32  FIO2PIN   0x00003B83\n"
        "firmware: LED_SetOut(joystick | buttons)  (LED3: UP, LED0: INT0)\n"
        "    #61   t=0          write32 FIO1SET   0x10000000\n"
        "    #62   t=0          write32 FIO1CLR   0x20000000\n"
        "    #63   t=0          write32 FIO1CLR   0x80000000\n"
        "    #64   t=0          write32 FIO2SET   0x00000004\n"
        "    #65   t=0          led     LED3      ON\n"
        "    #66   t=0          write32 FIO2CLR   0x00000008\n"
        "    #67   t=0          write32 FIO2CLR   0x00000010\n"
        "    #68   t=0          write32 FIO2CLR   0x00000020\n"
        "    #69   t=0          write32 FIO2CLR   0x00000040\n"
        "\n"
        "board:    joystick and INT0 released\n"
        "    #70   t=0          input   P1.23     high\n"
        "    #71   t=0          input   P2.10     high\n"
        "\n"
        "LEDs: 0=ON 1=OFF 2=OFF 3=ON 4=OFF 5=OFF 6=OFF 7=OFF\n"
        "71 trace events\n"
        "\n"
        "Part 2: timed firmware, on a new board\n"
        "Keil's Blinky_ULp: SysTick_Handler in IRQ.c (unmodified C) steps an LED chase\n"
        "every 10 ms of virtual time.\n"
        "\n"
        "firmware: LED_Initialize(); ...; SysTick_Config(SystemCoreClock / 100);\n"
        "    #1-#38: 38 events, LED pins driven low, SysTick every 1,000,000 cycles\n"
        "\n"
        "run for 25 ms (2,500,000 cycles)\n"
        "    #39   t=999999     irq     SysTick   pend\n"
        "    #40   t=999999     irq     SysTick   enter\n"
        "    #41   t=999999     write32 FIO1CLR   0x10000000\n"
        "    #42   t=999999     write32 FIO1SET   0x20000000\n"
        "    #43   t=999999     led     LED1      ON\n"
        "    #44   t=999999     write32 FIO1CLR   0x80000000\n"
        "    #45   t=999999     write32 FIO2CLR   0x00000004\n"
        "    #46   t=999999     write32 FIO2CLR   0x00000008\n"
        "    #47   t=999999     write32 FIO2CLR   0x00000010\n"
        "    #48   t=999999     write32 FIO2CLR   0x00000020\n"
        "    #49   t=999999     write32 FIO2CLR   0x00000040\n"
        "    #50   t=999999     irq     SysTick   exit\n"
        "    #51   t=1999999    irq     SysTick   pend\n"
        "    #52   t=1999999    irq     SysTick   enter\n"
        "    #53   t=1999999    write32 FIO1CLR   0x10000000\n"
        "    #54   t=1999999    write32 FIO1CLR   0x20000000\n"
        "    #55   t=1999999    led     LED1      OFF\n"
        "    #56   t=1999999    write32 FIO1SET   0x80000000\n"
        "    #57   t=1999999    led     LED2      ON\n"
        "    #58   t=1999999    write32 FIO2CLR   0x00000004\n"
        "    #59   t=1999999    write32 FIO2CLR   0x00000008\n"
        "    #60   t=1999999    write32 FIO2CLR   0x00000010\n"
        "    #61   t=1999999    write32 FIO2CLR   0x00000020\n"
        "    #62   t=1999999    write32 FIO2CLR   0x00000040\n"
        "    #63   t=1999999    irq     SysTick   exit\n"
        "\n"
        "LEDs: 0=OFF 1=OFF 2=ON 3=OFF 4=OFF 5=OFF 6=OFF 7=OFF\n"
        "63 trace events, t=2500000\n");
}

TEST(FirmwareDemo, RepeatedRunsAreIdentical) {
    std::ostringstream first, second;
    latasim::test::reset_irq_statics();
    latasim::cli::run_firmware_demo(first);
    latasim::test::reset_irq_statics();
    latasim::cli::run_firmware_demo(second);
    EXPECT_EQ(first.str(), second.str());
}
