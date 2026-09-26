# MCB1700 pin and peripheral map (Phase 0)

Every row cites where it came from. Nothing here is from memory.

**Sources**

| Tag | Source |
|---|---|
| **[BSP]** | Keil board support, `LPC1700_DFP 2.6.0/Boards/Keil/MCB1700/Common/*_MCB1700.c` (the new `Board_*.h` API) |
| **[DOC]** | MCB1700 User's Guide, `mcb1700.chm` in the same pack (pages *LEDs*, *Joystick*, *Push Buttons*, *Potentiometer*, *Speaker*, *Jumper Descriptions*) |
| **[HDR]** | `LPC1700_DFP 2.6.0/Device/Include/LPC17xx.h` |
| **[LEG]** | Legacy Keil MCB1700 drivers `LED.c`, `KBD.c`/`KBD.h`, `GLCD_SPI_LPC1700.c` (Keil 2008 code, as used by the joystick/LED demo firmware) |
| **[BBT]** | Bit-band test firmware (two variants of the same program) |
| **[SIM]** | Observed in the µVision LPC1768 simulator (`DARMP1.DLL -pLPC1768`) in experiments E1–E6 ([findings](findings.md)) |

The schematic PDF (`mcb1700-schematics.pdf`) could not be rendered on this machine; no PDF tools are installed, and none were installed for this. Nothing below depends on it.

## GPIO register addresses [HDR]

The GPIO block is `LPC_GPIO_BASE = 0x2009C000`, with `LPC_GPIOn_BASE = base + 0x20·n`. The layout is `FIODIR +0x00`, `FIOMASK +0x10`, `FIOPIN +0x14`, `FIOSET +0x18`, `FIOCLR +0x1C`.

| Port | FIODIR | FIOMASK | FIOPIN | FIOSET | FIOCLR | FIOPIN bit-band alias, bit *n* |
|---|---|---|---|---|---|---|
| 0 | 0x2009C000 | 0x2009C010 | 0x2009C014 | 0x2009C018 | 0x2009C01C | 0x23380280 + 4n |
| 1 | 0x2009C020 | 0x2009C030 | 0x2009C034 | 0x2009C038 | 0x2009C03C | 0x23380680 + 4n |
| 2 | 0x2009C040 | 0x2009C050 | 0x2009C054 | 0x2009C058 | 0x2009C05C | 0x23380A80 + 4n |

The GPIO block lies in the SRAM bit-band region (0x20000000–0x200FFFFF). The alias is `0x22000000 + (addr − 0x20000000)·32 + 4·bit`. **[SIM]** µVision applies alias writes to the GPIO pins (E5).

## LEDs

| LED | Pin | Dir | Polarity | Registers | Board API (new) | Legacy API (old) |
|---|---|---|---|---|---|---|
| LED0 | P1.28 | out | see below | FIO1*, bit 28 | `LED_On(0)` | `LED_On(0)` |
| LED1 | P1.29 | out | " | FIO1*, bit 29 | `LED_On(1)` | `LED_On(1)` |
| LED2 | P1.31 | out | " | FIO1*, bit 31 | `LED_On(2)` | `LED_On(2)` |
| LED3 | P2.2 | out | " | FIO2*, bit 2 | `LED_On(3)` | `LED_On(3)` |
| LED4 | P2.3 | out | " | FIO2*, bit 3 | `LED_On(4)` | `LED_On(4)` |
| LED5 | P2.4 | out | " | FIO2*, bit 4 | `LED_On(5)` | `LED_On(5)` |
| LED6 | P2.5 | out | " | FIO2*, bit 5 | `LED_On(6)` | `LED_On(6)` |
| LED7 | P2.6 | out | " | FIO2*, bit 6 | `LED_On(7)` | `LED_On(7)` |

