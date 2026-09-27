# Latasim — Virtual Embedded Systems Workbench

Latasim is a virtual lab bench for embedded firmware. It runs real Keil MCB1700 (NXP LPC1768, Cortex-M3) firmware against a simulated board, so LEDs, buttons, the joystick and the LCD can be driven, observed and asserted on in **deterministic, repeatable tests**, with no hardware on the desk.

**Status:** Phase 1 is complete: a production C++20 model of LPC1768 GPIO and the MCB1700 LEDs, with 51 tests and a CLI demo. Phase 0 (feasibility) experiments and evidence are kept alongside. Planned stack: C++20 and CMake, CLI first, Qt desktop UI later.

## Why

Embedded firmware is usually tested by hand on a physical board: flash it, press a button, watch an LED. That loop is slow, hard to repeat, and impossible to run in CI. Latasim aims to turn it into a real, repeatable test:

```
reset → run until address X (or simulated time T) → expect P1.28 == 1
```

It should work whether the firmware calls a board library (`LED_On(0)`) or writes registers directly (`LPC_GPIO1->FIOSET = 1 << 28`, including bit-band aliases).

## Architecture

Firmware reaches the board through different routes, all converging on one model:

```
firmware ── B1: board-API calls ─────┐
         ── B2: register accesses ───┼─► LPC17xx GPIO register model ─► MCB1700 board model ─► traces / assertions / UI
         ── B3: real AXF in µVision ─┘      (FIODIR/FIOPIN/FIOSET/FIOCLR,     (pin map, LEDs,
                (UVSC socket API)            bit-band decode)                   joystick, INT0, GLCD)
```

| Backend | What it is | Phase 0 decision |
|---|---|---|
| **B1** | Host-compiled firmware with board-API shims | Required for V1 |
| **B2** | Host-side LPC17xx register/MMIO model | Required for V1 |
| **B3** | The unmodified `.axf` in Keil µVision's LPC1768 simulator, driven over UVSC | **YELLOW**: works, with narrower scope |
| **B4** | External CPU emulator | After V1 |

Full reasoning: [docs/phase0/backend-decisions.md](docs/phase0/backend-decisions.md).

## Phase 0 results

Every experiment below is scripted, was re-run to check repeatability, and has its raw output committed under `spikes/`.

| Question | Answer | Evidence |
|---|---|---|
| Can a C++ program drive µVision? | **Yes.** A native C++20 spike connects over UVSC, enters the simulator, runs to addresses, resets and shuts down cleanly: **34/34 steps passed**, twice | [uvsc-results.md](docs/phase0/uvsc-results.md) |
| Is simulation deterministic? | **Yes, to the CPU cycle.** Repeat runs and resets give identical stop points (e.g. the same `60,019,170` cycles every time) | E2, E4, E5, UVSC |
| Can GPIO be observed? | **Yes, including bit-band alias writes.** A firmware bug where a direct alias targets P1.27 instead of P1.28 is caught | E2, E5 |
| Can inputs be injected? | **Yes.** Pressing INT0 through the simulator's pin registers changes the unmodified firmware's behaviour, reproducibly | E4 |
| Can the LCD be observed? | **Moderate.** Every SPI byte to the panel is captured exactly and decodes, but text must be rebuilt from pixels | E6 |
| How fast is it? | 1.7–2.0 M cycles/s, about **50–60× slower than real time**, so simulator tests must be short | E3 |

**Tool behaviour discovered along the way** (none of it in the official docs):
- `UVSC_Init` accepts only a port range of exactly 10.
- UVSC auto-start fails and leaves an orphaned µVision.
- Commands are asynchronous and race with µVision's own startup.
- µVision saves breakpoints into the project file and restores them *after* the init script runs.
- `LOG` rejects paths with spaces.
- The `seconds` variable doesn't exist for the LPC1768.

Each finding was diagnosed and worked around, and all are documented in [findings.md](docs/phase0/findings.md) §12.

