# Phase 2: register-level firmware on the host

Much LPC1768 firmware skips driver APIs and touches registers directly: `LPC_GPIO1->FIOSET = ...` through the device header, or a literal address such as `*(volatile uint32_t *)0x2009C038`. On the host those addresses are not device memory. This spike decides how such code reaches Latasim's existing MMIO decoder (`Lpc1768::read8/16/32`, `write8/16/32`), which it does not change.

## Options considered

| | Approach | Source changes | Real C firmware | Deterministic, safe | Verdict |
|---|---|---|---|---|---|
| A | Host device header redefining the register macros/types | none for `LPC_xxx->REG` code | C++ only (below) | yes | **chosen, with B** |
| B | Proxy register objects | none | C++ only | yes | **the mechanism inside A** |
| C | Explicit accessor functions/macros | every register access | yes | yes | **kept for literal addresses and C** |
| D | Host memory mapped at LPC addresses | none | yes | no | rejected |
| E | Shadow memory synchronised with the model | none | yes | no | rejected |

**A + B: a host `LPC17xx.h` whose registers are proxy objects.** `src/host/device/LPC17xx.h` replaces the device header on the host include path.
- **Keil's names and layout are kept:** `LPC_GPIO_TypeDef`, `LPC_GPIO0`–`LPC_GPIO4`, `LPC_GPIOn_BASE`, and each register's union of word, halfword (`FIOPINL/H`) and byte (`FIOPIN0`–`3`) views.
- **Each register member is a `host::Register<T>`.** `T` is `uint32_t`, `uint16_t` or `uint8_t`. A proxy stores nothing. Its LPC address is its offset inside a host anchor object (`latasim_gpio_ports[5]`) plus the peripheral's base, `0x2009C000`.
- **Reading or assigning a proxy calls the bound board's `Lpc1768`** with the proxy's width. A byte view is an 8-bit access, so the narrow-access rules (and their open question) are the decoder's, not the proxy's.
- **Compound assignments** (`|=`, `&=`, `^=`) are a read then a write, as the compiled ARM code would do.
- **Keil's own address arithmetic works unchanged.** `LPC_GPIO(n)` is `(LPC_GPIO_TypeDef *)(LPC_GPIO0_BASE + 0x20*n)`, and `LPC_GPIO0_BASE` is the anchor's host address.

**Why A + B.**
- **No source changes** for code written against the device header.
- **Everything reaches the model through one explicit function.** No host memory holds register state, and faults stay faults.
- **Portable C++.** Anonymous structs inside unions are a compiler extension, as they are in the real header.
- **Adding a peripheral** is a structure plus an anchor object, once the model implements it.

**The limit: it needs C++.** C cannot run code on a struct-member store, so in C `LPC_GPIO1->FIOSET = x` can only write memory. The host header therefore defines no register structures for C: such code fails to compile instead of silently writing host memory (checked by `#error` in `tests/host/gpio_client.c`).

**Register-level `.c` files are compiled as C++.** This is a build-setting change, not a source change. Typical driver C is also valid C++. Keil's `GPIO_LPC17xx.c` builds unchanged this way; C-only constructs (implicit `void *` conversions, for example) would need adapting.

**C: explicit accessors.** `latasim_mmio_read32(address)` and `latasim_mmio_write32(address, value)` have C linkage. C firmware that must stay C rewrites each register access as a call to them.

**D: host memory at LPC addresses (rejected).** A probe on this machine showed that `VirtualAlloc` can reserve `0x20090000`, `0x400F0000` and `0x22000000` in a small 64-bit process. Making those pages authoritative is the problem:
- **Making them unreadable** only traps an access. To learn its direction, width and value, the handler must decode the faulting x64 instruction (`mov`, `movzx`, `or` to memory, `bt`, vector copies, …), emulate it and advance `RIP`. That is a CPU emulator for the host ISA.
- **Mapping ordinary memory** makes it a second register store (see E).
- **The reservation can fail** in a larger process (ASLR, loaded modules), and the approach is Windows-specific.

