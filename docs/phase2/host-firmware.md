# Phase 2: running C firmware on the host

## The C boundary

`src/host/c/latasim_keil_gpio.h` declares, with C linkage, the GPIO functions of Keil's LPC17xx RTE driver: `GPIO_PortClock`, `GPIO_SetDir`, `GPIO_PinWrite`, `GPIO_PinRead` and `PIN_Configure`. Their signatures match Keil's `GPIO_LPC17xx.h` and `PIN_LPC17xx.h`, so firmware written against those headers links against Latasim unchanged.

- **The implementation holds no state** (`src/host/keil_rte_gpio.cpp`). Each call goes through `lpc17xx::KeilGpioDriver` and `Lpc1768::write32`/`read32`, the same path as the Phase 1 C++ driver emulation.
- **Firmware reaches the model through an explicit binding.** C functions take no board argument, so a test binds one:

  ```cpp
  mcb1700::Board board;
  host::FirmwareBinding bind(board);  // the C functions now act on `board`
  LED_Initialize();
  ```

  Only one board can be bound at a time. A second binding throws; the destructor unbinds.
- **Misuse aborts with a message.** A C caller cannot catch C++ exceptions, so a call with no bound board, an out-of-range pin or an unmapped port prints `latasim host: <function>: <reason>` and aborts.
- **`PIN_Configure` is accepted and ignored:** PINSEL/PINMODE are not modelled, and on reset every pin is GPIO.
- **`GPIO_PortClock` sets or clears PCGPIO in PCONP.** Originally it was ignored. PCONP is stored but gates nothing ([open-questions.md](open-questions.md), question 3).

## Real firmware: Keil's MCB1700 board support

**Source.** Keil's MCB1700 board-support drivers, from the `Keil::LPC1700_DFP` 2.6.0 pack (`Boards/Keil/MCB1700/Common/`, BSD-3-Clause):

| File | API | Board hardware |
|---|---|---|
| `LED_MCB1700.c` | `LED_Initialize/On/Off/SetOut/GetCount` | 8 LEDs |
| `Joystick_MCB1700.c` | `Joystick_Initialize/GetState` | 5-way joystick |
| `Buttons_MCB1700.c` | `Buttons_Initialize/GetState/GetCount` | INT0 button |

Their public headers (`Board_LED.h` etc.) come from `Keil::MDK-Middleware` 7.13.0.

**Why these drivers.**
- They are real, shipped firmware for this exact board, not code written for the test.
- Phase 1 already reimplements `LED_MCB1700.c` in C++ (`KeilBoardLed`), and E2 recorded that driver's effect in the LPC1768 simulator. Running the original gives a three-way check: simulator, C++ port, C source.
- Together they cover output (LEDs), input (joystick, INT0) and active-low decoding, which is everything the model supports.
- They are small, and their only dependency is the GPIO/PIN API above.

**How it is built** (root `CMakeLists.txt`, library `latasim_keil_board_drivers`, used by `latasim_keil_firmware_tests` and `latasim firmware-demo`):
- The three `.c` files are compiled **as C, unmodified, directly from the installed pack**. No Keil source or header is copied into this repository.
- The build looks for the packs under `LATASIM_KEIL_PACKS_DIR`, which defaults to `%LOCALAPPDATA%/Arm/Packs`. If they are absent, the target is skipped with a configure message and the rest of the suite still builds.
- Keil's own `GPIO_LPC17xx.h`, `PIN_LPC17xx.h` and `Board_*.h` are used.

**Adaptations.**
- **Only one: a host `LPC17xx.h`** (`src/host/device/LPC17xx.h`), placed ahead of the device pack on the include path. The real header includes `core_cm3.h`, whose ARM compiler intrinsics the host compiler cannot build. These drivers use no registers. In C the stand-in provides only `<stdint.h>` and two MMIO accessor declarations; its register structures are C++ only ([host-registers.md](host-registers.md)).
- `LPC175x_6x` is defined, as the Keil project does for the LPC1768.
- Vendor files are compiled without `/WX`. Latasim's warning policy covers Latasim's code.

**Omissions.**
- `TRACE_PIN_ENABLE` (LED7 used as a trace pin) is left undefined, as in Keil's default.
- The pull-down that `LED_Initialize` and `Buttons_Initialize` request through `PIN_Configure` is not modelled. An undriven input on the model reads high.

**What runs and what it does** (`tests/keil_firmware_test.cpp`). The test plays the application: it calls the drivers' API and presses the board's switches. It never sets LED state itself.

| Firmware | Board behaviour |
|---|---|
| `LED_Initialize()` | P1.28/29/31 and P2.2–6 become outputs driven low. All LEDs off. FIO1DIR = `0xB0000000`, FIO2DIR = `0x0000007C` |
| `LED_On(0)` | LED0 on. FIO1PIN = `0x5FFFC713` and FIO2PIN = `0x00003F83`, the values the LPC1768 simulator read with LED0 on (`spikes/uvsim-script/e2-run1.out`) |
| `LED_SetOut(0x81)` | LED0 and LED7 on, others off |
| `LED_On(8)` | returns -1 (the driver's own range check) |
| `Joystick_GetState()` | each `Board::press(direction)` yields the matching `JOYSTICK_*` bit, and 0 when released |
| `Buttons_GetState()` | `BUTTON_INT0` while `Board::press_int0()` is held |
| `LED_SetOut(Joystick_GetState())` | joystick up+left lights LED3 and LED0 |

The tests also check:
- **Convergence:** the C drivers and Phase 1's `KeilBoardLed` reach identical register and LED state for the same call sequence.
- **Determinism:** two independent runs of the same sequence produce identical state.
