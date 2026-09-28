# Latasim — Virtual Embedded Systems Workbench

Latasim is a virtual lab bench for embedded firmware. It runs real Keil MCB1700 (NXP LPC1768, Cortex-M3) firmware against a simulated board, so LEDs, buttons, the joystick and the LCD can be driven, observed and asserted on in **deterministic, repeatable tests**, with no hardware on the desk.

**Status:** Phases 0, 1, 2 and 3 are complete. Keil's own MCB1700 board drivers, compiled unmodified as C for the host, drive a modelled LPC1768/MCB1700. The model covers GPIO, LEDs, joystick and INT0, and register-level firmware runs through a host device header. Every hardware interaction lands in a deterministic trace, and the same scenario run as real ARM firmware in µVision's simulator gives the same register values. Phase 3 adds deterministic virtual time: SysTick counts virtual core cycles and calls the firmware's `SysTick_Handler`, so Keil's Blinky_ULp runs its 10 ms LED chase on the host with the simulator's timing, and tests read as `run_until(10ms); EXPECT_TRUE(s.led(1, LedState::On))`. 182 tests; 23 of them need the Keil packs installed. Summaries: [Phase 2](docs/phase2/overview.md), [Phase 3](docs/phase3/overview.md). Planned stack: C++20 and CMake, CLI first, Qt desktop UI later.

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
scripts\build.ps1          # configure + build + run the tests (MSVC, Ninja, GoogleTest)
build\latasim gpio-demo    # deterministic LED demo through both paths
```

```
Keil board API (LED_*), same register model
  LED_Initialize()                             LED0 OFF       FIO1PIN=4FFFC713 FIO2PIN=00003F83
  LED_On(0)                                    LED0 ON        FIO1PIN=5FFFC713 FIO2PIN=00003F83
  FIO2CLR <- 00000004 (register store)         LED3 OFF       FIO1PIN=4FFFC713 FIO2PIN=00003F83
```

Details, exact semantics, limitations and the Phase 2 list: [docs/phase1/production-model.md](docs/phase1/production-model.md).

## Phase 2: real firmware on the host, traced

- **Real firmware.** Keil's `LED_MCB1700.c`, `Joystick_MCB1700.c` and `Buttons_MCB1700.c` are compiled unmodified as C from the installed packs. They link against C-linkage versions of Keil's `GPIO_*`/`PIN_*` functions that act on a bound board.
- **Register-level code.** A host `LPC17xx.h` keeps Keil's names and layout (`LPC_GPIO1->FIOSET`, `FIOPIN0`, …), but its registers are proxies into the model. Keil's own `GPIO_LPC17xx.c` runs unmodified. Literal addresses need one documented adaptation (`LATASIM_REG32`).
- **Inputs.** Joystick and INT0 are board switches driving pin levels, and firmware reads them through GPIO.
- **Trace.** Every MMIO load and store, input change and LED change is a structured event, sequenced per machine and identical on every run.
- **Checked against the simulator.** The same scenario, as ARM firmware in µVision, matches register for register (E10). Four hardware questions were settled or bounded from NXP's manual, the data sheet and the board schematic: [open-questions.md](docs/phase2/open-questions.md).

```powershell
build\latasim firmware-demo    # needs the Keil packs at build time
```

```
firmware: LED_On(0)
    #51   write32 FIO1SET   0x10000000
    #52   led     LED0      ON

board:    joystick UP pressed
    #53   input   P1.23     low
firmware: Joystick_GetState() = 0x08 (JOYSTICK_UP)
    #54   read32  FIO1PIN   0x5F7FC713
```

Architecture, what is and isn't supported, and the evidence: [docs/phase2/overview.md](docs/phase2/overview.md).

## Phase 3: firmware over virtual time

- **Virtual time.** One integer clock of core cycles (100 MHz) per machine, moved only by `advance_cycles(n)`. No wall clock, sleeps or threads anywhere.
- **SysTick.** STCTRL/STRELOAD/STCURR/STCALIB in the memory map, counting virtual cycles. It is checked against ARM's Cortex-M3 guide, NXP's manual and two simulator experiments (E11, E12).
- **Timed firmware.** When SysTick counts to 0 with TICKINT set, the firmware's `SysTick_Handler` runs at that virtual time. This is SysTick only, with no NVIC or priorities. Keil's Blinky_ULp `IRQ.c` runs unchanged: its LED chase steps every 10 ms, first 999,999 cycles after `SysTick_Config`, exactly as in µVision.
- **Scenarios.** A typed GoogleTest layer: `run_for`/`run_until` in exact virtual time, `press`/`release`, and `led`/`pin`/`reg` checks. On failure, a check prints the virtual time and the last trace events.
- **Timed trace.** Every event carries its virtual time, and each handler call is an event:

```
    #38   t=999999     systick handler
    #39   t=999999     write32 FIO1CLR   0x10000000
    #40   t=999999     write32 FIO1SET   0x20000000
    #41   t=999999     led     LED1      ON
```

What is and isn't supported, the evidence and the open questions: [docs/phase3/overview.md](docs/phase3/overview.md).

## Layout

| Path | What |
|---|---|
| `src/lpc17xx/` | GPIO model, memory map, bit-band, PCONP, SysTick and virtual time (`Lpc1768`), Keil GPIO driver (B1) |
| `src/host/` | Host firmware support: board binding, C-linkage Keil GPIO functions, host `LPC17xx.h` and register proxies |
| `src/trace/` | The deterministic hardware trace, with virtual time |
| `src/firmware/` | Host ports of timed Keil examples (Blinky_ULp) |
| `src/boards/mcb1700/` | Board model (LEDs; joystick and INT0 inputs), Keil LED board API (B1) |
| `src/cli/` | The `latasim` command-line tool |
| `tests/` | GoogleTest suite, including replays of recorded simulator runs and the scenario layer (`scenario.hpp`) |
| `docs/phase3/` | Phase 3: [overview](docs/phase3/overview.md), timed firmware, timing questions |
| `docs/phase2/` | Phase 2: [overview](docs/phase2/overview.md), MMIO and inputs, host firmware, registers, hardware-behaviour questions |
| `docs/phase1/` | The production model |
| `docs/phase0/` | Findings, pin map, UVSC results, backend decisions |
| `spikes/uvsim-script/` | µVision simulator experiments E1–E12 (`*.ini`, firmware sources for E9, E10, E12) and their recorded output (`*-run*.out`) |
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