- **Pins [BSP][DOC][LEG] all agree.** The LEDs sit behind driver chip IC9, enabled by the **LED** jumper [DOC].
- **The P2.2–P2.6 LEDs are multiplexed with ETM trace** [DOC]. With `TRACE_PIN_ENABLE` defined, [BSP] exposes only 3 LEDs.
- **New API [BSP]:** `LED_Initialize/Uninitialize/On/Off/SetOut(val)/GetCount` (`Board_LED.h`, MDK-Middleware 7.13.0). Internally it uses `GPIO_PinWrite`, which is `FIOSET`/`FIOCLR`, never `FIOPIN`.
- **Old legacy API [LEG]:** `LED_Init/On/Off/Out(value)`, with `LED_NUM = 8`. `LED_On` does `FIOPIN |= mask` and `LED_Off` does `FIOPIN &= ~mask` (a read-modify-write of FIOPIN).

**⚠ Polarity is disputed:**
- **Active-high (pin 1 = on):** [BSP] `LED_On` writes 1, and [LEG] `LED_On` sets the bit.
- **Active-low:** [BBT] treats the LEDs as active-low (high = off) and drives low for on. The `LED_On(0)` ≡ `FIOCLR = 1<<28` example in the Phase 0 goals follows [BBT].
- **Weight of evidence:** two driver sources say active-high against one comment. The unrendered schematic or the real board must settle it. **The board model must hold polarity as one named, verified constant**, not spread it across code. The simulator cannot answer this: it models pins, not LEDs.

## Joystick (5-way) [BSP][DOC][LEG]

| Direction | Pin | Dir | Polarity | New API bit (`Board_Joystick.h`) | Legacy bit (`KBD.h`, value >> 20) |
|---|---|---|---|---|---|
| Center/select | P1.20 | in | active-low | `JOYSTICK_CENTER` = 1<<2 | `KBD_SELECT` = 0x01 |
| Up | P1.23 | in | active-low | `JOYSTICK_UP` = 1<<3 | `KBD_UP` = 0x08 |
| Right | P1.24 | in | active-low | `JOYSTICK_RIGHT` = 1<<1 | `KBD_RIGHT` = 0x10 |
| Down | P1.25 | in | active-low | `JOYSTICK_DOWN` = 1<<4 | `KBD_DOWN` = 0x20 |
| Left | P1.26 | in | active-low | `JOYSTICK_LEFT` = 1<<0 | `KBD_LEFT` = 0x40 |

- **Active-low is stated** in [DOC] ("connects … to ground"), and both drivers invert the pin.
- **Registers:** `FIO1PIN` bits 20, 23–26. [LEG] also clears `PINSEL3` for those pins and clears their `FIO1DIR` bits.
- **The two API generations encode directions differently.** A B1 shim must implement each one exactly.
- **[SIM]:** the pins are the `PORT1` VTREG bits. Reset value `PORT1 = 0xFFFFC713`, so all joystick pins read 1 (released).

## INT0 push button [BSP][DOC]

| Signal | Pin | Dir | Polarity | Registers | Board API |
|---|---|---|---|---|---|
| INT0 | P2.10 (EINT0 is its alternate function) | in | active-low: "Pushing the button generates a low signal" [DOC] | `FIO2PIN` bit 10 | `Buttons_Initialize/GetState/GetCount`; `GetState` bit 0 = INT0 |

- **Jumpers [DOC]:** the **INT0** jumper connects the button. The **ISP** jumper lets COM0 RTS also pull P2.10 low (ISP entry).
- **Oddity:** [BSP] configures P2.10 with the internal **pull-down** (`PIN_PINMODE_PULLDOWN`) yet reads it active-low. This is recorded as-is, not resolved.
- **[SIM] (E4):** writing `PORT2 &= ~(1<<10)` presses the button, and unmodified Blinky reacts.

## GLCD [BSP][DOC][LEG]

