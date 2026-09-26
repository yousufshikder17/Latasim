# UVSC results (Phase 0)

**Question (Q4): can a native C++ program reliably control µVision through UVSC?**

**Answer: yes, with two caveats.**
1. UVSC must connect to a µVision **that we launch**. Auto-start fails.
2. Every command is **asynchronous**. Reliable sequencing needs the async stop messages, which need `UVSOCK.h`, the one official header that is missing.

Code: `spikes/uvsc/uvsc_spike.cpp` (declarations: `uvsc_min.h`, generated at build time by `scripts/gen-uvsc-min.ps1` from a local copy of `UVSC_C.h`). Runner: `scripts/run-uvsc-spike.ps1`. Raw outputs are in `spikes/uvsc/results/` (`run1.txt`, `run2.txt`, and the UVSOCK wire logs `run*-wire.log`).

## Where the declarations came from

- **Package:** Keil Application Note 198, *Using the uVision Socket Interface* (KAN198 v1.1, Arm, published 2025-01-16; content revised July 2018).
  - URL: `https://documentation-service.arm.com/static/678911ba3f2a9a07789e22e7`
  - File: `apnt_198.zip`, sha256 `04b06be4…8c7`, in `third_party/uvsc/` (see `PROVENANCE.md`)
  - Downloaded on 2026-09-25.
- **What it contains:** `UVSC_C.h`, UVSC.dll/UVSC64.dll **2.30** plus import libs, Keil's `UVSC_Tester`/`UVSOCK_Tester` MFC sources, and Doxygen HTML docs.
- **What it's missing: `UVSOCK.h`.** `UVSC_C.h` does `#include "UVSOCK.h"`, and Keil's own sample sources include it too.
  - The Doxygen pages for every UVSOCK struct (`AMEM`, `VSET`, `EXECCMD`, `PRJDATA`, `UVSOCK_CMD`, …) are **empty**.
  - So the official package does not define the struct layouts, and **they were not guessed**.
- **What the spike declares:** only functions whose parameters are defined in `UVSC_C.h` itself or are plain scalars. `scripts/gen-uvsc-min.ps1` extracts them from a local copy of the header at build time, so no Keil content is committed. The script documents each substitution:
  - `xBOOL`→`int` (by value)
  - `UINT`→`unsigned`
  - `xU64`→`unsigned long long`
  - the callback data pointer → `void*`
- **Unofficial copies exist but were not used:**
  - `UVSOCK.h` in third-party GitHub repos.
  - PyPI `uvsc` / `github.com/MatthiasHertel80/pyUVSC`, by an Arm employee but on a personal account (Apache-2.0).
- **DLL used:** the installed `C:\Keil_v5\UV4\UVSC64.dll` **v2.29**, which matches µVision 5.35. It reports UVSC library 229 and UVSOCK interface 229.

## Final spike run (bit-band test snapshot, run twice)

| # | Operation | Result | Notes |
|---|---|---|---|
| 1 | `UVSC_Init(4823, 4832)` | PASS | See the **port-range finding** below |
| 2 | Launch `UV4.exe -j0 -s4830 <project>` | PASS | Hidden. The project is given on the command line because `PRJ_LOAD` needs `PRJDATA` |
| 3 | Wait for port 4830 LISTEN | PASS | 5.8–6.3 s after launch |
| 4 | `UVSC_OpenConnection` (existing session) | PASS | ~2.0 s |
| 5 | `UVSC_GEN_UVSOCK_VERSION` | PASS | 2.29 |
| 6 | `UVSC_DBG_ENTER` (simulator, `DARMP1 -pLPC1768`) | PASS | Loads the AXF, runs the project INI, resets, then runs to `main` on its own |
| 7 | Settle 3 s, `DBG_STATUS` = stopped | PASS | See the **async finding** below |
| 8 | `UVSC_DBG_RUN_TO_ADDRESS(delay)` × 6 | PASS ×6 | Each stop is one LED operation later. 4.3–5.3 s wall per 10 M states |
| 9 | `DBG_START_EXECUTION` → `DBG_STATUS` = 1 → `DBG_STOP_EXECUTION` | PASS | Stopped at a host-timed PC (0x7BE, `HIT_ESC`) |
| 10 | `RUN_TO_ADDRESS(BarrelShift)` | PASS | The same 60,019,170 states as without the pause |
| 11 | `UVSC_DBG_RESET` + settle + `RUN_TO_ADDRESS(delay)` | PASS | Reproduces **states=9101** exactly |
| 12 | `UVSC_DBG_EXIT` | PASS | |
| 13 | `UVSC_CloseConnection(h, terminate=1)` | PASS | |
| 14 | `UVSC_UnInit` | PASS | |
| 15 | UV4 process exits; no new UV4 left | PASS | Exited within 15 s; `orphan_uv4=0` |

