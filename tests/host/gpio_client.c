/* A C client of Latasim's firmware-facing GPIO functions. Compiled as C: CMake
 * gives it LANGUAGE C, and the checks below would fail otherwise. */
#ifdef __cplusplus
#error "gpio_client.c must be compiled as C"
#endif

#include "gpio_client.h"

#include "host/c/latasim_keil_gpio.h"

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
