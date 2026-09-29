# External host-compiled firmware

Existing LPC1768 firmware projects can be compiled into the workbench and run as scenarios, from where they live, several in one executable. Its sources are never modified and nothing from it is copied into this repository. A file that needs host adaptations is compiled from an adapted copy in the build tree (see Source adaptations).

## Building

```
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 ^
      -DLATASIM_USER_FIRMWARE_DIR="C:/path/to/project;C:/path/to/other"
cmake --build build
build\latasim-workbench.exe
```

The scenario list then ends with one **External: *folder name*** per folder.

| Option | Default | Meaning |
|---|---|---|
| `LATASIM_USER_FIRMWARE_DIR` | empty (no external firmware) | The project folders, `;`-separated. Folder names must differ. |
| `LATASIM_USER_FIRMWARE_SOURCES` | each folder's top-level `*.c` | With a single folder: its sources, relative to it, `;`-separated. Needed when the top level holds more than one program or the sources are in subfolders. |
| `LATASIM_USER_FIRMWARE_ADAPTER` | `local/adapters/<folder name>.cmake` if it exists | With a single folder: a CMake script of source adaptations (see below). Relative paths are from this repository. With several folders, each uses its own `local/adapters/<folder name>.cmake`. |
| `LATASIM_USER_FIRMWARE_BOARD_DRIVERS` | empty | Keil MCB1700 board drivers the firmware uses: `ADC`, `LED`, `Joystick`, `Buttons` (see Keil board drivers). A folder's adapter may set it for that folder alone. |
| `LATASIM_FIRMWARE_NOP_CYCLES` | 10 | Core cycles one `__NOP()` takes (see Timing). |

- **Requirements.** The option needs the workbench session layer, which needs the Keil packs (as the built-in scenarios do); configuring without them is an error. The desktop also needs Qt.
- **Without the option** nothing changes: the core, the CLI, the tests and the workbench build as before.
- **How it is compiled** (`latasim_add_host_firmware` in `CMakeLists.txt`):
  - The `.c` files are compiled as C++, so register expressions bind to the host register proxies ([phase2/host-registers.md](phase2/host-registers.md)). Ordinary driver C is valid C++; C-only constructs (an implicit `void *` conversion, for example) need adapting.
  - The host `LPC17xx.h` comes before any device header on the include path. Headers next to a source file are found as usual.
  - `src/host/firmware_shim.h` is force-included into every source.
  - **Each firmware has its own C++ namespace.** Every source is compiled through a generated wrapper that includes it inside `<entry>_firmware`. So two firmware in one executable can both define `GLCD_Init`, `LED_Init` or `delay`.
    - Only `main`, the interrupt handlers and the C functions they call (board drivers, the C library) are shared by name.
    - The shim includes the common C library headers first, at global scope.
  - The firmware's own warnings are its own: Latasim's warning policy is not applied, and nothing is weakened globally.

## The firmware's entry point

- **One `int main(void)` per folder.** The shim renames it (`latasim_user<N>_main` for the N-th folder, C linkage). The scenario runs it bare metal on a fiber of its own (`Scenario::bare_main`, `workbench/session.hpp`).
- **Main runs whenever virtual time has caught up with the processor time it has used.** So each register access happens at the right virtual time. Set-up before the first delay happens at t = 0, as soon as the scenario is selected.
- **Time passes only through `__NOP()` and `latasim_consume_cycles()`.** A loop without either takes no virtual time. A loop that never ends without either (an empty final `while (1) {}`) never gives control back, and the workbench hangs. Give such a loop a `__NOP()` (see below).
- **Returning from main** leaves the processor idle; time still advances.
- **Not provided:**
  - **Startup code.** `startup_*.s` and `SystemInit` are not run. The model starts at its reset state with the 100 MHz core clock that `SystemInit` would set.
  - **Resetting statics.** Latasim cannot know the firmware's globals (or a board driver's), so **Reset** in the desktop runs main again with the values the last run left, for example a chase continuing where it was. Restart the workbench for a clean start.
- **Faults** stop the session with a message, as for built-in scenarios: an unmapped or unmodelled register, an unsupported mode.

## Interrupt handlers

