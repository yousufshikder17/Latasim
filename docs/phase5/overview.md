# Phase 5: V1, the virtual workbench

**Status: complete.**

Phase 5 makes Latasim usable for everyday MCB1700 development without the board. All of it runs deterministically in virtual time and can be watched and driven from a desktop workbench. It covers:
- CMSIS-RTOS v1 firmware on a model of Keil RTX 4's scheduler;
- USB audio from a virtual PC to the board's speaker;
- a media-center application that uses the GLCD, joystick, potentiometer, LEDs and USB together.

The physical board remains the final acceptance hardware; see [open-questions.md](open-questions.md) for what has not been checked on it.

## What can be developed virtually

| Kind of firmware | Where |
|---|---|
| Bare-metal MCB1700 firmware on Keil's board drivers, unchanged | Phases 2–4 |
| **CMSIS-RTOS v1 applications:** threads, priorities, round-robin, preemption, yield, delays, signals, mutexes with priority inheritance, virtual timers; with computation modelled by `latasim_consume_us` | [rtos.md](rtos.md) |
| **Scheduling designs** (rate-monotonic task sets, priority inversion and its fixes), with a run timeline and processor accounting | [rtos.md](rtos.md) |
| **USB device firmware on Keil's LPC17xx USB driver:** a USB audio speaker enumerated by a virtual PC, streaming PCM to the DAC and speaker | [usb-audio.md](usb-audio.md) |
| **Integrated applications:** GLCD menus, bitmaps, games, audio, joystick and potentiometer input | `firmware/media` |

## Architecture

```
firmware (Keil drivers unchanged; CMSIS-RTOS v1 and register-level application code)
   |
host execution: LPC17xx.h register proxies, CMSIS NVIC/SysTick, cmsis_os.h (RTX 4),
                Driver_SPI1 / Driver_USBD0, fibers for RTOS threads
   |
+---------------------+---------------------+----------------------+
| MCU (lpc17xx)       | board (mcb1700)     | RTOS (rtos)          |
| GPIO, NVIC, SysTick | LEDs, joystick, INT0| kernel: RTX 4 rules  |
| timers, ADC, EINT0  | potentiometer, GLCD | threads on fibers    |
| USB device, DAC     | speaker, USB port   | timeline, accounting |
+---------------------+---------------------+----------------------+
   |                         |
devices (generic): virtual USB audio host (PC), usb::Bus
   |
scenario / trace: virtual time, structured trace, workbench::Session and scenarios
   |
Qt workbench (src/ui/qt): observes and controls; holds no simulation state
```

**Layering**
- **Generic layers** (`rtos`, `devices`, `trace/trace.hpp`) include nothing from `lpc17xx` or `boards`. `Extensibility.*` runs the kernel on a platform that is only a tick counter, and the USB host against a scripted device.
- **The LPC1768 layer** implements the generic interfaces: `rtos::Platform` (`lpc17xx::RtosPort`) and `usb::Bus` (`lpc17xx::UsbDevice`).
- **The MCB1700 layer** wires pins to LEDs, joystick, INT0, potentiometer, GLCD and speaker.
- **The session and desktop** are the application layer on top.

**The MCB1700 is the first reference board, not the core.** Another board would add a `boards/` directory and reuse `lpc17xx`, `rtos` and `devices`. Another microcontroller would implement `rtos::Platform` and `usb::Bus` for itself. Latasim makes no claim to run arbitrary boards yet; the couplings a second board would meet are listed in [open-questions.md](open-questions.md) (question 10).

## Components

| Component | What | Doc |
|---|---|---|
| `rtos::Kernel` | RTX 4 behavioural kernel: RTX's ready, delay, round-robin, signal, mutex and timer rules, fibers, virtual processor time, run timeline | [rtos.md](rtos.md) |
| `host/cmsis_rtos1.cpp` | CMSIS-RTOS v1 over the kernel, compiled against RTX's `cmsis_os.h` | [rtos.md](rtos.md) |
| `lpc17xx::UsbDevice` | USB device controller registers, SIE commands, endpoints, frames | [usb-audio.md](usb-audio.md) |
| `usb::AudioHost` | A virtual PC: enumeration, stream selection, PCM each frame | [usb-audio.md](usb-audio.md) |
| DAC, speaker | DACR output recorded with virtual time; PCM export | [usb-audio.md](usb-audio.md) |
| `firmware/rtos` | Workloads: round-robin, preemption, yield, delays, signals and mutex, virtual timers, rate-monotonic, inversion (three ways) | [rtos.md](rtos.md) |
| `firmware/usb` | USB audio speaker on Keil's USB and DAC drivers | [usb-audio.md](usb-audio.md) |
| `firmware/media` | Media center: GLCD menu, photos, paddle game, USB audio with potentiometer volume | below |
| `workbench::Session` | Scenario runs, board inputs, faults, checks; view data (registers, timeline) | [desktop.md](desktop.md) |
| `latasim-workbench` | The Qt desktop | [desktop.md](desktop.md) |