**E: shadow memory (rejected).**
- **Reads are wrong:** a plain host array cannot show the model's state (input levels, `FIOCLR` reading 0).
- **Writes are lost:** writes that do not change the stored value (setting a `FIOSET` bit twice) cannot be detected. Sub-word and bit-band behaviour would have to be reimplemented beside the decoder.

## Literal pointers: decision

**Firmware that dereferences literal addresses needs a documented source adaptation.** Latasim does not support `*(volatile uint32_t *)0x2009C038` unchanged on the host backend.
- **C++:** replace the dereference with `LATASIM_REG32(0x2009C038)`. It supports `=`, `|=`, `&=`, `^=` and reads. It reaches the same decoder, bit-band aliases included (`LATASIM_REG32(bit_band_alias(FIO1CLR, 28)) = 1`). Typically only a register-definition macro changes, for example `#define FIO1SET (*(volatile uint32_t *)0x2009C038)`.
- **C:** replace reads and writes with `latasim_mmio_read32`/`latasim_mmio_write32`.
- **Unchanged literal pointers belong to the simulator-backed backends** (B3, µVision, and a possible B4 CPU emulator), where the firmware runs as ARM code.

## What is implemented and tested

**Files:**
- `src/host/device/LPC17xx.h`: register structures, `LATASIM_REG32`, C accessors.
- `src/host/registers.hpp/.cpp`: `Register<T>` and `RegisterAt<T>`, the anchor objects, and the routing to the bound board.

**Tests** (`tests/host_registers_test.cpp`):
- **Struct-register reads and writes** reach the board:
  - LEDs change;
  - joystick presses are visible in `FIOPIN`;
  - `FIOSET` reads the latch and `FIOCLR` reads 0.
- **Byte and halfword views** make 8- and 16-bit accesses: `FIODIR0`, `FIOSET3`, `FIOCLRH`, `FIOPINH`.
- **Convergence:** registers, the C GPIO layer and the Phase 1 C++ driver reach identical state for the same operations.
- **`LATASIM_REG32`:** literal addresses, a bit-band alias, and register-to-register copy.
- **C firmware** using the accessors matches the register path.
- **No backing store:** after writes to every port, the anchor objects are still all zero.
- **Faults abort with the address:**
  - `PCONP` (unmodelled), a reserved GPIO offset, an unmapped address, a misaligned access;
  - a call with no board bound;
  - a proxy outside the anchors (a local `LPC_GPIO_TypeDef`).

**Keil's register-level driver** (`tests/keil_register_driver_test.cpp`, built with the Keil packs present): `RTE_Driver/GPIO_LPC17xx.c` from `LPC1700_DFP` 2.6.0, compiled unchanged as C++.
- `GPIO_SetDir`, `GPIO_PinWrite`, `GPIO_PinRead`, `GPIO_PortWrite` (with `FIOMASK`) and `GPIO_PortRead` drive and read the board.
- They match the Phase 1 C++ driver emulation.

## Limitations

- **Only GPIO registers are declared,** plus `LPC_SC->PCONP` so that `GPIO_PortClock` compiles.
- **`PCONP` is not modelled, so the two paths diverge on `GPIO_PortClock`.** On the register path it faults. The C GPIO layer (`src/host/keil_rte_gpio.cpp`) accepts it as a no-op, which is why Keil's board drivers run on that layer rather than on `GPIO_LPC17xx.c`.
- **`volatile` is meaningless on proxies.** Every access is performed, in program order, because each one is a call into the model's translation unit.
- **Proxy operators cover the common forms:** assignment, `|=`, `&=`, `^=` and reads. `++`, `<<=` and taking a register's address as a `uint32_t *` are not supported.
- **The binding is single-board and single-threaded,** as for the C GPIO layer.
