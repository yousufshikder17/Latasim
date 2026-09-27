# Phase 2 (in progress): MMIO access widths and board inputs

**Status:** deliverables 1–6 are done. Deliverables 1–3 (the C firmware boundary, real firmware on the host, register-level firmware) are described in [host-firmware.md](host-firmware.md) and [host-registers.md](host-registers.md). Tracing is next.

## MMIO: 8-, 16- and 32-bit access

`lpc17xx::Lpc1768` exposes `read8/16/32` and `write8/16/32`.

- **The decoder holds no semantics.** It maps (address, size) to (port, register, byte lanes) and calls `Gpio::read`/`Gpio::write`.
- **Narrow accesses** address the byte and halfword registers that `LPC17xx.h` declares in each register's union (`FIO1PIN0`–`FIO1PIN3`, `FIO1PINL`/`FIO1PINH`, `FIO1CLR3`, …).
- **A narrow store changes only its own lanes**, with the usual rules inside them:

  | Register | Narrow store |
  |---|---|
  | SET / CLR | set or clear the given bits |
  | PIN | replace the lane's latch bits, except masked ones |
  | DIR / MASK | replace the lane |

  A full-word store is all four lanes, so Phase 1's 32-bit semantics are unchanged.
- **Faults:** accesses must be naturally aligned. Reserved offsets, unmapped addresses, and 8- or 16-bit access to a bit-band alias raise `BusFault`. Nothing behaves like generic memory.

**Evidence and its limit (E8, `spikes/uvsim-script/e8-gpio-subword.ini`):**
- **The per-lane registers of `LPC17xx.h` support lane-only semantics.** The SVD defines only the 32-bit registers.
- **The simulator could not confirm them.** µVision's debug functions `_WWORD`/`_RWORD` move 32 bits despite being documented as 16-bit. Hand-assembled `STRB`/`STRH` in SRAM could not be executed from an INI (the PC stayed put, on two projects).
- **The debugger's `_WBYTE` to FIOPIN acts like a word read-modify-write.** Given the above, that says nothing about what a CPU `STRB` does.
- **So narrow-access semantics are a documented model decision, not a verified fact** (see open questions).
- **Later, E9 ran compiled `STRB`/`STRH` in the simulator** ([open-questions.md](open-questions.md), question 1). Narrow SET/CLR/DIR stores and narrow loads are lane-local there too. A narrow FIOPIN store, however, acts as a word read-modify-write in the simulator, unlike this model. The model follows UM10360 chapter 9; the question stays bounded.

## Board inputs

`mcb1700::Board` owns the physical switches and drives their pins' **external level** in the GPIO model. The latch is never touched.

| Input | Pin | Polarity |
|---|---|---|
| Joystick center (select) | P1.20 | active-low |
| Joystick up | P1.23 | active-low |
| Joystick right | P1.24 | active-low |
| Joystick down | P1.25 | active-low |
| Joystick left | P1.26 | active-low |
| INT0 button | P2.10 | active-low |

- **Pins and polarity** come from the Phase 0 pin map (Keil board sources and the MCB1700 user guide).
- **Released reads high; held reads low.** Several switches may be held at once.
- **INT0 is GPIO-only:** there is no NVIC, interrupt dispatch or ISR model.

## GPIO model: four separate things per pin

| Name | What it is | Changed by |
|---|---|---|
| direction | FIODIR bit | firmware |
| latch | output register | FIOSET/FIOCLR/FIOPIN writes (unmasked lanes) |
| external | level driven from outside | the board (`set_external_level`) |
| level | the pin | derived: latch if output, external if input |

`gpio_external_input_test.cpp` covers:
- inputs driven high, low and changing;
- switching direction both ways;
- latching on an input;
- masking.

## Tests

79 in total, all passing.

| Group | Count |
|---|---|
| Phase 1 | 51 |
| narrow access | 8 |
| external input | 9 |
| board inputs | 11 |

## Open questions

As recorded when D4–D6 were done. Questions 1 and 2 were reviewed later in [open-questions.md](open-questions.md): 1 is bounded (the simulator diverges for narrow FIOPIN stores), and 2 is resolved from the schematic (a 22 kΩ external pull-up, R27).

1. **What does a CPU `STRB`/`STRH` to FIOPIN really do?** Lane-only (modeled) or word read-modify-write? This needs a real firmware build under B3 or real hardware, since the debugger can't answer it. The obvious B3 test is a tiny ARMCC-built image that does `STRB` to `FIO1PIN0` with inputs high in the same word.
2. **The INT0 pull resistor.** Keil's `Buttons_Initialize` enables the internal **pull-down** on P2.10, yet reads the pin as active-low. The model follows the board guide (released = high), assuming an external pull-up dominates. It needs confirming on hardware.
3. **PINMODE is not modeled.** Released inputs read high because PINMODE resets to pull-up and the board's switches pull to ground. Firmware that changes PINMODE would not change what the model reads.
4. **Narrow bit-band alias accesses** are rejected. The Cortex-M3 allows them, but no current firmware uses them.
