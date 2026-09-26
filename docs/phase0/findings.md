# Phase 0 findings

**Date:** 2026-09-25.

**Scope:** feasibility only. No production code. The experiments are reproducible with the scripts in `scripts/` and the INIs in `spikes/`.

**Experiment index**

| ID | What | Firmware | Result |
|---|---|---|---|
| E1 | Headless debug session, VTREG list | Keil Blinky | PASS |
| E2 | GPIO write observation, ×2 runs | Keil Blinky | PASS, identical |
| E3 | Simulation speed / `swatch` | Keil Blinky | PASS (slow) |
| E4 | INT0 injection via `PORT2`, ×2 runs | Keil Blinky | PASS, identical |
| E5 | Bit-band + masking observation | bit-band test | PASS; catches its alias bug |
| E6 | GLCD SPI byte tap | joystick/LED demo | PASS |
| UVSC | Native C++ control over UVSC, ×2 runs | bit-band test | PASS; 34/34 steps, identical |

## 1. Environment confirmation

Everything from the earlier inspection was re-confirmed in use:
- **Toolchains:** MSVC 14.51 + CMake 4.3.1 + Ninja 1.13.2 (through `vcvars64.bat`), and µVision 5.35 with ARMCC 5.06u7.
- **Packs:** `LPC1700_DFP 2.6.0`, `CMSIS 5.8.0`, `MDK-Middleware 7.13.0` (`Board_*.h`).
- **UVSC:** `UVSC64.dll` v2.29.

New facts:
- **License:** µVision runs as **"Non-Commercial Use License"**, and the simulator prints *"Running with Code Size Limit: 32K"*. Largest firmware used: Keil Blinky, 13.3 KB code.
- **Pack files are installed read-only.** Copies must clear the attribute (`make-reference.ps1` does).
- **An interactive µVision session** (with the bit-band test project open) was running throughout. It does not listen on a UVSOCK port, and no experiment touched it. All experiment sessions run hidden (`-j0`).
- **Added: `third_party/uvsc`**, the official Keil AN198 package, downloaded during Phase 0 (see `third_party/uvsc/PROVENANCE.md`).
- **Nothing was installed.**

## 2. MCB1700 pin/peripheral findings

Full table with sources: [mcb1700-pin-map.md](mcb1700-pin-map.md). Summary:
- **LEDs:** P1.28, P1.29, P1.31, P2.2–P2.6 (outputs). **Verified in the simulator:** `LED_Initialize` touched exactly those pins in that order (E2).
- **Joystick:** P1.20 (center), P1.23 (up), P1.24 (right), P1.25 (down), P1.26 (left). Inputs, active-low.
- **INT0:** P2.10, input, active-low (verified by injection in E4).
- **GLCD:** SSP1 (P0.7 SCK, P0.8 MISO, P0.9 MOSI) plus CS on GPIO P0.6. 240×320 RGB565. Two controller init paths.
- **Potentiometer:** P0.25 = AD0.2. **Speaker:** P0.26 = AOUT.
- **⚠ LED polarity is disputed.**
  - Keil's board library and the legacy `LED.c` both drive **high for on**.
  - The bit-band test code and its comment, and the `LED_On(0) ≡ FIOCLR` example in the Phase 0 goals, assume **active-low**.
  - The real board must settle it. The board model will hold it as a single constant.
- **The two board-API generations encode the joystick differently.** New `JOYSTICK_LEFT = 1<<0 … DOWN = 1<<4`; legacy `KBD_SELECT = 0x01, UP = 0x08, RIGHT = 0x10, DOWN = 0x20, LEFT = 0x40`.
- **The Keil Blinky `Abstract.txt` is wrong for this board** ("LED PD_10", "BUTTON P4_0"). The code uses LED0 = P1.28 and INT0 = P2.10.

## 3. Firmware access styles

Firmware inspected:
- Keil DFP: `Blinky`, `Blinky_ULp`, `Demo`, and the board drivers.
- **Joystick/LED demo firmware:** `Blinky_ULp`-based, with the legacy `LED.c`/`KBD.c`/`GLCD_SPI_LPC1700.c`.
- **Bit-band test firmware:** the C source built by the current project, plus a second variant that also writes to the GLCD.

