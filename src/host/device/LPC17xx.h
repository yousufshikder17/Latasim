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
 *               SystemCoreClock, SystemCoreClockUpdate(), SysTick_Config(ticks),
 *               IRQn_Type and the NVIC_* functions (host/cmsis_system.cpp)
 *   C++ only    LPC_GPIO0..4, LPC_SC, LPC_PINCON, LPC_TIM0..3, LPC_ADC, LPC_DAC and the
 *               USB device registers (latasim_usb), with
 *               Keil's register names (only the registers the model implements work):
 *                 LPC_GPIO1->FIODIR |= 1UL << 28;  LPC_GPIO1->FIOPIN0 = 0x12;
 *               LATASIM_REG32(address), for firmware that dereferences literal
 *               addresses: *(volatile uint32_t *)0x2009C038 becomes
 *               LATASIM_REG32(0x2009C038)
 *
 * In C, the register structures are not defined: C has no way to intercept a
 * store through a struct member, so using LPC_GPIO1 in C fails to compile rather
 * than writing host memory. */
#ifndef LATASIM_HOST_LPC17XX_H
#define LATASIM_HOST_LPC17XX_H

#include <stdint.h>

/* CMSIS compiler macros the Keil board drivers use (cmsis_compiler.h on the
 * target). __NOP() takes no virtual time on the host, so busy-wait delays built
 * from it take none either. */
#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif
#ifndef __NOP
#define __NOP() ((void)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

uint32_t latasim_mmio_read32(uint32_t address);
void latasim_mmio_write32(uint32_t address, uint32_t value);

/* The interrupts Latasim models, numbered as in the device header (CMSIS
 * IRQn_Type), and its implemented priority bits. */
typedef enum IRQn {
    SysTick_IRQn = -1,
    TIMER0_IRQn = 1,
    TIMER1_IRQn = 2,
    TIMER2_IRQn = 3,
    TIMER3_IRQn = 4,
    EINT0_IRQn = 18,
    ADC_IRQn = 22,
    USB_IRQn = 24
} IRQn_Type;
#define __NVIC_PRIO_BITS 5

/* CMSIS system_LPC17xx.h and core_cm3.h functions, host versions. */
extern uint32_t SystemCoreClock;
void SystemCoreClockUpdate(void);
uint32_t SysTick_Config(uint32_t ticks);
void NVIC_EnableIRQ(IRQn_Type IRQn);
void NVIC_DisableIRQ(IRQn_Type IRQn);
void NVIC_SetPendingIRQ(IRQn_Type IRQn);
void NVIC_ClearPendingIRQ(IRQn_Type IRQn);
uint32_t NVIC_GetPendingIRQ(IRQn_Type IRQn);
uint32_t NVIC_GetActive(IRQn_Type IRQn);
void NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority);
uint32_t NVIC_GetPriority(IRQn_Type IRQn);

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

#define LATASIM_REG ::latasim::host::Register<uint32_t>

/* System control: the registers modelled so far (UM10360 chapter 4 and 3.6).
 * Accessing any other system control register faults. */
typedef struct {
    uint32_t RESERVED0[0xC4 / 4];
    LATASIM_REG PCONP;                     /* 0x0C4 */
    uint32_t RESERVED1[(0x140 - 0xC8) / 4];
    LATASIM_REG EXTINT;                    /* 0x140 */
    uint32_t RESERVED2;
    LATASIM_REG EXTMODE;                   /* 0x148 */
    LATASIM_REG EXTPOLAR;                  /* 0x14C */
    uint32_t RESERVED3[(0x1A8 - 0x150) / 4];
    LATASIM_REG PCLKSEL0;                  /* 0x1A8 */
    LATASIM_REG PCLKSEL1;                  /* 0x1AC */
} LPC_SC_TypeDef;

