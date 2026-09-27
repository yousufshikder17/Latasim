/* A C client of Latasim's firmware-facing GPIO functions. Compiled as C: CMake
 * gives it LANGUAGE C, and the checks below would fail otherwise. */
#ifdef __cplusplus
#error "gpio_client.c must be compiled as C"
#endif

#include "gpio_client.h"

#include "LPC17xx.h"
#include "host/c/latasim_keil_gpio.h"

/* In C the host device header declares no register structures, so register
 * expressions cannot silently write host memory. */
#if defined(LPC_GPIO1) || defined(LPC_SC)
#error "the host LPC17xx.h must not define peripheral pointers in C"
#endif

/* In C a character literal has type int; in C++ it has type char. */
int c_char_literal_size(void) { return (int)sizeof('a'); }

void c_led_pin_init(uint32_t port, uint32_t pin) {
    GPIO_PortClock(1U);
    PIN_Configure((uint8_t)port, (uint8_t)pin, 0U, 3U, 0U); /* function 0, pull-down, normal */
    GPIO_SetDir(port, pin, GPIO_DIR_OUTPUT);
    GPIO_PinWrite(port, pin, 0U);
}

void c_input_pin_init(uint32_t port, uint32_t pin) {
    GPIO_PortClock(1U);
    PIN_Configure((uint8_t)port, (uint8_t)pin, 0U, 0U, 0U);
    GPIO_SetDir(port, pin, GPIO_DIR_INPUT);
}

void c_pin_write(uint32_t port, uint32_t pin, uint32_t value) { GPIO_PinWrite(port, pin, value); }

uint32_t c_pin_read(uint32_t port, uint32_t pin) { return GPIO_PinRead(port, pin); }

/* Firmware that dereferences literal addresses, adapted for C: each
 * *(volatile uint32_t *)address becomes a latasim_mmio_read32/write32 call. */
#define FIO1DIR_ADDRESS 0x2009C020U
#define FIO1SET_ADDRESS 0x2009C038U

void c_literal_led0_on(void) {
    latasim_mmio_write32(FIO1DIR_ADDRESS, latasim_mmio_read32(FIO1DIR_ADDRESS) | (1U << 28));
    latasim_mmio_write32(FIO1SET_ADDRESS, 1U << 28);
}