The firmware's CMSIS handlers for the exceptions Latasim models run as on the target: `SysTick_Handler`, `TIMER0_IRQHandler` to `TIMER3_IRQHandler`, `EINT0_IRQHandler`, `ADC_IRQHandler` and `USB_IRQHandler`.
- **Only the handlers the firmware defines are bound.** An interrupt it enables without a handler stays pending.
- **Handlers run at the virtual time their exception is taken.** They take no virtual time themselves, so a `__NOP()` or `latasim_consume_cycles()` in a handler adds none. `main()` sees their effects at its next step.
- **No collisions.** The shim renames each handler `<entry>_<name>` with C linkage. It therefore cannot collide with a built-in scenario's handler of the same name, such as Keil's `SysTick_Handler`.
- **How presence is detected.** `latasim_add_host_firmware` generates a table that binds the handlers that exist.
  - A name the firmware leaves undefined resolves through the MSVC linker's `/alternatename` to a placeholder, and is skipped.
  - This works even when the handler is in a file nothing else references.
  - Executables linking the firmware are linked with `/INCREMENTAL:NO`, so that addresses compare exactly.
- **Board-driver handlers.** A board driver's own handler (the ADC driver's `ADC_IRQHandler`) is bound when the firmware does not define one; on the target the driver's is then the only definition.
- The desktop's status line lists the bound handlers.

## Keil board drivers

Firmware built with Keil's RTE "Board Support" components calls pack drivers that are not in its folder: for example `ADC_Initialize`, `ADC_StartConversion` and `ADC_GetValue` from `Board_ADC.h`. List them in `LATASIM_USER_FIRMWARE_BOARD_DRIVERS`:

| Driver | Pack source | Latasim library |
|---|---|---|
| `ADC` | `Boards/Keil/MCB1700/Common/ADC_MCB1700.c` | `latasim_keil_adc` (its `ADC_IRQHandler` is bound too) |
| `LED`, `Joystick`, `Buttons` | `LED_MCB1700.c`, `Joystick_MCB1700.c`, `Buttons_MCB1700.c` | `latasim_keil_board_drivers` |

- **The drivers are Latasim's own builds** of the installed pack's sources, unchanged. Nothing is copied into this repository.
- **The firmware sees `Board_<name>.h` through generated wrappers** that include the pack header inside `extern "C"`. So its calls, compiled as C++, reach the drivers' C functions without a source change.
- **Drivers keep their statics** across sessions, as the firmware does (see Resetting statics).

## Polling main loops

A main loop that polls registers or driver state (`while (1) { v = ADC_GetValue(); ... }`) has nothing in it that takes virtual time. On the host it would run without end at one instant, and no interrupt would ever come due. Give each pass a cost with an adapter, for example:

```cmake
latasim_adapt_source(main.c [[while (1) {]] [[while (1) { latasim_consume_cycles(200);  /* modeled pass cost */]])
```

- **The cost is a model, not a measurement.** Choose it from the loop's shape, and label it in the adapter.
  - The loop then runs once per that many core cycles.
  - Interrupts are taken at their exact times in between.
  - Each register access still happens once virtual time has caught up with it.
- **Host speed.** Every register access is a call into the model. A loop making a dozen accesses per 2 µs pass runs several times slower than real time on the host. Virtual time stays exact.

## Trace retention

Register accesses from a polling loop can reach millions a second. The trace keeps at most `Trace::mmio_capacity()` of them: 1,000,000 by default, about 56 MB.
- **Dropping.** When there are more, the oldest half are dropped in one step, and `Trace::dropped()` counts them. The desktop's trace view notes the count. Sequence numbers are never reused, so the gaps show where accesses went.
- **Always kept:** input, LED, interrupt, timer, ADC conversion, GLCD and RTOS events. What the dropped accesses did stays in the trace.
- **Built-in scenarios** stay well under the limit.

## Source adaptations

Most register-level code builds unchanged. What needs changing is marked in the table below. There are two ways to make the changes:

- **Adapter script** (recommended: the firmware stays untouched). A CMake script lists exact text replacements with `latasim_adapt_source(<file> <old> <new>)`. `<file>` is relative to the firmware folder, and `<old>` must occur exactly once in the original. Bracket arguments (`[[...]]`) take C text as it is. At configure time the original is read, the replacements applied, and the result written to `build/user_firmware/<folder name>/` with a `#line` directive, so diagnostics point at the original file and line. That copy is compiled instead. Editing the original re-runs the configuration. A replacement that no longer matches it is a configure error, so the adapter cannot silently drift.
  - Adapters for private projects belong in `local/adapters/`, which git ignores. `local/adapters/<folder name>.cmake` is picked up without naming it, so each project gets its own file.
  - The adapter only changes text. Product behaviour is the same with or without one.
