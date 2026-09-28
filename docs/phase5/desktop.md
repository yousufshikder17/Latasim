# The desktop workbench

`latasim-workbench` is a Qt 6 Widgets application for running firmware on the virtual MCB1700 and watching it. It observes and controls the simulation; it holds no simulation state of its own.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build
build\latasim-workbench.exe          # Qt's DLLs are copied next to it by windeployqt at build time
```

**Requirements:** Qt 6 (6.8 LTS was used, with Multimedia) and the Keil packs. Without Qt, or with `-DLATASIM_BUILD_QT=OFF`, everything else still builds and tests, including the session layer the desktop uses.

## Layers

```
Qt widgets (src/ui/qt)          board panel, GLCD view, audio panel, trace/RTOS/timeline/register tabs
      |  reads / calls
view data (src/workbench)       Session (run control, inputs, faults), scenarios, views (registers, timeline)
      |
simulation                      Board + Lpc1768 (+ rtos::Kernel, + usb::AudioHost) + firmware
```

- **`workbench::Session`** owns one board running one built-in scenario, the RTOS kernel when the scenario has one, and the virtual PC on the USB port. It is plain C++, with no Qt, and is tested headless (`Workbench.*`, `MediaCenter.*`).
- **Widgets read model state through its inspection functions,** with no copies kept in the UI:
  - `Board::led`, `Glcd::pixel`, `Trace::events`;
  - `Kernel::threads`/`timeline`;
  - `Lpc1768::peek32` and `usb()`.
- **They change it only through board inputs and the run controls.**

## Controls

| Control | Semantics |
|---|---|
| **Scenario / Reset** | A new session: firmware statics reset, a fresh board, the scenario started |
| **Run / Pause** | Runs in slices from a 20 ms UI timer: real time (20 ms of virtual time per slice), 10x, or as fast as possible (about 15 ms of wall time per slice). The wall clock only paces the display; results are the same at any speed |
| **Step event** | To the machine's next scheduled event: a SysTick, a timer match, an ADC completion, a USB frame. There is no instruction step: host firmware has no instructions |
| **Run for** | 1 ms, 10 ms, 100 ms or 1 s of virtual time |
| **Run until** | An absolute virtual time |
| **Time** | Cycles and milliseconds |
| **Check** | The scenario's pass/fail check and its detail, once it has run long enough |

**Board inputs**
- **Joystick:** real press/release while a button is held. With "hold", a click toggles a direction, so several can be held at once.
- **INT0:** press/release.
- **Potentiometer:** a 12-bit slider, shown raw and in volts.

**LEDs:** show `Board::led` (on, off, or grey for undriven).

## Views

| View | What it shows |
|---|---|
| **GLCD** | The 320 x 240 GRAM as a `QImage::Format_RGB16`: the model's RGB565 values copied unchanged. Drawn at whole-number scales with nearest-neighbour sampling (fit, 1x, 2x, 3x), aspect kept; rebuilt only when the framebuffer hash changes |
| **Trace** | A model/view table of every event (sequence, cycles, ms, category, operation, subject, value), from `describe()`, not by parsing text. Category filters: MMIO, interrupts, timers, ADC, GLCD, inputs, LEDs, RTOS, USB/audio. It follows new events unless scrolled back |
| **RTOS tasks** | Each thread's number, name, state, base and effective priority, what it waits for, wake tick and share of processor time, coloured by state; plus tick count, idle share, kernel-call time and the running thread |
| **Timeline** | One row per thread with its run intervals, markers for its signal, mutex, priority, yield and timer-callback events, and a row of interrupt entries. Wheel zooms, drag pans, double-click follows the latest time |
| **Registers** | GPIO, SysTick, NVIC, timers, ADC, PINCON/EINT, USB and DAC, read with `peek32`, which has no side effects: inspecting STCTRL, ADGDR or USBRxData changes nothing and records nothing. Only the expanded groups are re-read |
| **USB audio** | The PC's state, stream parameters and counts; the device's connection, address and frame; speaker samples and the DAC value; the scenario's firmware line (buffer level, volume, underruns) |

**USB audio controls**
- pause or resume the PC's stream;
- load a WAV file as its source (16-bit PCM, resampled to 32 kHz mono);
- "Play the speaker on this PC" (Qt Multimedia).

Host playback only reads recorded samples. Switching it on or off changes nothing in the simulation or the trace.

## Errors

**Faults stop the session, not the application:**
- a BusFault;
- a register used in a mode that is not modelled;
- an interrupt storm;
- an unsupported RTOS call;
- an RTOS error.

The status bar shows the message, running stops, and Reset starts again.

**How:** each run executes on a fiber. The host's fault handler abandons that fiber (or the faulting RTOS thread's) instead of aborting the process.

## Threading

Everything runs on the UI thread: runs are bounded slices of virtual time from a timer, so the window stays responsive and nothing touches the session concurrently.

**Limitation:** a firmware thread that never lets virtual time pass cannot be interrupted. Examples are a busy loop with no RTOS call, or `consume` in bare-metal code. A kernel-call loop is caught by the kernel's zero-time guard.

## Tests

`latasim_ui_tests` (QtTest, run offscreen by CTest) covers:
- the window launches;
- the LEDs match the board;
- the GLCD image equals the model's pixels;
- joystick, INT0 and potentiometer controls reach the board;
- the trace filter works;
- the RTOS table and timeline reflect the kernel;
- reading registers records nothing;
- a fault stops the session and Reset recovers;
- running advances time.
