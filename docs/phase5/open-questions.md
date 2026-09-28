# Phase 5: open questions

What the RTOS, USB/audio and desktop work leaves uncertain, and where each question stands.

**Evidence:**
- RTX 4.82's own sources and configuration templates (CMSIS 5.8.0 pack);
- Keil's USB Audio example, USB device driver and DAC driver (LPC1700_DFP 2.6.0);
- UM10360 chapters 11 (USB device) and 30 (DAC);
- the Phase 0–4 experiments.

No simulator experiment was run for Phase 5.

| # | Question | Outcome |
|---|---|---|
| 1 | Does the kernel schedule as RTX does? | **RESOLVED** from RTX's source; **DEFERRED** against a running RTX |
| 2 | How long do RTX kernel calls take? | **DEFERRED** |
| 3 | Host code takes no processor time | **BOUNDED** |
| 4 | Interrupt latency and PendSV ordering | **BOUNDED** |
| 5 | RTX features not modelled | **BOUNDED** |
| 6 | USB device controller fidelity | **BOUNDED** |
| 7 | Keil's USB audio middleware cannot run on the host | **BOUNDED** |
| 8 | Audio output below the DAC | **BOUNDED** |
| 9 | USB clock agreement and rate feedback | **BOUNDED** |
| 10 | Layering: trace text names MCB1700 and LPC1768 things | **BOUNDED** |
| 11 | Firmware statics between runs | **BOUNDED** |
| 12 | Fibers are Windows-only | **BOUNDED** |
| 13 | Real-hardware confirmation | **DEFERRED** |

## 1. Scheduling fidelity: RESOLVED from source, DEFERRED against a running RTX

**How it was built:** every list operation that decides who runs is a port of the RTX 4.82 function named in [rtos.md](rtos.md), tested in `RtosKernel.*`. Covered:
- where a preempted or readied thread goes;
- round-robin timing;
- simultaneous wake order (latest first);
- yield only to equals;
- the handover and restoration rules for mutexes;
- the main-thread start-up;
- the tick restart when main starts the kernel again.

**Not yet done:** these results have not been compared with RTX running in µVision's simulator.

**To close:** run the workloads (`firmware/rtos`) under RTX in the simulator and compare the Event Viewer's switch times. The simulator's own timing adds kernel-call time (question 2).

## 2. Kernel-call cost: DEFERRED

- **Model:** RTX's SVC entry, service and PendSV switch take processor time, which the model sets with `Config::call_cycles`, default 0.
- **Consequence:** with 0, workloads that compute through `latasim_consume_*` get exact times, and firmware that only calls the kernel needs a nonzero value.
- **Evidence:** no local measurement exists. The yield scenario assumes 250 cycles (2.5 µs at 100 MHz) per call, an order-of-magnitude figure, not a measurement.

**To close:** measure a yield loop's cycles per iteration in the simulator, as E11 measured the handler's LED stores.

## 3. Processor time of host code: BOUNDED

- **Model:** firmware bodies run on the host in zero virtual time; only `latasim_consume_*` and kernel-call cost use processor time.
- **Consequences:**
  - timelines show modelled work, not instruction timing;
  - a busy loop with no kernel call never gives up the processor and hangs the host thread;
  - stock firmware whose "work" is a counting loop (`for (;;) counta++;`) must use `latasim_consume_us` to be scheduled as it would be.
- **Sufficient for:** scheduling, synchronisation and timing-design exercises, which state their computation times.

## 4. Interrupt latency and PendSV ordering: BOUNDED

- **Model:** handlers take no time (Phase 4). Work that interrupts queue for the kernel (`osSignalSet` from a handler, timer messages from the tick) runs after all pending handlers have returned, which is where RTX's lowest-priority PendSV would run.
- **Not modelled:**
  - a higher-priority interrupt arriving *during* PendSV (there is no nesting);
  - `OS_FIFOSZ` overflow of the post-service queue.

## 5. RTX features not modelled: BOUNDED

- **Refused with an error:** semaphores, memory pools, message and mail queues, `osWait`, `os_suspend`/`os_resume` (tickless idle). Calling any of them stops the calling thread with an error naming the call.
- **Not modelled:** stack sizes and overflow checks, `OS_TASKCNT` limits, and the `os_error` and `os_idle_demon` hooks.
- **None of the Phase 5 workloads uses them.**
- **Adding one** means porting its RTX source file the same way (`rt_Semaphore.c`, `rt_Mailbox.c`, `rt_MemBox.c`).