- **Guarded edits in the firmware**, if you own it. `#ifdef LATASIM_HOST` (defined by the shim) keeps the target build identical.

Why a forced include or compile definitions cannot do these changes: the macros involved are defined inside the firmware's own `.c` files, after anything injected ahead of them. A raw `(volatile unsigned long *)0x…` cast and an empty loop cannot be intercepted at all.

| Firmware construct | On the host |
|---|---|
| `LPC_GPIO1->FIOSET = ...`, `LPC_SSP1->DR`, other device-header registers | unchanged |
| `(unsigned long)&LPC_GPIO1->FIOPIN` in address arithmetic (bit-band alias macros) | unchanged: the register's LPC address |
| `*(volatile unsigned long *)0x233806F0` (a literal register or bit-band address) | `LATASIM_REG32(0x233806F0)`, usually by changing the one macro that dereferences |
| `volatile unsigned long *p = &...;` (a pointer variable to a register) | declare `LATASIM_REG32_PTR p` |
| an empty idle loop `while (1) {}` | put a `__NOP()` in it |

For example, an adapter for a bit-band macro and its pointer variable:

```cmake
latasim_adapt_source(main.c [[(*((volatile unsigned long *)(x)))]] [[LATASIM_REG32(x)]])
latasim_adapt_source(main.c [[volatile unsigned long *bit;]] [[LATASIM_REG32_PTR bit;]])
latasim_adapt_source(main.c [[while (1) {]] [[while (1) { __NOP();]])
```

or the same as guarded edits:

```c
#ifdef LATASIM_HOST
#define ADDRESS(x) LATASIM_REG32(x)
LATASIM_REG32_PTR bit;
#else
#define ADDRESS(x) (*((volatile unsigned long *)(x)))
volatile unsigned long *bit;
#endif
```

On the host a literal address is host memory. Dereferencing one that was not adapted crashes the process instead of reaching the model. `tests/external_firmware/sample_firmware.c` shows every adaptation above, and the tests build it the way an external folder is built.

## ITM and printf

ITM is not modelled.
- The shim renames `fputc`, so a firmware's ITM retarget (the `fputc` that writes the ITM stimulus port) is compiled but never called.
- `printf` writes to the host process's standard output. That shows in a console, not in the desktop.
- Code that does run must not touch ITM or `DEMCR` through literal addresses (see above).

## Timing

- **`__NOP()` takes `LATASIM_FIRMWARE_NOP_CYCLES` core cycles.** The default, 10, is what the LPC1768 simulator measured for one pass of a `volatile` counter loop around `__NOP()` (docs/phase0/findings.md). That makes such a delay loop take its target time. A different loop shape or compiler gives a different cost; the option is the calibration.
- **Other host code takes no virtual time.** That includes busy loops without `__NOP()`, SSP transfers and interrupt handlers. A polling loop needs a modeled cost (see Polling main loops).

## SSP1

`LPC_SSP1` works as a polled SPI master uses it: `lpc17xx/ssp.hpp`, UM10360 chapter 18.
- **Stored:** CR0, CR1 and CPSR.
- **DR write:** sends one 8-bit frame at once to the MCB1700 GLCD, which is wired to SSP1's bus.
- **The reply** goes to an 8-frame receive FIFO, which a DR read empties.
- **SR:** reads TFE = TNF = 1 and BSY = 0, with RNE and RFF following the FIFO.
- **Not modelled** (the session stops with a message): frames other than 8-bit SPI, slave and loopback modes, DR writes while disabled, receive overrun, the IMSC/RIS/MIS/ICR/DMACR registers, and SSP0.
- **No virtual time:** the bit rate is stored, not applied.
- **Chip select** is GPIO P0.6, as the board's GLCD drivers drive it.

## What cannot be loaded

- **Compiled target images:** `.axf`, `.elf`, `.hex` or other ARM binaries. Latasim runs host-compiled C, not ARM code. Running unchanged binaries would take a CPU emulator backend, which is not built.
- **Assembly sources** and CMSIS core intrinsics other than `__NOP()`.
- **Peripherals the model does not implement.** They fault at their first access: see `lpc17xx/lpc1768.hpp` for what is mapped.
- **Text drawn beyond the GLCD's edge.** The GLCD model wraps GRAM addresses beyond the panel back onto it, so a string longer than a line reappears at the line's start. The controller's behaviour there is not specified: treat such output as a firmware overflow.
