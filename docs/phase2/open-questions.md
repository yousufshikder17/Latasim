# Phase 2: hardware-behaviour questions reviewed

Before Phase 2 closed, four questions were still open (listed in [mmio-and-inputs.md](mmio-and-inputs.md) and [host-registers.md](host-registers.md)). This review re-examined each against the vendor documents installed with the device pack, and ran one new simulator experiment (E9).

| # | Question | Outcome | Model changed? |
|---|---|---|---|
| 1 | CPU `STRB`/`STRH` to Fast GPIO | **BOUNDED**: the manual and the simulator disagree for FIOPIN | no |
| 2 | INT0 pull resistor | **RESOLVED** for the board wiring; the pull-down that Keil's driver enables is **BOUNDED** | no |
| 3 | Model PCONP now? | **RESOLVED**: minimal PCONP storage added | yes |
| 4 | LED polarity | **RESOLVED**: active-high on all eight | no |

**Sources** (all under `%LOCALAPPDATA%/Arm/Packs/Keil/LPC1700_DFP/2.6.0`):

| Short name | Document |
|---|---|
| UM10360 | `Documents/LPC17xx_UM.pdf`: LPC17xx user manual, Rev. 2, 19 August 2010 |
| Data sheet | `Documents/LPC1769_68_67_66_65_64_63.pdf`: LPC1769/68/… data sheet, Rev. 9.0, 10 August 2012 |
| Schematic | `Boards/Keil/MCB1700/Documentation/mcb1700-schematics.pdf`: page 3, sheet `USB_COM_LED.SchDoc`, revision 1.2 |
| SVD, device header | `SVD/LPC176x5x.svd`, `Device/Include/LPC17xx.h` |
| Keil sources | `RTE_Driver/GPIO_LPC17xx.c`, `PIN_LPC17xx.h`, `Boards/Keil/MCB1700/Common/*_MCB1700.c`, `system_LPC17xx.c` |

**E9** (`spikes/uvsim-script/e9-*`) is real firmware, compiled by ARMCC 5.06 and run from reset in the µVision LPC1768 simulator.
- `fromelf -c` confirms that each narrow access is a single `STRB`, `STRH`, `LDRB` or `LDRH` instruction.
- It avoids what failed in E8: there, the debugger set the PC and single-stepped hand-assembled code, which did not execute.
- Two runs gave identical output.

## 1. `STRB`/`STRH` to LPC1768 Fast GPIO: BOUNDED

**Previously:** narrow accesses were modelled lane-only, as a documented decision. E8 showed that the debugger's own access functions can't answer the question, and it could not get the simulated CPU to execute a narrow store.

**What each kind of evidence says.** These are kept apart deliberately; none is inferred from another.

