# Phase 1: the production model

Phase 1 builds the smallest production foundation that models LPC1768 GPIO correctly and shows it through the MCB1700 LEDs:

```
board API call (B1) ─┐
                     ├─► Lpc1768::read32/write32 ─► lpc17xx::Gpio ─► mcb1700::Board ─► LED state ─► tests / vwb CLI
register store (B2) ─┘      (memory map, bit-band)     (one latch       (pin map,
                                                        per port)        polarity)
```

There is exactly one authoritative state: `lpc17xx::Gpio`. Everything else derives from it or writes through it.

## Layout

| Path | What |
|---|---|
| `src/lpc17xx/gpio.*` | `Gpio`: FIODIR/FIOMASK/FIOPIN/FIOSET/FIOCLR semantics for ports 0–4 |
| `src/lpc17xx/lpc1768.*` | `Lpc1768`: 32-bit loads/stores at real addresses, GPIO decode, bit-band alias, `BusFault` |
| `src/lpc17xx/keil_gpio_driver.*` | `KeilGpioDriver`: host version of Keil's `GPIO_LPC17xx.c` (B1) |
| `src/boards/mcb1700/board.*` | `Board`: owns the `Lpc1768`; LED pin map, polarity, `LedState` |
| `src/boards/mcb1700/keil_board_led.*` | `KeilBoardLed`: host version of Keil's `LED_MCB1700.c` (B1) |
| `src/cli/` | `vwb` executable; `gpio-demo` |
| `tests/` | GoogleTest suite (51 tests) |
| `spikes/`, `docs/phase0/` | Phase 0 experiments and evidence, unchanged apart from the new E7 |

Build and test with `scripts\build.ps1`, then run `build\vwb gpio-demo`.

## GPIO semantics

**Sources:**
- NXP's register description (`LPC176x5x.svd` in LPC1700_DFP 2.6.0).
- Simulator experiment **E7** (`spikes/uvsim-script/e7-gpio-semantics.ini`), which probes the points the SVD leaves open. The unit tests replay E7 register by register.

Each port keeps `dir`, `mask`, `latch` (the output register) and `external` (the level driven onto input pins from outside).

| Access | Behaviour | Evidence |
|---|---|---|
| FIODIR read/write | 32-bit storage; 1 = output | SVD; E7 R11 |
| FIOMASK read/write | 32-bit storage; 1 = pin ignores SET/CLR/PIN writes | SVD; E7 R5, R6 |
| FIOSET write | `latch \|= value & ~mask`, for input pins too | E7 R1, R5 |
| FIOSET read | `latch` (not masked, not the pin level) | E7 R3, R13 |
| FIOCLR write | `latch &= ~(value & ~mask)` | E7 R3 |
| FIOCLR read | `0` (write-only register) | SVD; E7 R3 |
| FIOPIN write | `latch = (latch & mask) \| (value & ~mask)` | E7 R6, R8 |
| FIOPIN read | `level & ~mask`; masked bits read 0 even when the pin is high | E7 R13, R14 |
| pin level | latch bit for outputs; external level for inputs | E7 R2, R9, R10 |
| reset | all registers 0; `external` = high for bonded-out pins (PINMODE resets to pull-up) and low for the rest | SVD; E1 |

**Bit-band.** A store to an alias in `0x22000000–0x23FFFFFF` is a read-modify-write of the whole target word, as the Cortex-M3 bus does it. For FIOPIN that has a side effect: the current levels of *input* pins are written into their latch bits. The model reproduces this (`Lpc1768.BitBandFioPinWriteCopiesInputLevelsIntoTheLatch`).

**Memory map.** Only the GPIO block (`0x2009C000`, five ports of `0x20`) and its bit-band aliases are mapped. Reserved offsets, unaligned addresses and every other address raise `BusFault`. A fault behind an alias reports the alias address.

## MCB1700 LEDs

- **Pins:** LED0–LED7 = P1.28, P1.29, P1.31, P2.2–P2.6, in Keil's board-driver numbering (see [the pin map](../phase0/mcb1700-pin-map.md)).
- **Derived state:** an LED's state is computed from the GPIO model and never stored:
  - `Undriven` while its pin is not an output. The model doesn't guess what a floating LED driver input does, so a missing FIODIR write is visible.
  - Otherwise `On` or `Off` from the pin level.
- **Polarity** is the single constant `kLedActiveHigh = true`. Keil's board driver and the legacy `LED.c` both drive high for on. One firmware source assumes active-low, and real hardware has not confirmed either, so this is the one line to change.

## How B1 and B2 converge

