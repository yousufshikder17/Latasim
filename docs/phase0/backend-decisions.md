# Backend decisions after Phase 0

Evidence: [findings.md](findings.md) and [uvsc-results.md](uvsc-results.md).

## The convergence point is the GPIO register model

Phase 0 showed that both firmware styles meet at the **LPC17xx GPIO registers**, not at the LED:

- The Keil `LED_On(n)` is `GPIO_PinWrite` → `FIOSET`/`FIOCLR`.
- The legacy `LED_On(n)` is `FIOPIN |= mask`.
- The bit-band test firmware uses `FIOSET`/`FIOCLR`, a computed bit-band alias of `FIOPIN`, and literal alias addresses.
- In the simulator, all of these land on the same pin state (E2, E5).

So the intended semantic chain becomes:

```
firmware operation ── B1: board-API call ──┐
                   ── B2: register access ─┼─► LPC17xx GPIO model (DIR/MASK/PIN/SET/CLR + bit-band)
                   ── B3: µVision pin state┘            │ pin levels
                                                        ▼
                                          MCB1700 board model (pin map, LED polarity constant,
                                          active-low inputs) ─► LED/joystick/INT0 state
                                                        ▼
                                                trace / assertions / UI
```

B1 and B2 are not alternatives. They are two ways of feeding the **same** register model. B3 bypasses the model and reports **pin levels**, which the board model consumes directly.

## Classification

| Backend | Class | Basis |
|---|---|---|
| **B1: board-API shim** | **V1 required** | The joystick/LED demo uses the legacy API, and Keil examples use the new `Board_*` API. B1 is also the only cheap way to get GLCD **text** and to handle RTOS-level waits |
| **B2: register/MMIO model** | **V1 required** | The bit-band test firmware is pure register access and is the best acceptance case, with a real bug to catch. Both board APIs are themselves register code |
| **B3: µVision/UVSC** | **V1 feasible**, gate **YELLOW** | Control, determinism, GPIO observation and injection are all proven; the scope is narrowed below |
| **B4: external CPU emulator** | **post-V1** | No blocker found that requires it now. See the reconsideration trigger under B2 |

### B1: board-API shim (V1 required)

- **Two API surfaces, implemented exactly:**
  - New `Board_LED/Buttons/Joystick/GLCD/ADC`.
  - Legacy `LED_Init/On/Off/Out`, `KBD_Init/get_button`, `GLCD_Init/Clear/DisplayString/…`.
  - The joystick bit encodings differ between them.
- **Preferred implementation for GPIO-backed APIs:** compile the **real driver sources** (`LED_MCB1700.c` + `GPIO_LPC17xx.c`, legacy `LED.c`/`KBD.c`) against the host B2 register model. Then `LED_On(0)` goes through `FIOSET` for free, and no LED semantics are duplicated.
- **Hand-written shims** only where the driver talks to unmodeled hardware:
  - **GLCD:** intercept at the text API into a text grid.
  - **ADC:** value source.
  - **RTOS2:** post-first-milestone.
