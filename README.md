# Virtual Workbench

A virtual lab bench for Keil MCB1700 (LPC1768) firmware. It is planned as C++20 with a CLI first and a Qt UI later.

**Status:** Phase 0 (feasibility) is complete; see [docs/phase0/findings.md](docs/phase0/findings.md). No production code yet.

## Layout

| Path | What |
|---|---|
| `docs/phase0/` | Findings, pin map, UVSC results, backend decisions |
| `spikes/uvsim-script/` | µVision debug-script experiments E1–E6 (`*.ini`, `*-run*.out`) |
| `spikes/uvsc/` | Throwaway native C++ UVSC spike and its results |
| `reference/` | Generated copies of the reference firmware (never edit; regenerate) |
| `scripts/` | Reproduce everything (PowerShell) |
| `third_party/uvsc/` | Keil AN198 (UVSC header and docs). **Keil licence: keep private** |

## Reproduce

```powershell
scripts\make-reference.ps1                      # Keil Blinky → reference\blinky (simulator-configured) + build
scripts\snapshot-project.ps1 -Source <bit-band test project folder> -Project Bitband -Name bitband-test
scripts\snapshot-project.ps1 -Source <joystick/LED demo project folder> -Project Blinky -Name joystick-led-demo
scripts\run-sim.ps1 -Ini spikes\uvsim-script\e2-gpio-observe.ini -TimeoutSec 240
scripts\run-uvsc-spike.ps1                      # builds with MSVC (vcvars64) + runs the UVSC spike
```

**Requirements:**
- Keil µVision 5.35 at `C:\Keil_v5`
- `Keil.LPC1700_DFP 2.6.0`
- VS 2026 Build Tools (MSVC 14.51, bundled CMake and Ninja)
- The bit-band test and joystick/LED demo µVision projects (any location), for the snapshots
- Keil AN198 `apnt_198.zip` in `third_party/uvsc/`, for the UVSC spike (see its `PROVENANCE.md`); the build generates the UVSC declarations from it