B1 is not a second model. `KeilGpioDriver` and `KeilBoardLed` make **the same register accesses the real Keil drivers compile to**, through `Lpc1768::write32`:

| Call | Store |
|---|---|
| `LED_On(n)` | `FIOSET = 1 << pin` |
| `LED_Off(n)` | `FIOCLR = 1 << pin` |
| `GPIO_SetDir` | read-modify-write of FIODIR |

So a board-API call and a direct register store land in the same `Gpio` latch. The convergence tests check that:
- equivalent sequences (API, plain stores, bit-band stores) leave identical registers on all five ports and identical LED states;
- each path sees the other's writes;
- a masked pin ignores both.

## Determinism

There is no clock, no threads and no wall time: state changes only on an explicit register access. The CLI demo's output is pinned byte for byte by a test. No time model was introduced, because nothing in Phase 1 needs one.

## Cross-checks against the simulator (B3 as reference)

| Test | Checks |
|---|---|
| `Gpio.ReplayOfSimulatorExperimentE7MatchesEveryReading` | all 16 E7 readings |
| `Lpc1768.ReplayOfBitBandTestFirmwareMatchesSimulatorE5` | the bit-band test firmware's GPIO accesses, and every FIO1PIN/FIO2PIN word E5 recorded, including the wrong-pin alias |
| `KeilBoardLed.InitializeMatchesSimulatorE2EndState` | Keil `LED_Initialize` ends where E2 recorded (FIO1PIN `4FFFC713`, FIO2PIN `00003F83`) |

The demo's `LED_On(0)` state (FIO1PIN `5FFFC713`) also matches E3.

**One known difference from µVision:** its `PORTn` pin override does not persist once the pin has been an output (E7 R12). The model's `set_external_level` does persist, which matches a real external driver. The E7 replay test releases the pin explicitly at that point.

## Known limitations

- **GPIO only.** No PINCON/PINMODE (pull resistors are fixed at their reset pull-up), no PCONP clock gating, no GPIO interrupts, no other peripherals.
- **32-bit accesses only.** The byte and half-word views in `LPC17xx.h` (`FIO1PIN3`, `FIOPINL`, …) are not decoded yet.
- **Bit-band covers the SRAM region only**, which holds the GPIO block. The peripheral alias region (`0x42000000`) is not mapped.
- **B1 is a C++ class API, not C symbols.** Firmware source that calls `LED_On()` cannot be linked against it yet. `GPIO_PortClock`, `GPIO_PortWrite/Read`, `PIN_Configure` and `LED_Uninitialize` are not implemented.
- **Firmware that stores to literal addresses** (`*(volatile uint32_t*)0x233806F0 = 0`) is covered by the `write32` interface, but there is no mechanism yet to route a host-compiled program's raw pointer stores into it (Phase 0 finding 17).
- **LED polarity is unconfirmed on hardware.**
- **No trace output yet.** Tests assert on state after each step.

## Found during Phase 1

- **Masked FIOPIN bits read 0, and FIOSET reads ignore the mask** (E7). The SVD text alone was ambiguous here.
- **FIOSET writes to input pins are latched** and appear when the pin becomes an output. This is a real way firmware can light an LED "early".
- **Bit-band FIOPIN stores copy input levels into the latch**, a side effect a naive per-bit model would miss.
- **None of this changes the architecture.** B2 as the single authoritative state, with B1 writing through it, works without special cases.

## Phase 2: remaining work

1. **Run real firmware sources under B1.** C-linkage `LED_*`/`GPIO_*` symbols bound to a board instance; compile the bit-band test and Keil Blinky's LED code against a host `LPC17xx.h` whose `LPC_GPIOn` accesses reach `Lpc1768`.
2. **Literal-address spike.** Route raw pointer stores (`0x233806F0`) from host-compiled code into `write32`, by trapping the address range or by instrumentation. This decides how far B2 reaches for direct-alias firmware.
3. **Byte and half-word GPIO views**, needed as soon as real firmware uses them.
4. **Inputs:** joystick (P1.20, P1.23–P1.26) and INT0 (P2.10) as board devices driving `set_external_level`, active-low.
5. **A trace of register accesses** (who wrote what, in order): the basis for assertions, replay and conflicting-writer detection.
6. **Legacy LED API** (`LED_Init/On/Off/Out` via FIOPIN read-modify-write) for the joystick/LED demo firmware.
7. **A deterministic time model**, only when SysTick-driven firmware is brought in.
8. **B3 acceptance hook:** compare a scenario's GPIO snapshots against a µVision run of the same AXF.