| Class | Examples | Where |
|---|---|---|
| **A. Board API / library** | New: `LED_On/Off/SetOut`, `Buttons_GetState`, `Joystick_GetState`, `GLCD_DrawString`, `ADC_GetValue`. Old/legacy: `LED_Init/On/Off/Out`, `KBD_Init/get_button`, `GLCD_Init/Clear/DisplayString/Bargraph` | Keil Blinky/Demo; joystick/LED demo (mixes the legacy API with the new `Board_ADC.h`) |
| **B. CMSIS register access** | `LPC_GPIO1->FIOSET = 1<<28`, `FIOCLR`, `FIODIR \|=`, `FIOPIN \|= mask` / `&= ~mask` (legacy `LED_On`/`LED_Off` are read-modify-writes of FIOPIN), `LPC_SSP1->DR = byte; while(!(SR & RNE));`, `LPC_ADC->ADCR \|= 1<<24`, `LPC_SC->PCONP \|=`, `LPC_PINCON->PINSEL3 &= ~…` | legacy drivers, bit-band test, and the Keil drivers underneath the board API |
| **C. Bit-band access** | Computed at run time from the register address: `(x & 0xF0000000) \| 0x02000000 \| ((x & 0xFFFFF) << 5) \| (bit << 2)` | bit-band test |
| **D. Direct address / pointer** | `*(volatile unsigned long *)0x233806EC` (meant for P1.28), `0x23380A88` (P2.2 alias), `0x42680060` (ADCR bit-24 alias), ITM `0xE0000000`, `DEMCR 0xE000EDFC` | bit-band test, joystick/LED demo (`fputc` → ITM) |
| **E. Other** | CMSIS-RTOS2 (RTX5) threads, `osDelay`, thread flags; `SysTick_Config` + `SysTick_Handler`; ADC IRQ; CMSIS-Driver `Driver_SPI1` | Keil Blinky (RTX5), joystick/LED demo, Keil GLCD driver |

**The board API bottoms out in class B.**
- Keil's `LED_On` → `GPIO_PinWrite` → `LPC_GPIO(n)->FIOSET/FIOCLR`.
- The legacy `LED_On` → `FIOPIN |=`.

So `LED_On(0)` and `LPC_GPIO1->FIOSET = 1u<<28` really are the same operation one layer apart. The semantic convergence point is the **GPIO register model** (FIODIR/FIOMASK/FIOPIN/FIOSET/FIOCLR plus bit-band), not the LED.

**⚠ The bit-band test firmware contains a real bug.** Its direct-alias write for the P1.28 LED uses `0x233806EC`, which is the alias for **P1.27**; P1.28's alias is `0x233806F0`.
- **Observed in the simulator (E5 and UVSC):** after the direct-alias "on" step, P2.2 goes low but **P1.28 does not change**, and P1.27 is an input, so the write is invisible.
- This is a natural acceptance case: a correct B2/B3 must report "LED0 (P1.28) unchanged in direct mode".

## 4. Reference firmware build result

| | Keil Blinky (reference) | Bit-band test (snapshot) | Joystick/LED demo (snapshot) |
|---|---|---|---|
| Script | `scripts/make-reference.ps1` | `scripts/snapshot-project.ps1 -Source <path> -Project Bitband -Name bitband-test` | `… -Source <path> -Project Blinky -Name joystick-led-demo` |
| Command | `UV4.exe -b "<ws>\reference\blinky\Blinky.uvprojx" -t "Debug" -j0 -o "<ws>\reference\blinky\build.log"` | `UV4.exe -r "<ws>\reference\bitband-test\Bitband.uvprojx" -j0 -o …\build.log` | `UV4.exe -r …\Blinky.uvprojx -j0 -o …` |
| Exit | 0 | 0 | 0 |
| Log tail | `".\Debug\Blinky.axf" - 0 Error(s), 0 Warning(s).` Code=13322 RO=558 RW=5124 ZI=1800 | `".\Objects\Bitband.axf" - 0 Error(s), 0 Warning(s).` Code=2200 | `".\SWO_Trace\Blinky.axf" - 0 Error(s), 0 Warning(s).` Code=5290 |
| AXF | `reference\blinky\Debug\Blinky.axf` (322,948 B) | `reference\bitband-test\Objects\Bitband.axf` | `reference\joystick-led-demo\SWO_Trace\Blinky.axf` |
| Compiler | ARMCC 5.06 update 7 (the project asks for u6; µVision used the installed u7) | ARMCC 5.06u7 | ARMCC 5.06u7 |
| Target / device | `Debug` / LPC1768 | `Target 1` / LPC1768 | `SWO Trace` / LPC1768 |
| Simulator | Patched (see below) | Already `DARMP1.DLL -pLPC1768`, `uSim=1` | Already `DARMP1.DLL -pLPC1768`, `uSim=1` |

