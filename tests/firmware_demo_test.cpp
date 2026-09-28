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
        "    #1-#43: 43 events, LED pins driven low, SysTick every 1,000,000 cycles\n"
        "\n"
        "run for 25 ms (2,500,000 cycles)\n"
        "    #44   t=999999     irq     SysTick   pend\n"
        "    #45   t=999999     irq     SysTick   enter\n"
        "    #46   t=999999     write32 FIO1CLR   0x10000000\n"
        "    #47   t=999999     write32 FIO1SET   0x20000000\n"
        "    #48   t=999999     led     LED1      ON\n"
        "    #49   t=999999     write32 FIO1CLR   0x80000000\n"
        "    #50   t=999999     write32 FIO2CLR   0x00000004\n"
        "    #51   t=999999     write32 FIO2CLR   0x00000008\n"
        "    #52   t=999999     write32 FIO2CLR   0x00000010\n"
        "    #53   t=999999     write32 FIO2CLR   0x00000020\n"
        "    #54   t=999999     write32 FIO2CLR   0x00000040\n"
        "    #55   t=999999     read32  ADCR      0x00200404\n"
        "    #56   t=999999     write32 ADCR      0x00200404\n"
        "    #57   t=999999     read32  ADCR      0x00200404\n"
        "    #58   t=999999     write32 ADCR      0x01200404\n"
        "    #59   t=999999     irq     SysTick   exit\n"
        "    #60   t=1001299    adc     AD0.2     0x000\n"
        "    #61   t=1001299    irq     ADC       pend\n"
        "    #62   t=1001299    irq     ADC       enter\n"
        "    #63   t=1001299    read32  ADSTAT    0x00010004\n"
        "    #64   t=1001299    read32  ADGDR     0x82000000\n"
        "    #65   t=1001299    irq     ADC       exit\n"
        "    #66   t=1999999    irq     SysTick   pend\n"
        "    #67   t=1999999    irq     SysTick   enter\n"
        "    #68   t=1999999    write32 FIO1CLR   0x10000000\n"
        "    #69   t=1999999    write32 FIO1CLR   0x20000000\n"
        "    #70   t=1999999    led     LED1      OFF\n"
        "    #71   t=1999999    write32 FIO1SET   0x80000000\n"
        "    #72   t=1999999    led     LED2      ON\n"
        "    #73   t=1999999    write32 FIO2CLR   0x00000004\n"
        "    #74   t=1999999    write32 FIO2CLR   0x00000008\n"
        "    #75   t=1999999    write32 FIO2CLR   0x00000010\n"
        "    #76   t=1999999    write32 FIO2CLR   0x00000020\n"
        "    #77   t=1999999    write32 FIO2CLR   0x00000040\n"
        "    #78   t=1999999    read32  ADCR      0x01200404\n"
        "    #79   t=1999999    write32 ADCR      0x00200404\n"
        "    #80   t=1999999    read32  ADCR      0x00200404\n"
        "    #81   t=1999999    write32 ADCR      0x01200404\n"
        "    #82   t=1999999    irq     SysTick   exit\n"
        "    #83   t=2001299    adc     AD0.2     0x000\n"
        "    #84   t=2001299    irq     ADC       pend\n"
        "    #85   t=2001299    irq     ADC       enter\n"
        "    #86   t=2001299    read32  ADSTAT    0x00010004\n"
        "    #87   t=2001299    read32  ADGDR     0x82000000\n"
        "    #88   t=2001299    irq     ADC       exit\n"
        "\n"
        "LEDs: 0=OFF 1=OFF 2=ON 3=OFF 4=OFF 5=OFF 6=OFF 7=OFF\n"
        "88 trace events, t=2500000\n");
}

TEST(FirmwareDemo, RepeatedRunsAreIdentical) {
    std::ostringstream first, second;
    latasim::test::reset_irq_statics();
    latasim::cli::run_firmware_demo(first);
    latasim::test::reset_irq_statics();
    latasim::cli::run_firmware_demo(second);
    EXPECT_EQ(first.str(), second.str());
}