/* Pin connect block (UM10360 chapter 8): PINSEL, PINMODE, PINMODE_OD. */
typedef struct {
    LATASIM_REG PINSEL0, PINSEL1, PINSEL2, PINSEL3, PINSEL4, PINSEL5, PINSEL6, PINSEL7, PINSEL8, PINSEL9,
        PINSEL10;                          /* 0x00-0x28 */
    uint32_t RESERVED0[5];
    LATASIM_REG PINMODE0, PINMODE1, PINMODE2, PINMODE3, PINMODE4, PINMODE5, PINMODE6, PINMODE7, PINMODE8,
        PINMODE9;                          /* 0x40-0x64 */
    LATASIM_REG PINMODE_OD0, PINMODE_OD1, PINMODE_OD2, PINMODE_OD3, PINMODE_OD4; /* 0x68-0x78 */
    LATASIM_REG I2CPADCFG;                 /* 0x7C */
} LPC_PINCON_TypeDef;

/* Timer 0-3 (UM10360 chapter 21). CCR, CR0-1, EMR and CTCR are declared but not
 * modelled: accessing them faults. */
typedef struct {
    LATASIM_REG IR, TCR, TC, PR, PC, MCR, MR0, MR1, MR2, MR3; /* 0x00-0x24 */
    LATASIM_REG CCR, CR0, CR1;             /* 0x28-0x30 */
    uint32_t RESERVED0[2];
    LATASIM_REG EMR;                       /* 0x3C */
    uint32_t RESERVED1[12];
    LATASIM_REG CTCR;                      /* 0x70 */
} LPC_TIM_TypeDef;

/* A/D converter (UM10360 chapter 29). */
typedef struct {
    LATASIM_REG ADCR, ADGDR;               /* 0x00, 0x04 */
    uint32_t RESERVED0;
    LATASIM_REG ADINTEN;                   /* 0x0C */
    LATASIM_REG ADDR0, ADDR1, ADDR2, ADDR3, ADDR4, ADDR5, ADDR6, ADDR7; /* 0x10-0x2C */
    LATASIM_REG ADSTAT, ADTRM;             /* 0x30, 0x34 */
} LPC_ADC_TypeDef;

/* D/A converter (UM10360 chapter 30). */
typedef struct {
    LATASIM_REG DACR, DACCTRL, DACCNTVAL;  /* 0x00-0x08 */
} LPC_DAC_TypeDef;

/* USB device controller (UM10360 chapter 11), the device-side registers and the
 * clock control pair; the host, OTG and DMA registers are not modelled. The field
 * names are those Keil's USB device driver uses. */
typedef struct {
    uint32_t RESERVED0[0x200 / 4];
    LATASIM_REG DevIntSt, DevIntEn, DevIntClr, DevIntSet;      /* 0x200 */
    LATASIM_REG CmdCode, CmdData;                              /* 0x210 */
    LATASIM_REG RxData, TxData, RxPLen, TxPLen, Ctrl, DevIntPri; /* 0x218 */
    LATASIM_REG EpIntSt, EpIntEn, EpIntClr, EpIntSet, EpIntPri; /* 0x230 */
    LATASIM_REG ReEp, EpInd, MaxPSize;                         /* 0x244 */
    uint32_t RESERVED1[(0xFF4 - 0x250) / 4];
    LATASIM_REG USBClkCtrl, USBClkSt;                          /* 0xFF4 */
} LPC_USB_TypeDef;

#undef LATASIM_REG