**Only these were changed in the copies:**
- **Keil Blinky, stock settings:** `uSim=0`/`uTrg=1` (ULINK2 hardware), `SimDlgDll=DCM.DLL -pCM3` (core-only).
- **Patched to:** `uSim=1`/`uTrg=0`, `DARMP1.DLL -pLPC1768`, and `sIfile=.\sim.ini`.
- **Source of the simulator string:** copied from Keil's own `MCB1700\Blinky_ULp` project, and confirmed by both reference firmware projects using the same setting.
- **Bit-band test and joystick/LED demo:** only `sIfile` is set.
- **Untouched:** the installed pack and the original project folders (snapshots only).
- **Rebuilds:** the project snapshots use `-r` (full rebuild), so they don't reuse stale objects.

## 5. UVSC connection result

**PASS in launch-and-connect mode.** Full detail: [uvsc-results.md](uvsc-results.md).
- **Setup:** launch `UV4.exe -j0 -s4830 <project>`, wait for LISTEN, then `UVSC_OpenConnection` (existing session). It connects in about 8 s from launch. `GEN_UVSOCK_VERSION` reports 2.29.
- **`UVSC_Init` only accepts a port range of exactly 10** (max − min == 9); anything else returns FAILED. Undocumented, deterministic.
- **UVSC auto-start fails and orphans** the µVision it spawns.
- **The official AN198 package ships `UVSC_C.h` but not `UVSOCK.h`.** Calls needing its structs (memory read/write, VTR set, exec command, project load, async message decoding) were **not made**.

## 6. AXF load / debug result

**PASS.**
- **`UVSC_DBG_ENTER` does a lot:** loads the AXF (`Load "…Bitband.axf"` appears on the wire), runs the project INI, resets and runs to `main`.
- **Execution control works:** `RUN_TO_ADDRESS` (×8), `START` / `STATUS` / `STOP`, `DBG_RESET`, `DBG_EXIT`, `CloseConnection(terminate)`. The process exits and no UV4 is orphaned.
- **Headless batch mode also works without UVSC:** `UV4 -d <proj> -j0` with an INI that ends in `EXIT` finishes on its own in 5 s. The help says `EXIT` does not work in debug scripts; here it did.
- **Commands are asynchronous.** A command issued while `DBG_ENTER`'s own run-to-main is in flight is lost; that led to `HardFault_Handler` in two attempts. The spike settles 3 s after `DBG_ENTER`/`DBG_RESET`. The correct handshake is the async `DBG_STOP_EXECUTION`/`BPREASON` message, which needs `UVSOCK.h`.

## 7. GPIO observation result

**PASS. The LPC1768 peripheral simulator models GPIO, including bit-band aliases.**

| Firmware | Register | Before | After | Sim time | Board meaning |
|---|---|---|---|---|---|
| Keil Blinky (E2) | FIO1PIN 0x2009C034 bit 28 | 0 (after init) | 1 at `LED_On` | write at 63,961 states (0.64 ms); 1 on readback at 0.25 s (E3) | LED0 on (if active-high) |
| | | 1 | 0 at `LED_Off` | 50,056,994 states (500.6 ms) | LED0 off |
| | | 0 | 1 at `LED_On` | 100,057,544 states (1000.6 ms) | LED0 on |
| Bit-band test (E5, UVSC) | FIO1PIN bit 28 / FIO2PIN bit 2 | 1/1 | 0/0 after masking "on" | 9,101 | LED0/LED3 "on" per the firmware's active-low assumption |
| | | 1/1 | 0/0 after computed bit-band "on" | 20,014,175 | computed alias works |
| | | 1/1 | **1**/0 after direct alias "on" | 40,018,080 | the bug: P1.28 untouched |

**Mechanisms that work**
1. **State at a stop point:** `_RDWORD(FIOnPIN)` or the `PORTn` VTREG, read at an execution breakpoint or `RUN_TO_ADDRESS` stop. Exact.
2. **Write hooks:** `BA WRITE <GPIO block>,len,1,"fn()"` fires on every GPIO write, including **bit-band alias writes, which are reported as GPIO-block accesses**. It runs without halting.

**Write-hook caveats**
- The hook runs **before** the write is applied, so it sees pre-write state.
- It fires **2× (SET) or 3× (CLR)** per firmware write.
- It fires on **both** the SET and the CLR hook for one write, so hooks **cannot tell which register or style was written**. The help warns that the simulator doesn't separate firmware accesses from its own.
- **Conclusion:** B3 should treat hooks as "GPIO touched at state *s*" and take pin state from the next stop or sample. It cannot reconstruct the written value or the access style.