**Board facts:** a sourced pin map of the MCB1700's LEDs, joystick, INT0 button, GLCD (SSP1) and potentiometer, cross-checked in the simulator: [mcb1700-pin-map.md](docs/phase0/mcb1700-pin-map.md).

## Phase 1: the production model

- **One authoritative state.** An LPC17xx GPIO model (FIODIR, FIOMASK, FIOPIN, FIOSET, FIOCLR, bit-band aliases) whose semantics were checked register by register against µVision's LPC1768 simulator.
- **Board layer.** An MCB1700 board model maps pins to LED0–LED7 and derives each LED's state (On, Off, or Undriven when its pin isn't an output) from that model.
- **Two paths in, converging.** Direct register stores (B2) and host versions of Keil's GPIO and LED drivers (B1) both go through the same memory-mapped interface. Tests prove they leave identical state.
- **Cross-checked.** The tests replay recorded simulator runs (E2, E5, E7) and match every register value, including the wrong-pin bit-band alias.

```powershell
scripts\build.ps1          # configure + build + run the 51 tests (MSVC, Ninja, GoogleTest)
build\latasim gpio-demo    # deterministic LED demo through both paths
```

```
Keil board API (LED_*), same register model
  LED_Initialize()                             LED0 OFF       FIO1PIN=4FFFC713 FIO2PIN=00003F83
  LED_On(0)                                    LED0 ON        FIO1PIN=5FFFC713 FIO2PIN=00003F83
  FIO2CLR <- 00000004 (register store)         LED3 OFF       FIO1PIN=4FFFC713 FIO2PIN=00003F83
```

Details, exact semantics, limitations and the Phase 2 list: [docs/phase1/production-model.md](docs/phase1/production-model.md).

## Layout

| Path | What |
|---|---|
| `src/lpc17xx/` | GPIO model, memory map and bit-band (`Lpc1768`), Keil GPIO driver (B1) |
| `src/boards/mcb1700/` | Board model (LED pins, polarity, LED state), Keil LED board API (B1) |
| `src/cli/` | The `latasim` command-line tool |
| `tests/` | GoogleTest suite, including replays of recorded simulator runs |
| `docs/phase1/` | The production model |
| `docs/phase0/` | Findings, pin map, UVSC results, backend decisions |
| `spikes/uvsim-script/` | µVision debug-script experiments E1–E7 (`*.ini`) and their recorded output (`*-run*.out`) |
| `spikes/uvsc/` | Throwaway native C++ UVSC spike and its recorded results |
| `scripts/` | Reproduce everything (PowerShell) |
| `reference/` | Generated copies of the reference firmware (gitignored; recreate with `scripts/`) |
| `third_party/uvsc/` | Provenance for Keil AN198 (UVSC). The package itself is Keil-licensed and not included; download it as described in `PROVENANCE.md` |

## Reproduce the Phase 0 experiments

The production build above needs only VS Build Tools. The experiments need Keil:

```powershell
scripts\make-reference.ps1                      # Keil Blinky → reference\blinky (simulator-configured) + build
scripts\snapshot-project.ps1 -Source <bit-band test project folder> -Project Bitband -Name bitband-test
scripts\snapshot-project.ps1 -Source <joystick/LED demo project folder> -Project Blinky -Name joystick-led-demo
scripts\run-sim.ps1 -Ini spikes\uvsim-script\e2-gpio-observe.ini -TimeoutSec 240
scripts\run-uvsc-spike.ps1                      # builds the opt-in UVSC spike (build\uvsc-spike) + runs it
```

**Requirements:**
- Keil µVision 5.35 at `C:\Keil_v5`
- `Keil.LPC1700_DFP 2.6.0`
- VS 2026 Build Tools (MSVC 14.51, bundled CMake and Ninja)
- The bit-band test and joystick/LED demo µVision projects (any location), for the snapshots
- Keil AN198 `apnt_198.zip` in `third_party/uvsc/`, for the UVSC spike (see its `PROVENANCE.md`); the build generates the UVSC declarations from it