Both runs: 34 PASS, 0 FAIL. The stop-reason and observation sequences in the two wire logs are **identical**, apart from the deliberately host-timed stop in step 9.

Observed values match the debug-script experiment E5 to the exact state count:

| Stop | states | P1.28 | P1.27 | P2.2 | After |
|---|---|---|---|---|---|
| 1 | 9,101 | 0 | 1 | 0 | masking "on" |
| 2 | 10,009,142 | 1 | 1 | 1 | masking "off" |
| 3 | 20,014,175 | 0 | 1 | 0 | computed bit-band "on" |
| 4 | 30,014,237 | 1 | 1 | 1 | computed bit-band "off" |
| 5 | 40,018,080 | **1** | 1 | 0 | direct alias "on": P1.28 unchanged, the firmware's alias bug |
| 6 | 50,018,122 | 1 | 1 | 1 | direct alias "off" |
| final | 60,019,170 | 1 | 1 | 1 | `BarrelShift` |
| after reset | 9,101 | 0 | 1 | 0 | reproduces stop 1 |

## Failures found on the way (kept, not hidden)

1. **`UVSC_Init` port range.**
   - Only `uvMaxPort − uvMinPort == 9` succeeds. That was measured in fresh processes: (4823, 4832), (5101, 5110) and (20000, 20009) pass.
   - (4823, 4899), (4823, 4833), (5101, 5111), (1, 65535) and (20000, 21000) return `UVSC_STATUS_FAILED` (1), every time.
   - The header only says the range must *accommodate* `UVSC_MAX_CLIENTS` (10). Re-`Init` after `UnInit` works.
2. **Auto-start (`pPort = UVSC_PORT_AUTO`, `uvCmd = UV4.exe`) fails.**
   - UVSC spawns `UV4.exe -j0 -s4823`, the port listens, and a connection is made and dropped (TIME_WAIT). After ~8–9 s it returns `UVSC_STATUS_FAILED`.
   - **The spawned UV4 is left running (an orphan).** Same result on the repeat.
   - Cause unknown. Suspects: µVision opens the *last used* project in that mode, or there's a handshake timeout.
   - Also, a project path cannot be given there. So B3 launches µVision itself.
   - `uvCmd` with a quoted project appended fails immediately and spawns nothing.
3. **Async race (attempts 1 and 2).**
   - `DBG_ENTER` and `DBG_RESET` return before their internal run-to-main has started, and `DBG_STATUS` reads "stopped" at that moment.
   - A `RUN_TO_ADDRESS` sent then was lost (the wire shows `[???]`). The target ran from `main` without stopping and ended in **`HardFault_Handler` (PC 0x182)**. Same result when repeated with a status wait.
   - Fixed in the spike with a 3 s settle, which is **host time used only for µVision's own startup**.
   - The correct fix is to wait for the async `DBG_STOP_EXECUTION` message (it carries `BPREASON`: reason, PC, breakpoint number). Its payload is a `UVSOCK_CMD`, which **needs `UVSOCK.h`**.
   - `DBG_STATUS` polled right after a run command can also report "stopped" before execution starts. Stops 1 and 12 show "stopped after 0.0 s", while the wire log shows the real stop landing later. µVision serialized the commands correctly, but a client can't rely on polling.
4. The `GEN_UVSOCK_VERSION` 0/0 seen in an early probe was a bug in the probe, not UVSC. The spike reads 2.29.

## What UVSC could not be tested for, and why

| Capability | UVSC function | Status | Blocker |
|---|---|---|---|
| Read memory / GPIO registers | `DBG_MEM_READ` | **not tested** | `AMEM` layout in `UVSOCK.h` |
| Write memory | `DBG_MEM_WRITE` | **not tested** | `AMEM` |
| Set pin VTREG (input injection) | `DBG_VTR_SET` | **not tested** | `VSET`/`TVAL` |
| Run a debugger command (e.g. `PORT2 &= …`, `BS …`) | `DBG_EXEC_CMD` | **not tested** | `EXECCMD`/`SSTR` |
| Load/switch project | `PRJ_LOAD` | worked around (command line) | `PRJDATA` |
| Decode async stop / command output | callback `UVSC_CB_ASYNC_MSG` | seen only in the UVSC text log | `UVSOCK_CMD` |

**Everything in this table was proven through µVision's own debug commands**, which UVSC runs inside the session:
- **Observation:** the INI's `BS`/`BA` hooks printed state, and that output arrived over the socket as `DBG_CMD_OUTPUT` messages.
- **Injection:** E4 wrote the `PORT2` VTREG from a `SIGNAL` function.
- **Execution:** `DBG_EXEC_CMD` is the documented way to send those same commands.

So `UVSOCK.h` is the only thing between the spike and full B3 control. It is not a capability gap in µVision.