## 6. USB device controller fidelity: BOUNDED

- **Model:** the register interface in slave mode, as Keil's driver uses it, from UM10360 chapter 11. Keil's driver runs on it unchanged.
- **Transfers:** whole packets at frame granularity.
- **Not modelled:**
  - DMA;
  - double buffering;
  - NAK interrupts;
  - error codes (always "no error");
  - the address taking effect only after the status stage (it takes effect at once; one device on the bus makes no difference);
  - suspend/resume timing;
  - the OTG and host controllers.
- **One reading:** that an isochronous read with no packet gives PKT_RDY with length 0 follows 11.14.2 ("the control logic will fetch the packet length ... and set the PKT_RDY bit"); Keil's driver depends on it.
- **Not checked** against the simulator's USB model or a board.

## 7. Keil's USB audio middleware: BOUNDED

- **The limit:** the Keil USB Audio example's class layer is MDK middleware shipped only as ARM object code, and the example uses CMSIS-RTOS2. Neither can run on the host.
- **What runs instead:**
  - Keil's USB device driver (`USBD_LPC17xx.c`), OTG/pin driver and DAC driver, unchanged;
  - a representative speaker firmware with the example's format, endpoint, buffering and scaling.
- **Not reproduced:** the middleware's own class-request handling (host volume/mute). Volume comes from the potentiometer instead.

## 8. Audio below the DAC: BOUNDED

- **Model:** the speaker records DACR values with their virtual times, and PCM export converts them to signed 16-bit.
- **Not modelled:**
  - the DAC's settling time and bias current;
  - the MCB1700's filter and amplifier;
  - the speaker.
- **Effect:** what a person hears on the PC is the DAC sequence played at 32 kHz, not the board's acoustic output.

## 9. USB clocks and rate feedback: BOUNDED

- **In the model,** the host sends 32 samples every 1 ms frame and TIMER0 plays one every 3125 cycles: exactly equal rates. Playback therefore neither drifts nor underruns while the host plays.
- **On hardware** the two clocks differ slightly, which is why Keil's example adjusts TIMER0's period against the buffer level. The representative firmware omits that adjustment.

## 10. Layering of trace text: BOUNDED

- **What is generic:**
  - the RTOS kernel (`src/rtos`) and USB host (`src/devices`), which never include LPC1768 or MCB1700 headers (`Extensibility.*` builds and runs them on a fake platform and a scripted device);
  - the trace's event structure (`trace/trace.hpp`).
- **What is not:** the trace's *text* (`describe`, `to_string`) names LPC1768 registers and MCB1700 LEDs, so it depends on those layers.
- **For a second board,** a naming hook would be needed. The events themselves need no change.
- **Other items to move then:**
  - the GLCD controller model (`boards/mcb1700/glcd`), which is an ILI9320-family controller and would move to `devices/` if another board used it;
  - the workbench's register groups, which are LPC1768-specific.

## 11. Firmware statics between runs: BOUNDED

- **The problem:** host-compiled firmware keeps its statics for the life of the process. This includes Keil's USB driver's "initialized" and "powered" flags, IRQ.c's counters and the workloads' globals.
- **What restores them:**
  - each scenario's `reset_statics`, before a session;
  - each scenario's teardown, while still bound; for example the USB speaker is stopped so the driver's flags clear.
- **Scope:** only firmware Latasim knows about is covered. New firmware needs its own reset or its own process.

## 12. Fibers are Windows-only: BOUNDED

- **The limit:** thread switching uses Win32 fibers behind `rtos::Fiber`.
- **Unaffected:** scheduling policy, which is portable.
- **Another host** needs its own `Fiber`, for example ucontext or a coroutine library.

## 13. Real hardware: DEFERRED

- **Status:** no Phase 5 result has been compared with a real MCB1700 running RTX or USB audio.
- **Worth checking on a board:**
  - the round-robin and inversion timelines with RTX;
  - enumeration and streaming with a PC;
  - the potentiometer-to-volume path.