## 8. Input injection result

**PASS through the simulator; UVSC path not yet tested.**
- **What worked (E4):** a `SIGNAL` function in the INI did `PORT2 &= ~(1<<10)` (INT0 pressed) at 0.2 s sim time and released it at 0.3 s.
- **Firmware reaction:** unmodified Keil Blinky read it through `Buttons_GetState` and stopped blinking. The `LED_On` due at 1.0 s never happened, and LED0 = 0 at 1.25 s. Two runs were identical.
- **Timing effect:** the press shifted a later LED edge by 2 states (50,056,994 → 50,056,996). That's input-dependent timing, reproducible, not noise.
- **Mechanism:** pin VTREGs (`PORT0..4`) are the documented input path. Writing `FIOPIN` does not drive an input pin.
- **Over UVSC:** the equivalents are `UVSC_DBG_VTR_SET` or `UVSC_DBG_EXEC_CMD("PORT2 = …")`. Both need `UVSOCK.h` structs and were not called. INI-defined SIGNAL functions do run in UVSC-driven sessions.

## 9. Deterministic execution result

**PASS. Simulated time is exact and repeatable; host timing never enters the result.**

| Mechanism | Evidence | Verdict |
|---|---|---|
| `states` counter (CPU cycles) | 0.25 s `swatch` = 24,968,655 states; the `delay(1000000)` NOP loop costs ~10.0 M states each time | exact |
| Stop at sim time (`SIGNAL` + `swatch(sec)` + `_break_=1`) | E2/E3/E4 stop at identical states every run | deterministic |
| Breakpoint / run-to-address (`BS`, `G,addr`, `UVSC_DBG_RUN_TO_ADDRESS`) | UVSC ×2 sessions: identical 8-stop sequences; `DBG_RESET` reproduces states 9101 | deterministic, **most reliable** |
| Free run + host stop (`START`/`STOP`) | Stops at an arbitrary PC, yet later breakpoint results unchanged | fine for control, never for assertions |
| Repeat runs (script and UVSC) | E2, E4 and UVSC outputs byte-identical across runs | deterministic |

**So an automated test can mean exactly:** `reset → run until address X / breakpoint / sim time T → expect FIO1PIN.28 == v`.

**Caveats**
- `seconds` is an *undefined identifier* for LPC1768. Sim time = `states` / `CCLK`, and CCLK is 12 MHz before the PLL and 100 MHz after, so the conversion is piecewise.
- **Speed:** about 1.7–2.0 M states per wall second, i.e. **50–60× slower than real time** at 100 MHz. 1 s of firmware time takes about a minute; the joystick/LED demo's `GLCD_Init` alone is 1.3 s of sim time.

## 10. Timing / wait findings

| Firmware | Wait mechanism | B1 host shim | B2 register model | B3 µVision |
|---|---|---|---|---|
| Keil Blinky | CMSIS-RTOS2 (RTX5) `osDelay(500)`, threads, thread flags; SysTick 1 kHz | Needs an RTOS2 shim (threads → fibers, `osDelay` → sim-time wait). Real work; defer | Same as B1 (RTOS is above the registers) | Exact: 500.0055 ms between edges |
| Joystick/LED demo | `SysTick_Config(SystemCoreClock/100)` + `SysTick_Handler` (10 ms ISR) drives the LED chaser; `while(1)` polls ADC and joystick; ADC completes by IRQ | Must run the ISR on sim time and yield in the poll loop. Every register access / API call is the natural yield point | Same yield-at-register-access rule; SysTick + ADC must be modeled | Exact |
| Bit-band test | `delay(n)`: `volatile` counter + `__NOP()` busy loop, no register access inside | **No yield point inside the loop.** Run it natively (instant) and advance sim time by a declared cost, or intercept `delay` by name. Either way the time is an estimate | Same | Exact: ~10 states per iteration |
| GLCD drivers | Busy-loop `delay(cnt)` (legacy driver) plus SSP status polling | Intercept at the GLCD API (text level) | SSP polling becomes a register read that yields | Exact but slow |

**Consequences**
- B1/B2 time is **logical**: correct ordering, estimated durations.
- B3 time is **cycle-level**: the `states` counter.
- Scenario timing assertions need a declared tolerance on B1/B2. B3 is the timing truth short of real hardware.
- No scheduler was built.

## 11. GLCD feasibility (B3)