## Validation workloads

Every result is checked by a test, and each built-in scenario checks itself (`Workbench/EveryScenario`).

| Workload | Result |
|---|---|
| **Round-robin** | 40, 30 and 20 ms of work in 15 ms slices: A 0–15, B 15–30, C 30–45, A 45–60, B 60–75, C 75–80 (done), A 80–90 (done). No idle time until the 90 ms of work is done |
| **Preemption** | Five computations at three priorities finish High first, then each level in creation order; an endless higher thread starves a lower one until it ends |
| **Yield** | Two equal threads alternate strictly (ABAB…), one 250-cycle kernel call per turn |
| **Delays** | `osDelay(10)` and `osDelay(20)`: 101 and 51 runs in 1 s, the processor otherwise idle |
| **Signals and mutex** | Five threads: signals order the work, a mutex protects the shared log ("app: start of message, end"), all terminate: finish order C M A D U |
| **Virtual timers** | 50, 80 and 120 ms timers fire 20, 12 and 8 times per second and toggle LEDs 0–2 |
| **Rate-monotonic** | C (200, 50), B (400, 100), A (400, 150): C 0–50, B 50–150, A 150–200, C 200–250, A 250–350, idle 350–400, then again |
| **Priority inversion** | Low holds a flag-protected resource from 0 ms, Medium runs 50–150 ms, High wants the resource at 60 ms. High gets it at 190 ms |
| **Inversion, elevation** | High raises Low's priority: High gets it at 90 ms, Medium finishes at 180 ms |
| **Inversion, RTX mutex** | Low inherits High's priority: High gets it at 90 ms |
| **USB audio** | Enumerated in about 30 ms. Playback starts at half a buffer with no underruns, samples 3125 cycles apart; the DAC plays the PC's samples in order; volume scales the output; pausing underruns and stops; stopping disconnects and restarting re-enumerates |
| **Media center** | Menu with joystick navigation and LED selection; photo gallery; USB audio (enumerated, streaming, potentiometer volume, disconnected on exit); paddle game with score; identical repeated runs |

**Repeated runs of every workload give identical traces, timelines and outputs.**

## Tests

| Build | Tests |
|---|---|
| **Full** (Keil packs and Qt 6) | 353 CTest tests, all pass on a clean build |
| **Without Qt** (`LATASIM_BUILD_QT=OFF`) | 352 |
| **Without the Keil packs** | 274 (the core) |

The 353 break down as follows:
- **274 core.** They include:
  - `RtosKernel` (32);
  - `UsbDevice` (9) and `Dac` (1);
  - `Extensibility` (2).
- **78 need the packs.** They include:
  - `RtosWorkload` (12);
  - `UsbAudio` (7);
  - `Workbench`, `WorkbenchViews` and `MediaCenter` (22).
- **1 CTest test for the desktop** (`WorkbenchUi`, 7 QtTest cases, offscreen).

The Phase 0–4 tests all still pass.

## Build and run

```powershell
scripts\build.ps1                                                   # core, CLI and tests (Qt if CMAKE_PREFIX_PATH finds it)
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64     # with the desktop
cmake --build build
build\latasim-workbench.exe
```

## Not in V1

Not in V1:
- MCP, agents and LLM integration (Phase 6 / V2);
- a CPU emulator or an ARM execution backend;
- a source-level debugger or GDB server;
- semaphores, pools and queues in the RTOS;
- USB classes other than audio, the USB host role and DMA;
- electrical or analog behaviour;
- clock-tree changes;
- Ethernet, CAN and UART.

Evidence limits for everything above: [open-questions.md](open-questions.md).
