# Phase 2: host-executed firmware on a deterministic, observable board

**Status: complete.**

Phase 2 shows that embedded C firmware, compiled for the host, can drive a modelled MCB1700, react to injected inputs, and be observed through a deterministic trace. The same scenario, run as real ARM firmware in µVision's LPC1768 simulator, gives the same register values.

```powershell
scripts\build.ps1              # build and run all tests
build\latasim firmware-demo    # Keil's board drivers on the modelled board, with the trace
```

## Architecture

**Several front doors, one authoritative hardware state:**

```
            real C firmware (Keil LED/joystick/button drivers, register-level code)
                  |                                   |
      Keil GPIO/PIN API (C linkage)        host register binding
      src/host/keil_rte_gpio.cpp           src/host/device/LPC17xx.h
                  |                        LPC_GPIO1->FIOSET, LATASIM_REG32(addr)
                  +-----------------+-----------------+
                                    v
                     Lpc1768: 8/16/32-bit MMIO, bit-band, PCONP      ---> Trace
                                    |                                     ^
                     Gpio: DIR, MASK, latch, external level               |
                                    |                                     |
                     MCB1700 Board: LEDs  <--  joystick, INT0 ------------+
```

- **Nothing is duplicated.** The C GPIO layer calls the Phase 1 driver emulation. The register proxies call the same `Lpc1768` loads and stores. The board derives LEDs from GPIO and drives inputs as external pin levels.
- **Tests prove the paths converge:** identical register state, LED state and trace for the same operations.
- **The trace observes the real operations** at the MMIO boundary and at the board. There is no separate instrumentation state.
- **B3** (µVision's LPC1768 simulator running the real `.axf`) is the reference backend, not part of this architecture. It is used to check the model (E1–E10).

## Supported today

| Area | What | Where documented |
|---|---|---|
| GPIO | FIODIR, FIOMASK, FIOPIN, FIOSET, FIOCLR on ports 0–4, as verified against the simulator (E7) | [production-model.md](../phase1/production-model.md) |
| MMIO | 8-, 16- and 32-bit loads and stores; alignment and unmapped accesses fault | [mmio-and-inputs.md](mmio-and-inputs.md) |
| Bit-band | SRAM alias region covering the GPIO block; word accesses | [production-model.md](../phase1/production-model.md) |
| PCONP | Stored, reset `0x042887DE`, gates nothing | [open-questions.md](open-questions.md) §3 |
| LEDs | LED0–LED7, active-high (schematic), On/Off/Undriven | [open-questions.md](open-questions.md) §4 |
| Joystick | 5 directions, active-low, as board inputs | [mmio-and-inputs.md](mmio-and-inputs.md) |
| INT0 | P2.10, active-low, as a GPIO-visible input (no interrupt) | [open-questions.md](open-questions.md) §2 |
| External drive | Any input pin's level, separate from the output latch | [mmio-and-inputs.md](mmio-and-inputs.md) |
| C firmware API | Keil's `GPIO_*`/`PIN_*` with C linkage, `FirmwareBinding` | [host-firmware.md](host-firmware.md) |
| Real firmware | Keil's `LED_MCB1700.c`, `Joystick_MCB1700.c`, `Buttons_MCB1700.c` compiled unmodified as C; Keil's `GPIO_LPC17xx.c` unmodified as C++ | [host-firmware.md](host-firmware.md), [host-registers.md](host-registers.md) |
| Register code | Host `LPC17xx.h` with proxy registers; literal addresses via `LATASIM_REG32` (C++) or `latasim_mmio_*32` (C) | [host-registers.md](host-registers.md) |
| Trace | Structured, deterministic, per machine | below |
| B3 reference | µVision simulator experiments E1–E10, scripted and recorded | `spikes/uvsim-script/` |

## The trace

**Each `Lpc1768` owns a `Trace`** (`src/trace/trace.hpp`). There is no global trace, event bus or logging framework. An event is a plain struct:

| Field | Meaning |
|---|---|
| `seq` | 1, 2, 3, … per machine; a new board starts at 1 |
| `kind` | `Read`, `Write`, `Input` or `Led` |
| `address`, `width`, `value` | Read/Write: the MMIO address and access size the firmware used, and the value loaded or stored |
| `port`, `pin`, `value` | Input: the pin whose external level changed, and the new level |
| `led`, `value` | Led: the LED whose visible state changed, and the new `LedState` |

**What is recorded:**
- **Every successful MMIO load and store** through `Lpc1768::read8/16/32`/`write8/16/32`. This covers the C GPIO layer, the register proxies, `LATASIM_REG32` and the C++ drivers.
  - A bit-band access is one event at the alias address.
  - A faulting access records nothing.
  - The model's own internal lookups (LED state, snapshots) are not MMIO and are not recorded.
- **Board inputs** (`press`/`release`), only when a switch actually changes. The released state at reset is the initial condition, not an event.
- **LED changes**, recorded right after the store that caused them.

**Determinism:**
- No wall clock, host pointers, thread IDs or unordered containers are involved.
- Identical runs produce identical event vectors and byte-identical formatted text (tested).
- Recording can be switched off. The model's behaviour is identical either way (tested).

**Example:** `latasim firmware-demo`, trimmed:

```
firmware: LED_On(0)
    #51   write32 FIO1SET   0x10000000
    #52   led     LED0      ON

board:    joystick UP pressed
    #53   input   P1.23     low
firmware: Joystick_GetState() = 0x08 (JOYSTICK_UP)
    #54   read32  FIO1PIN   0x5F7FC713
    ...   (one FIO1PIN read per direction, as Keil's driver does)

board:    INT0 pressed
    #59   input   P2.10     low
firmware: Buttons_GetState() = 0x01 (INT0)
    #60   read32  FIO2PIN   0x00003B83
```

## End-to-end scenario

`KeilFirmware.EndToEndScenarioIsTracedAndDeterministic` (`tests/keil_firmware_test.cpp`) and `latasim firmware-demo` run the same steps:

1. **Initialise.** A new board is bound to host firmware, and `LED_Initialize`, `Joystick_Initialize` and `Buttons_Initialize` run. The trace opens with `GPIO_PortClock`'s PCONP read-modify-write, and all LEDs go from Undriven to Off.
2. **`LED_On(0)`** records a `FIO1SET` write, then `LED0 ON`.
3. **Joystick UP** is pressed: an input event, P1.23 low. `Joystick_GetState()` returns `JOYSTICK_UP`, and the trace shows the five `FIO1PIN` reads that saw the pin low.
4. **INT0** is pressed: an input event, P2.10 low. `Buttons_GetState()` returns 1, from one `FIO2PIN` read. INT0 is a GPIO-visible input; there is no interrupt.
5. **`LED_SetOut(joystick | buttons)`** lights LED0 and LED3. Both inputs are then released.
6. **Checks:**
   - the final GPIO and LED state;
   - a second run gives an identical trace and state;
   - the register values equal the simulator's for the same firmware and inputs (E10).

Nothing in Keil's API needed changing for this. The one adaptation remains the host `LPC17xx.h` ([host-firmware.md](host-firmware.md)).

## B3 reference evidence

| Behaviour | Evidence | Result |
|---|---|---|
| GPIO register semantics (SET/CLR/PIN/MASK, inputs, directions) | E7 | Model matches; replayed in tests |
| Input defaults (bonded pins high) | E1 | Model matches |
| `LED_Initialize` and `LED_On(0)` register values | E2 | Pinned in tests |
| Bit-band writes, including the wrong-pin alias | E5 | Replayed in tests |
| INT0 injection through pin VTREGs; P2.10 released reads high after `Buttons_Initialize` | E4 | Consistent with the model |
| CPU `STRB`/`STRH`, narrow loads, PCONP reset and gating | E9 | Recorded; one bounded divergence (narrow FIOPIN stores) |
| The whole firmware-demo scenario: Keil drivers, joystick and INT0 injected | **E10** (new) | Register values and driver results match at every checkpoint; pinned in the scenario test |

**E10 was the one new comparison.** The joystick driver consuming an injected press had no direct simulator evidence before it. Every other behaviour reuses earlier evidence.

**Normal builds and tests never need µVision.** Tests that compile Keil's sources need only the installed packs. Without them, 117 core tests still build and run, and `firmware-demo` says why it is unavailable.

## Not yet supported

- **Timing and interrupts:**
  - virtual time;
  - SysTick, timers, RIT;
  - the full clock tree (PLLs, CCLK), beyond storing PCONP;
  - NVIC, interrupt delivery, external interrupts. INT0 is GPIO-only.
- **Pin configuration:** PINSEL/PINMODE. `PIN_Configure` is accepted and ignored, and pull resistors are not modelled.
- **Other peripherals:** ADC/potentiometer, DAC, GLCD/SSP, UART, USB, Ethernet, CAN, I2C, I2S/audio.
- **Memory map:** narrow bit-band alias accesses, the peripheral bit-band region, and everything outside GPIO and PCONP.
- **Execution backends:** CPU instruction execution (B4); unchanged literal-pointer firmware on the host (see the policy in [host-registers.md](host-registers.md)).
- **Tooling:** scenario files, assertions over traces beyond the tests, trace replay, VCD export, Qt UI.

## Bounded questions carried forward

Both are documented in [open-questions.md](open-questions.md), and each needs a real board to close:
- **Narrow FIOPIN stores.** The model is lane-only, per UM10360; the simulator does a word read-modify-write (E9).
- **INT0's released level with Keil's internal pull-down enabled.** It sits between the guaranteed input levels.

## Tests

132 tests, all passing. 15 of them compile Keil's sources and are built only when the packs are installed.

| Group | Tests |
|---|---|
| GPIO model; memory map and bit-band; narrow access; PCONP | 15 + 11 + 8 + 4 |
| Board: LEDs, inputs, external drive | 8 + 11 + 9 |
| Keil driver emulation; convergence | 11 + 6 |
| C GPIO layer | 9 |
| Register binding | 11 |
| Trace | 12 |
| `gpio-demo` | 2 |
| Keil board drivers, scenario, `firmware-demo` (packs) | 10 |
| Keil `GPIO_LPC17xx.c` on the register path (packs) | 5 |