- **Header/API representation** (`LPC17xx.h`, and Latasim's host header). Every GPIO register is a union of a 32-bit word, two 16-bit halves (`FIOxxxL/H`) and four bytes (`FIOxxx0`–`3`).
  - This shows the vendor intends byte and halfword access.
  - It says nothing about what the hardware does with the other lanes.
- **Cortex-M3 architecture.** `STRB`/`STRH`/`LDRB`/`LDRH` are ordinary byte and halfword transfers to any address. Whether a peripheral honours the transfer size is the peripheral's business, not the core's.
- **Documented peripheral behaviour** (UM10360 chapter 9):
  - §9.2.1: "All GPIO registers are byte, half-word, and word addressable."
  - Tables 104, 106, 108, 110 and 112 list each byte and half-word register with its own address and reset value. For example, `FIOxPIN1` is "Fast GPIO Port x Pin value register 1. Bit 0 in FIOxPIN1 register corresponds to pin Px.8 ... bit 7 to pin Px.15" (Table 110).
  - Each section says these registers provide "the same functions as the FIOxPIN [DIR, SET, CLR, MASK] register" and "allow easier and faster access to the physical port pins".
  - Read literally, a byte register controls only its eight pins. The manual never says what happens to the other 24.
- **Simulator behaviour** (E9, CPU-executed):

  | Step | Access | Latch after (FIOSET read) | Reading |
  |---|---|---|---|
  | CP1 | `STRB FIO1PIN0 = A5`; byte 3 input, reading high | `FF0000A5` | **word read-modify-write**: byte 3's input levels land in the latch |
  | CP2 | `STRH FIO1PINL = 5AA5` | `FF005AA5` | same |
  | CP3 | `STRB FIO1SET1 = 01` | `00000100` | lane-local, no byte replication |
  | CP4 | `STRH FIO1SETH = 0001` | `00010100` | lane-local |
  | CP5 | `STRB FIO1CLR2 = 01` | `FFFEFFFF` | lane-local |
  | CP6 | `STRB FIO1DIR3 = 00` | DIR `00FFFFFF` | lane-local |
  | CP7 | `STRB FIO1MASK0 = 0F`, then `STRB FIO1PIN0 = FF` | `000000F0` | masked bits unchanged (both rules agree) |
  | CP8 | `LDRB FIO1PIN3`, `LDRH FIO1PINH` with PIN `12345678` | `12`, `1234` | lane-correct loads |

**Conclusion.**
- **SET, CLR, DIR, MASK and all narrow loads:** the manual, the simulator and the model agree: lane-local.
- **Narrow FIOPIN stores:** they disagree in one corner.
  - The simulator treats them as a read-modify-write of the whole word, so the latch bits of **input** pins in the other lanes take those pins' levels.
  - The model changes only the addressed lanes.
  - The difference cannot be seen on output pins, whose level equals their latch. It becomes visible only if such an input pin is later switched to output.
- **The simulator is Keil's behavioural model of the chip, not silicon.** It matches its own debugger `_WBYTE` behaviour (E8), which points to a whole-word implementation inside the simulator. The manual describes independent byte registers.
- **Latasim keeps lane-only semantics** and does not claim them as verified:

  > Latasim implements the byte/halfword register views exposed by the LPC17xx device header and described in UM10360 chapter 9 as lane-local accesses. This interpretation is consistent with the vendor register interface. It has not been verified with CPU-executed `STRB`/`STRH` on physical hardware, and the µVision LPC1768 simulator behaves differently for narrow FIOPIN stores (E9: word read-modify-write).

**Impact on the model:** none. Narrow FIOPIN stores now have a recorded simulator divergence. A B3 run of firmware that relies on it would show it.

**Future verification:** on a real MCB1700, the E9 firmware as-is: `STRB FIO1PIN0` with P1.24–31 as inputs held high, then switch them to outputs and read FIOSET.

## 2. INT0 pull behaviour: RESOLVED (wiring); Keil's pull-down BOUNDED

**Previously:** the model reads released as high and pressed as low. Keil's `Buttons_Initialize` enables the internal pull-down, which looked inconsistent.

**1. Physical board wiring** (schematic p. 3):
- P2.10 connects through the **INT0 jumper** to a node with:
  - **R27, a 22 kΩ pull-up to +3.3 V**;
  - C48, 100 nF to GND;
  - the INT0 push button, which shorts the node to **GND**.
- So released is pulled high externally and pressed is 0 V. The button is active-low by wiring.
- The same P2.10 net also reaches the **ISP jumper**, which lets the USB-COM circuit (T3) pull it low for ISP entry. That path is not part of the button.

**2. MCU internal pull configuration:**
- `Buttons_Initialize` calls `PIN_Configure(2, 10, PIN_FUNC_0, PIN_PINMODE_PULLDOWN, PIN_PINMODE_NORMAL)`. `PIN_PINMODE_PULLDOWN` is 3, and PINMODE `11` is "on-chip pull-down resistor enabled" (UM10360). So it really is the pull-down; the code was read correctly.
- At reset the pin has its pull-up instead (UM10360 §9.2.1: "All I/Os default to input with pullup after reset").

**Electrically, with the pull-down on:**
- The data sheet gives a pull-down current of 10 / 50 / 150 µA (min / typ / max, at VI = 5 V). Its Fig. 14 shows about 50–70 µA near 2 V (typical, −40 to 85 °C).
- Against 22 kΩ from 3.3 V, a released pin settles at roughly **1.9–2.2 V**.
- That lies between VIL max (0.3 VDD = 0.99 V) and VIH min (0.7 VDD = 2.31 V), so **the data sheet does not guarantee a logic level**. It will often read high, since CMOS input thresholds typically sit near mid-supply, but that is not specified behaviour.
- With the reset pull-up, or with no internal pull, released is a solid high. Pressed is 0 V in every case.
- Nothing on the board needs the pull-down. It looks like an oversight in Keil's driver, though that is an inference.

**3. Simulator default state:** P2.10 reads high at reset, and the E4 input injection drives it low.

**4. Latasim:**
- Released is high and pressed is low. PINMODE is not modelled, so the pull-down that Keil's driver enables has no effect.
- This matches the wiring and the reset pull-up, and it matches the likely, though not guaranteed, reading with the pull-down on.

**Impact on the model:** none; the abstraction is correct for the board. The earlier rationale ("assumes an external pull-up dominates") is now sourced: R27, 22 kΩ. It is also qualified: with Keil's pull-down on, a released pin sits outside the guaranteed input levels.

**Future verification:** measure P2.10 on a board with the button released after `Buttons_Initialize`, and read FIO2PIN bit 10.

## 3. PCONP: RESOLVED, minimal model added (option C)

**Previously:**
- `GPIO_PortClock` was a no-op on the C GPIO layer.
- On the register path, `LPC_SC->PCONP` faulted, so Keil's unmodified `GPIO_LPC17xx.c` aborted in `GPIO_PortClock` (a documented divergence).

**Evidence:**
- **Address:** `0x400FC0C4` (UM10360 Table 46, SVD).
- **Reset value:** the sources conflict.
  - UM10360 Table 14 (register summary) and the SVD say `0x03BE`. That value sets reserved bit 5, clears PWM1 (bit 6), and lacks PCGPIO.
  - UM10360 Table 46's per-bit reset values sum to **`0x042887DE`**, with PCGPIO = 1.
  - The simulator reads `0x042887DE` at reset, before any code runs (E9 RESET).
  - Keil's `system_LPC17xx.c` writes `PCONP_Val = 0x042887DE` in `SystemInit`.
  - The per-bit table, simulator and startup code agree; the summary value looks stale. **`0x042887DE`** is used.
- **Bit 15 (PCGPIO):** "Power/clock control bit for IOCON, GPIO, and GPIO interrupts". It was added in the UM10360 revision of 4 January 2010, per its revision history.
  - The same manual's GPIO chapter (§9.1, basic configuration) says "Power: always enabled".
  - The simulator keeps accepting GPIO writes with PCGPIO cleared (E9 CP9).
  - Whether silicon gates GPIO on PCGPIO is therefore unsettled. Nothing indicates GPIO needs enabling from reset: the bit resets to 1.
- **Keil's `GPIO_PortClock`:** `LPC_SC->PCONP |= (1U << 15)`, or `&= ~(1U << 15)`. The MCB1700 LED, joystick, buttons, GLCD and LCD drivers all call `GPIO_PortClock(1)`, which sets an already-set bit. None clears it.

**Decision:** option C, enough PCONP for the real firmware path, without claiming system control support.
- `Lpc1768` stores PCONP: 32-bit accesses at `0x400FC0C4` only, reset `0x042887DE`. Narrow accesses fault, and so do the neighbouring system control registers (PCON, …).
- **No bit has any effect.** GPIO keeps working with PCGPIO clear, following UM10360 §9.1 and the simulator. Nothing is clocked, gated or powered.
- `KeilGpioDriver::port_clock` makes Keil's read-modify-write. The C GPIO layer's `GPIO_PortClock` now calls it instead of doing nothing.
- The host register path reads and writes the same PCONP, so both paths agree, and Keil's `GPIO_LPC17xx.c` runs unmodified including `GPIO_PortClock`.
- **This adds no pressure toward a clock model:**
  - the register is plain storage;
  - no other system control register is mapped;
  - peripherals the model lacks remain absent whatever their PCONP bit says.

**Why not B (keep it unmapped):** Keil's own register-level driver could not run `GPIO_PortClock`, so the two host paths would disagree about a real firmware call for no fidelity gain.

**Why not full A:** gating GPIO on PCGPIO contradicts both the GPIO chapter and the simulator.

**Tests:**
- `tests/pconp_test.cpp`: reset value; read-back; word-only access; neighbours fault; GPIO not gated; `port_clock`.
- `HostRegisters.PconpIsSharedWithTheCGpioLayer`.
- `KeilRegisterDriver.PortClockMatchesThePhase1DriverEmulation`: replaces the death test that expected a fault.
- The convergence snapshots (`tests/gpio_snapshot.hpp`) now include PCONP.

**Future verification:** on hardware, clear PCGPIO and check whether GPIO still responds.

## 4. LED polarity: RESOLVED, active-high

**Previously:**
- `kLedActiveHigh = true` came from Keil's board driver and the legacy Keil drivers, both of which drive high for on.
- One firmware source's comment assumed active-low.
- Hardware had not confirmed either.

**Evidence:**
- **Schematic p. 3:**
  - P1.28, P1.29, P1.31 and P2.2–P2.6 drive inputs A1–A8 of **IC9, a 74LVC244** (a non-inverting buffer).
  - Outputs Y1–Y8 go through the 680 Ω arrays R33 and R40 to each **LED anode**. The cathodes all go to **GND**.
  - GPIO high therefore gives buffer output high, current through the LED, and **LED on**. This holds for all eight; there is one bank and one polarity.
  - IC9's output enables (`OE1`, `OE2`) connect to the **LED jumper**, with R31 (22 kΩ) pulling them high. With the jumper fitted the buffer drives; removed, its outputs float and every LED is dark regardless of GPIO.
- **Keil `LED_MCB1700.c`:** `LED_On` writes 1 and `LED_Off` writes 0.
- **Phase 0:** E2/E3 observe the driver's writes. They show register state only, not light.

**Conclusion:**
- Schematic and board driver agree: high means on, for all eight LEDs.
- The active-low comment in the other firmware source is wrong for this board.
- The model's `kLedActiveHigh = true` is correct. The LED jumper is assumed fitted and is not modelled.

**Impact on the model:** none. The documentation now cites the schematic.

## What remains deferred

Nothing in this review is deferred outright. Two items stay **bounded**, and each needs a real board to close:
- the narrow FIOPIN store (question 1);
- the released level with Keil's pull-down enabled (question 2).

Whether silicon gates GPIO on PCGPIO is recorded under question 3 but does not affect the model: the bit resets to 1, and no MCB1700 firmware clears it.
