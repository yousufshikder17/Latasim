/* Latasim host stand-in for the LPC17xx device header.
 *
 * Put this directory ahead of the real device header when compiling firmware for
 * the host. The real LPC17xx.h pulls in core_cm3.h, whose compiler intrinsics the
 * host compiler cannot build, and defines peripherals as pointers to fixed ARM
 * addresses, which are not host memory.
 *
 * Every register access below reaches the board bound with
 * latasim::host::FirmwareBinding, through the same Lpc1768 decoder as the model's
 * own tests (docs/phase2/host-registers.md):
 *
 *   C and C++   latasim_mmio_read32(address), latasim_mmio_write32(address, value)
 *   C++ only    LPC_GPIO0..4 and LPC_SC, with Keil's register names:
 *                 LPC_GPIO1->FIODIR |= 1UL << 28;  LPC_GPIO1->FIOPIN0 = 0x12;
 *               LATASIM_REG32(address), for firmware that dereferences literal
 *               addresses: *(volatile uint32_t *)0x2009C038 becomes
 *               LATASIM_REG32(0x2009C038)
 *
 * In C, the register structures are not defined: C has no way to intercept a
 * store through a struct member, so using LPC_GPIO1 in C fails to compile rather
 * than writing host memory. Only the registers the model implements are declared. */
#ifndef LATASIM_HOST_LPC17XX_H
#define LATASIM_HOST_LPC17XX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t latasim_mmio_read32(uint32_t address);
void latasim_mmio_write32(uint32_t address, uint32_t value);

#ifdef __cplusplus
}

#include <cstddef>

#include "host/registers.hpp"

#define LATASIM_REG32(address) (::latasim::host::RegisterAt<uint32_t>(address))

/* One 32-bit register with its halfword and byte views, as the device header lays
 * them out (FIOPIN, FIOPINL/FIOPINH, FIOPIN0..FIOPIN3). Anonymous structs are a
 * compiler extension, as in the real header. */
#define LATASIM_GPIO_REGISTER(name)                                                \
    union {                                                                        \
        ::latasim::host::Register<uint32_t> name;                                  \
        struct {                                                                   \
            ::latasim::host::Register<uint16_t> name##L, name##H;                  \
        };                                                                         \
        struct {                                                                   \
            ::latasim::host::Register<uint8_t> name##0, name##1, name##2, name##3; \
        };                                                                         \
    }

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4201) /* nameless struct/union */
#endif

typedef struct {
    LATASIM_GPIO_REGISTER(FIODIR);
    uint32_t RESERVED0[3];
    LATASIM_GPIO_REGISTER(FIOMASK);
    LATASIM_GPIO_REGISTER(FIOPIN);
    LATASIM_GPIO_REGISTER(FIOSET);
    LATASIM_GPIO_REGISTER(FIOCLR);
} LPC_GPIO_TypeDef;

#ifdef _MSC_VER
#pragma warning(pop)
#endif

/* System control: only PCONP is declared (GPIO_PortClock uses it). The model does
 * not implement it, so any access faults. */
typedef struct {
    uint32_t RESERVED0[0xC4 / 4];
    ::latasim::host::Register<uint32_t> PCONP;
} LPC_SC_TypeDef;

static_assert(sizeof(LPC_GPIO_TypeDef) == 0x20, "GPIO port block is 0x20 bytes");
static_assert(offsetof(LPC_GPIO_TypeDef, FIOPIN) == 0x14, "FIOPIN at 0x14");
static_assert(offsetof(LPC_SC_TypeDef, PCONP) == 0xC4, "PCONP at 0xC4");

/* The host objects these names refer to hold no register state: each register's
 * LPC address is its offset within them (host/registers.cpp). */
extern LPC_GPIO_TypeDef latasim_gpio_ports[5];
extern LPC_SC_TypeDef latasim_sc;

#define LPC_GPIO0_BASE (reinterpret_cast<uintptr_t>(&latasim_gpio_ports[0]))
#define LPC_GPIO1_BASE (reinterpret_cast<uintptr_t>(&latasim_gpio_ports[1]))
#define LPC_GPIO2_BASE (reinterpret_cast<uintptr_t>(&latasim_gpio_ports[2]))
#define LPC_GPIO3_BASE (reinterpret_cast<uintptr_t>(&latasim_gpio_ports[3]))
#define LPC_GPIO4_BASE (reinterpret_cast<uintptr_t>(&latasim_gpio_ports[4]))
#define LPC_SC_BASE (reinterpret_cast<uintptr_t>(&latasim_sc))

#define LPC_GPIO0 (&latasim_gpio_ports[0])
#define LPC_GPIO1 (&latasim_gpio_ports[1])
#define LPC_GPIO2 (&latasim_gpio_ports[2])
#define LPC_GPIO3 (&latasim_gpio_ports[3])
#define LPC_GPIO4 (&latasim_gpio_ports[4])
#define LPC_SC (&latasim_sc)

#endif /* __cplusplus */

#endif /* LATASIM_HOST_LPC17XX_H */