**MODERATE.**
- **Evidence (E6, joystick/LED demo):**
  - A SIGNAL function blocking on `wwatch(SSP1_OUT)` captured **every byte**: 306 (init), 153,640 (`GLCD_Clear`: 240×320×2 plus setup), 16,160 (the first 20-char `GLCD_DisplayString` = 20 × (768 pixel + 40 setup) bytes).
  - The framing decodes by hand: `70 00 50 | 72 00 00` = register 0x50 ← 0x0000, then 0x51 ← 0x0017, 0x52 ← 0, 0x53 ← 0x000F (a 16×24 window at the origin), `70 00 22 | 72 …` = GRAM pixel stream (0x001F = blue background).
  - CS (P0.6) was low throughout.
- **Why not EASY:**
  - Text is rasterized in firmware, so B3 sees pixels, not characters. Recovering text needs a controller decoder, a 240×320 framebuffer and glyph matching against the firmware's own font tables.
  - There are two controller paths (HX8347-D vs other). The simulator returns ID 0 and takes the non-HX8347 path, while the real board takes whichever path its panel reports.
  - Full-screen clears are 150 KB each at 50–60× slowdown.
- **Why not DIFFICULT:** the stream is complete, deterministic and byte-exact, the protocol is small, and the fonts ship with the firmware.
- **Text through B1** (intercept `GLCD_DisplayString` / `GLCD_DrawString`) is **EASY** and remains the V1 plan. B3 pixel reconstruction is post-V1.

## 12. Limitations discovered

**µVision debug scripts (INI)**
1. `LOG >` rejects paths containing spaces (error 10). Logs go to `%TEMP%`.
2. A syntax error aborts the INI and µVision **waits forever**. Every B3 session needs an external watchdog.
3. A SIGNAL parameter named `t` is a syntax error (it clashes with the `T` command).
4. `$` inside `printf` is undefined.
5. `LOG` is buffered until `LOG OFF`, so a killed session loses its log.
6. Keil Blinky's Debug target enables Event Recorder, which touches DWT (0xE0001000) and faults with *error 65: no 'read' permission*. The fix is `MAP 0xE0001000,0xE0001FFF READ WRITE` in the INI, with no firmware change.

**Simulator**
7. `seconds` is undefined for LPC1768 (use `states`).
8. Speed is 50–60× slower than real time.
9. Write hooks: pre-write, multiplicity 2–3, no SET/CLR/alias discrimination.
10. MDK-Lite 32 KB code limit.

**UVSC**
11. `UVSC_Init` accepts only a 10-port range.
12. Auto-start fails and orphans UV4.
13. `UVSOCK.h` is missing from the official package.
14. Commands are asynchronous, and `DBG_STATUS` polling is racy right after run commands.
15. `-j0` sessions are invisible. Visible sessions pop up over whatever else is on screen, so B3 must always run hidden.

**Host-side B2 (analysis, not an experiment)**
16. Firmware that dereferences **numeric addresses** (the bit-band test's `0x233806F0`, `0x42680060`) cannot be redirected to a host register model by swapping `LPC17xx.h`; only `LPC_GPIOn->…` accesses can.
   - Catching numeric-address writes on the host needs address-space trapping (reserve those ranges, handle the access fault, emulate the store) or compiler instrumentation.
   - Neither is proven. This is the hardest part of B2.

## 13. Unknowns remaining

1. **An official `UVSOCK.h`.** Needed for UVSC memory read/write, `VTR_SET`, `EXEC_CMD`, `PRJ_LOAD` and async stop decoding. Options: ask Keil/Arm support, check a newer MDK install, or explicitly accept the Arm-employee pyUVSC definitions (not done).
2. **LED polarity on the real board**, active-high vs active-low (sources conflict).
3. **Q2: is an MCB1700 + ULINK available?** ULINK drivers are installed; no hardware was touched.
4. Which GLCD controller the real panel has (HX8347-D vs other), and the purpose of P4.28/P4.29 in the legacy driver.
5. Why UVSC auto-start fails.
6. Whether the simulator raises **GPIO/EINT0 interrupts** from injected pin changes. Blinky polls, so this was not exercised.
7. ADC input injection (`AIN2`), timers (TIM0–3), UART VTREGs: listed, not exercised.
8. Why the bit-band test's `ADCR` writes and bit-band writes had no visible effect in the simulator (the ADC is unpowered because PCONP bit 12 isn't set; plausibly correct, not investigated).
9. More reference firmware (timers, interrupts, RTOS). Only these two projects exist locally.
10. **Host-side mechanism for numeric-address MMIO in B2** (trap vs instrumentation). Needs a Phase 1 spike.
11. The exact cause of the write-hook multiplicity.