static_assert(offsetof(LPC_USB_TypeDef, DevIntSt) == 0x200, "USBDevIntSt at 0x200");
static_assert(offsetof(LPC_USB_TypeDef, MaxPSize) == 0x24C, "USBMaxPSize at 0x24C");
static_assert(offsetof(LPC_USB_TypeDef, USBClkCtrl) == 0xFF4, "USBClkCtrl at 0xFF4");
static_assert(sizeof(LPC_GPIO_TypeDef) == 0x20, "GPIO port block is 0x20 bytes");
static_assert(offsetof(LPC_GPIO_TypeDef, FIOPIN) == 0x14, "FIOPIN at 0x14");
static_assert(offsetof(LPC_SC_TypeDef, PCONP) == 0xC4, "PCONP at 0xC4");
static_assert(offsetof(LPC_SC_TypeDef, EXTINT) == 0x140, "EXTINT at 0x140");
static_assert(offsetof(LPC_SC_TypeDef, EXTPOLAR) == 0x14C, "EXTPOLAR at 0x14C");
static_assert(offsetof(LPC_SC_TypeDef, PCLKSEL1) == 0x1AC, "PCLKSEL1 at 0x1AC");
static_assert(offsetof(LPC_PINCON_TypeDef, PINMODE0) == 0x40, "PINMODE0 at 0x40");
static_assert(offsetof(LPC_PINCON_TypeDef, I2CPADCFG) == 0x7C, "I2CPADCFG at 0x7C");
static_assert(offsetof(LPC_TIM_TypeDef, EMR) == 0x3C, "EMR at 0x3C");
static_assert(offsetof(LPC_TIM_TypeDef, CTCR) == 0x70, "CTCR at 0x70");
static_assert(offsetof(LPC_ADC_TypeDef, ADSTAT) == 0x30, "ADSTAT at 0x30");

/* The host objects these names refer to hold no register state: each register's
 * LPC address is its offset within them (host/registers.cpp). */
extern LPC_GPIO_TypeDef latasim_gpio_ports[5];
extern LPC_SC_TypeDef latasim_sc;
extern LPC_PINCON_TypeDef latasim_pincon;
extern LPC_TIM_TypeDef latasim_tim[4];
extern LPC_ADC_TypeDef latasim_adc;
extern LPC_DAC_TypeDef latasim_dac;
extern LPC_USB_TypeDef latasim_usb;

#define LPC_GPIO0_BASE (reinterpret_cast<uintptr_t>(&latasim_gpio_ports[0]))
#define LPC_GPIO1_BASE (reinterpret_cast<uintptr_t>(&latasim_gpio_ports[1]))
#define LPC_GPIO2_BASE (reinterpret_cast<uintptr_t>(&latasim_gpio_ports[2]))
#define LPC_GPIO3_BASE (reinterpret_cast<uintptr_t>(&latasim_gpio_ports[3]))
#define LPC_GPIO4_BASE (reinterpret_cast<uintptr_t>(&latasim_gpio_ports[4]))
#define LPC_SC_BASE (reinterpret_cast<uintptr_t>(&latasim_sc))
#define LPC_PINCON_BASE (reinterpret_cast<uintptr_t>(&latasim_pincon))
#define LPC_TIM0_BASE (reinterpret_cast<uintptr_t>(&latasim_tim[0]))
#define LPC_TIM1_BASE (reinterpret_cast<uintptr_t>(&latasim_tim[1]))
#define LPC_TIM2_BASE (reinterpret_cast<uintptr_t>(&latasim_tim[2]))
#define LPC_TIM3_BASE (reinterpret_cast<uintptr_t>(&latasim_tim[3]))
#define LPC_ADC_BASE (reinterpret_cast<uintptr_t>(&latasim_adc))
#define LPC_DAC_BASE (reinterpret_cast<uintptr_t>(&latasim_dac))

#define LPC_GPIO0 (&latasim_gpio_ports[0])
#define LPC_GPIO1 (&latasim_gpio_ports[1])
#define LPC_GPIO2 (&latasim_gpio_ports[2])
#define LPC_GPIO3 (&latasim_gpio_ports[3])
#define LPC_GPIO4 (&latasim_gpio_ports[4])
#define LPC_SC (&latasim_sc)
#define LPC_PINCON (&latasim_pincon)
#define LPC_TIM0 (&latasim_tim[0])
#define LPC_TIM1 (&latasim_tim[1])
#define LPC_TIM2 (&latasim_tim[2])
#define LPC_TIM3 (&latasim_tim[3])
#define LPC_ADC (&latasim_adc)
#define LPC_DAC (&latasim_dac)

#endif /* __cplusplus */

#endif /* LATASIM_HOST_LPC17XX_H */
