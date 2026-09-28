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
        "    #1-#190: 190 events, PCONP, pin functions and directions, LED pins driven low\n"
        "LEDs: 0=OFF 1=OFF 2=OFF 3=OFF 4=OFF 5=OFF 6=OFF 7=OFF\n"
        "\n"
        "firmware: LED_On(0)\n"
        "    #191  t=0          write32 FIO1SET   0x10000000\n"
        "    #192  t=0          led     LED0      ON\n"
        "\n"
        "board:    joystick UP pressed\n"
        "    #193  t=0          input   P1.23     low\n"
        "firmware: Joystick_GetState() = 0x08 (JOYSTICK_UP)\n"
        "    #194  t=0          read32  FIO1PIN   0x5F7FC713\n"
        "    #195  t=0          read32  FIO1PIN   0x5F7FC713\n"
        "    #196  t=0          read32  FIO1PIN   0x5F7FC713\n"
        "    #197  t=0          read32  FIO1PIN   0x5F7FC713\n"
        "    #198  t=0          read32  FIO1PIN   0x5F7FC713\n"
        "\n"
        "board:    INT0 pressed\n"
        "    #199  t=0          input   P2.10     low\n"
        "firmware: Buttons_GetState() = 0x01 (INT0)\n"
        "    #200  t=0          read32  FIO2PIN   0x00003B83\n"
        "firmware: LED_SetOut(joystick | buttons)  (LED3: UP, LED0: INT0)\n"
        "    #201  t=0          write32 FIO1SET   0x10000000\n"
        "    #202  t=0          write32 FIO1CLR   0x20000000\n"
        "    #203  t=0          write32 FIO1CLR   0x80000000\n"
        "    #204  t=0          write32 FIO2SET   0x00000004\n"
        "    #205  t=0          led     LED3      ON\n"
        "    #206  t=0          write32 FIO2CLR   0x00000008\n"
        "    #207  t=0          write32 FIO2CLR   0x00000010\n"
        "    #208  t=0          write32 FIO2CLR   0x00000020\n"
        "    #209  t=0          write32 FIO2CLR   0x00000040\n"
        "\n"
        "board:    joystick and INT0 released\n"
        "    #210  t=0          input   P1.23     high\n"
        "    #211  t=0          input   P2.10     high\n"
        "\n"
        "LEDs: 0=ON 1=OFF 2=OFF 3=ON 4=OFF 5=OFF 6=OFF 7=OFF\n"
        "211 trace events\n"
        "\n"
        "Part 2: timed firmware, on a new board\n"
        "Keil's Blinky_ULp: SysTick_Handler in IRQ.c (unmodified C) steps an LED chase\n"
        "every 10 ms of virtual time.\n"
        "\n"
        "firmware: LED_Initialize(); ...; SysTick_Config(SystemCoreClock / 100);\n"
        "    #1-#133: 133 events, pin functions, LED pins driven low, ADC, SysTick every 1,000,000 cycles\n"
        "\n"
        "run for 25 ms (2,500,000 cycles)\n"
        "    #134  t=999999     irq     SysTick   pend\n"
        "    #135  t=999999     irq     SysTick   enter\n"
        "    #136  t=999999     write32 FIO1CLR   0x10000000\n"
        "    #137  t=999999     write32 FIO1SET   0x20000000\n"
        "    #138  t=999999     led     LED1      ON\n"
        "    #139  t=999999     write32 FIO1CLR   0x80000000\n"
        "    #140  t=999999     write32 FIO2CLR   0x00000004\n"
        "    #141  t=999999     write32 FIO2CLR   0x00000008\n"
        "    #142  t=999999     write32 FIO2CLR   0x00000010\n"
        "    #143  t=999999     write32 FIO2CLR   0x00000020\n"
        "    #144  t=999999     write32 FIO2CLR   0x00000040\n"
        "    #145  t=999999     read32  ADCR      0x00200404\n"
        "    #146  t=999999     write32 ADCR      0x00200404\n"
        "    #147  t=999999     read32  ADCR      0x00200404\n"
        "    #148  t=999999     write32 ADCR      0x01200404\n"
        "    #149  t=999999     irq     SysTick   exit\n"
        "    #150  t=1001299    adc     AD0.2     0x000\n"
        "    #151  t=1001299    irq     ADC       pend\n"
        "    #152  t=1001299    irq     ADC       enter\n"
        "    #153  t=1001299    read32  ADSTAT    0x00010004\n"
        "    #154  t=1001299    read32  ADGDR     0x82000000\n"
        "    #155  t=1001299    irq     ADC       exit\n"
        "    #156  t=1999999    irq     SysTick   pend\n"
        "    #157  t=1999999    irq     SysTick   enter\n"
        "    #158  t=1999999    write32 FIO1CLR   0x10000000\n"
        "    #159  t=1999999    write32 FIO1CLR   0x20000000\n"
        "    #160  t=1999999    led     LED1      OFF\n"
        "    #161  t=1999999    write32 FIO1SET   0x80000000\n"
        "    #162  t=1999999    led     LED2      ON\n"
        "    #163  t=1999999    write32 FIO2CLR   0x00000004\n"
        "    #164  t=1999999    write32 FIO2CLR   0x00000008\n"
        "    #165  t=1999999    write32 FIO2CLR   0x00000010\n"
        "    #166  t=1999999    write32 FIO2CLR   0x00000020\n"
        "    #167  t=1999999    write32 FIO2CLR   0x00000040\n"
        "    #168  t=1999999    read32  ADCR      0x01200404\n"
        "    #169  t=1999999    write32 ADCR      0x00200404\n"
        "    #170  t=1999999    read32  ADCR      0x00200404\n"
        "    #171  t=1999999    write32 ADCR      0x01200404\n"
        "    #172  t=1999999    irq     SysTick   exit\n"
        "    #173  t=2001299    adc     AD0.2     0x000\n"
        "    #174  t=2001299    irq     ADC       pend\n"
        "    #175  t=2001299    irq     ADC       enter\n"
        "    #176  t=2001299    read32  ADSTAT    0x00010004\n"
        "    #177  t=2001299    read32  ADGDR     0x82000000\n"
        "    #178  t=2001299    irq     ADC       exit\n"
        "\n"
        "LEDs: 0=OFF 1=OFF 2=ON 3=OFF 4=OFF 5=OFF 6=OFF 7=OFF\n"
        "178 trace events, t=2500000\n");
}

TEST(FirmwareDemo, RepeatedRunsAreIdentical) {
    std::ostringstream first, second;
    latasim::test::reset_irq_statics();
    latasim::cli::run_firmware_demo(first);
    latasim::test::reset_irq_statics();
    latasim::cli::run_firmware_demo(second);
    EXPECT_EQ(first.str(), second.str());
}