- **Time is logical.** API calls and register accesses are yield points; busy loops with no I/O (the bit-band test's `delay`) need a declared cost or a by-name intercept.

### B2: register/MMIO model (V1 required)

- **The model:**
  - `FIODIR`, `FIOMASK`, `FIOPIN`, `FIOSET`, `FIOCLR` per port.
  - Input pins read external levels; output pins read the latch.
  - `FIOMASK` gating.
  - Bit-band alias decode for `0x22000000–0x23FFFFFF` (SRAM/GPIO) and `0x42000000–0x43FFFFFF` (peripherals).
  - Later: SysTick, SSP1 as a byte sink, ADC.
- **Delivering accesses from host-compiled firmware:**
  - **Easy:** `LPC_GPIOn->FIOx` (CMSIS struct access). Build against a host `LPC17xx.h` whose `LPC_GPIOn` points at model storage with observable writes.
  - **Hard, and unproven:** literal addresses (`*(volatile uint32_t*)0x233806F0`). This needs trapping writes to those address ranges on the host, or compiler instrumentation.
  - **Phase 1 must start with a small spike on this.**
- **Reconsideration trigger for B4:** if host-side trapping of literal addresses proves impractical, the remaining options for direct-alias firmware like the bit-band test are B3 (works today) and B4 (run the real AXF under an emulator with MMIO hooks). B4 stays post-V1 unless both that spike and B3 fall short.

### B3: µVision / UVSC (V1 feasible, YELLOW)

**Proven**
- Headless batch runs.
- Native C++ UVSC control: connect, debug-enter, run-to-address, start/stop, reset, exit, clean shutdown with no orphans.
- Deterministic stop points across runs and resets.
- GPIO observation, including bit-band.
- Pin-VTREG input injection.
- Byte-exact GLCD SPI capture.

**Why YELLOW, not GREEN**
1. `UVSOCK.h` is not in the official package. UVSC memory read/write, `VTR_SET`, `EXEC_CMD` and async stop decoding are untested, and today B3 observes through INI hooks plus the UVSOCK text log.
2. Commands are asynchronous. The reliable completion signal needs the missing async message structs; until then sequencing uses a settle delay.
3. Speed is 50–60× slower than real time, so B3 scenarios must be short (sub-second to a few seconds of firmware time).
4. Write hooks can't identify the written register or value. B3 reports pin **state** at deterministic points, not individual writes.
5. Auto-start fails. B3 must launch UV4 itself and supervise it (watchdog, kill on INI error).

**Narrowed V1 scope for B3**
- Same `.axf` as hardware.
- Launch hidden UV4 with `-s <port>`, connect.
- Deterministic stops by address or sim time.
- Pin state from the `PORTn` VTREG or `FIOnPIN` at stops.
- Inputs via the `PORTn` VTREG.
- No per-write conflict detection.
- No GLCD reconstruction.
- **Use:** "backend agreement" checks against B1/B2 and timing truth.

**Path to GREEN:** obtain an official `UVSOCK.h` and verify `DBG_MEM_READ`, `DBG_VTR_SET` and async `DBG_STOP_EXECUTION` decoding in the spike.

### B4: external CPU emulator (post-V1)

Unchanged. See the reconsideration trigger under B2.

## Decision gate A

**B3: YELLOW.** UVSC control works and determinism is excellent. The limits are the missing official `UVSOCK.h` (typed memory/VTREG access and async handshake), 50–60× simulation speed, and write-hook granularity, so B3 enters V1 with the narrowed scope above.

The original plan said that if the reference firmware turned out to use raw registers, B2 would replace B1 as the first backend for further development. That condition is met **partially**: it uses **both** styles, so Phase 1 builds the B2 register model as the core and B1 as driver-level feeding into it, not either one alone.

## Recommended Phase 1 architecture (not started)

1. **Core:**
   - Integer sim time: ticks = CPU cycles, which lines up with µVision `states`.
   - Event queue.
   - Trace (JSON Lines).
   - Assertions evaluated at deterministic points.
2. **`lpc17xx` register model (B2 core):**
   - GPIO0–4 with full `FIO*` semantics and bit-band decode.
   - SysTick next.
   - SSP1 as a byte sink.
   - Unit-tested directly against the Phase 0 facts:
     - `LED_Initialize` order.
     - The bit-band test stop table.
     - `0x233806EC` → P1.27.
3. **`mcb1700` board model:**
   - Pin map from `mcb1700-pin-map.md`.
   - **One** LED-polarity constant, unresolved until hardware says otherwise.
   - Active-low joystick and INT0.
4. **Host firmware harness (B1 + B2 together):**
   - Compile firmware sources plus the real GPIO drivers against a host `LPC17xx.h` bound to the register model.
   - Fibers with yield-at-access.
   - Text-level GLCD intercept for both API generations.
   - **First task: the literal-address trapping spike** (decides how far B2 reaches on the host).
5. **CLI:** `vwb run` with human and `--json` output.
6. **First acceptance programs, in order:**
   1. **Bit-band test firmware.** Masking, computed bit-band and direct alias; must report LED0 (P1.28) unchanged in direct mode (the real bug).
   2. **Joystick/LED demo firmware.** Joystick → LED mapping through the legacy API (SysTick ISR, ADC stubbed).
   3. **Keil MCB1700 Blinky** once an RTOS2 shim exists; until then, B3's reference program.
7. **B3, when it is built in a later phase of development, keeps the spike's lessons:**
   - Self-launch.
   - 10-port `UVSC_Init`.
   - Watchdog.
   - Hidden sessions only.
   - Clear breakpoints saved in the project before each launch.
   - Async handshake via `UVSOCK.h`.