| Signal | Pin | Function |
|---|---|---|
| CS | P0.6 | GPIO output (driven by software, idle high) |
| SCK | P0.7 | SCK1 (SSP1) |
| MISO | P0.8 | MISO1 (SSP1); the panel's SDO |
| MOSI | P0.9 | MOSI1 (SSP1); the panel's SDI |
| RS, RD | GND | on the board [BSP comment] |
| (legacy driver only) | P4.28, P4.29 | set as outputs; P4.29 set high at init start, P4.28 high at init end [LEG]. **Purpose not confirmed from docs** (backlight/reset are plausible, not verified) |

**Panel and protocol:**
- **Panel:** 240×320, 16 bpp RGB565 [BSP `GLCD_Config.h`].
- **SPI framing** (the start byte is `0x70 | RS<<1 | RW`):
  - `0x70` = index (register) write
  - `0x72` = data write
  - `0x73` = data read
  - [BSP][LEG], and seen on the wire in [SIM] E6.
- **Controller** is picked at runtime from an ID read: `0x47` = Himax HX8347-D; other IDs take a second init path [BSP][LEG]. The simulator returns ID 0, so it takes the non-HX8347 path.
- **SSP1 registers:** `LPC_SSP1_BASE = 0x40030000`. [LEG] writes `DR` directly and polls `SR.RNE`, while [BSP] goes through the CMSIS `Driver_SPI1`.
- **[SIM]:** the `SSP1_OUT` VTREG carries each transmitted byte.
- **[DOC] discrepancy:** the LCD page says "connects using a 16-bit interface", but both drivers use SPI. The page probably describes another board revision. The drivers are what the reference firmware actually runs.

**APIs:**
- **New:** `GLCD_Initialize`, `GLCD_ClearScreen`, `GLCD_SetForeground/BackgroundColor`, `GLCD_SetFont`, `GLCD_DrawString(x, y, str)`, `GLCD_DrawChar`, `GLCD_DrawBargraph`, `GLCD_DrawBitmap`, …
- **Old legacy API:** `GLCD_Init`, `GLCD_Clear(color)`, `GLCD_SetTextColor/BackColor`, `GLCD_DisplayString(ln, col, fi, s)`, `GLCD_DisplayChar`, `GLCD_ClearLn`, `GLCD_Bargraph`. Fonts: `fi = 0` is 6×8 and `fi = 1` is 16×24, rasterized in firmware.

## Potentiometer and other analog [BSP][DOC]

| Signal | Pin | Notes |
|---|---|---|
| Potentiometer | P0.25 = AD0.2 (PINSEL function 1) | 0–3.3 V; **AD0.2** jumper. ADC at `0x40034000` (`ADCR +0x00`, `ADGDR +0x04`); needs PCONP bit 12. Board API `ADC_Initialize/StartConversion/ConversionDone/GetValue/GetResolution` (12-bit, IRQ-driven). [SIM] VTREG `AIN2` (listed, not exercised) |
| Speaker | P0.26 = AOUT (DAC) | **SPK** jumper → LM386 amplifier. [SIM] VTREG `AOUT` |

## Simulator VTREGs seen (E1, `DIR VTREG`)

- **Pins:** `PORT0..PORT4`
- **UARTs:** `S0..S3 IN/OUT/TIME/BAUD`
- **CAN:** `CAN1`/`CAN2` registers and bytes
- **SPI/SSP:** `SPI_IN/OUT`, `SSP0_IN/OUT`, `SSP1_IN/OUT`
- **I²C:** `I2C0..2 IN/OUT`
- **Analog:** `AIN0..7`, `AOUT`, `VREF`, `V3`
- **Clocks:** `XTAL`, `IRC`, `RTCX`, `CCLK`, `PLLCLK`, `USBCLK`, …
- **Kernel/trace:** `CURR_TID`, `TRAPS`, `TRIGFLT`, `STCLK`

`CCLK` reads 12 MHz at reset and 100 MHz once `SystemInit` has configured the PLL.
