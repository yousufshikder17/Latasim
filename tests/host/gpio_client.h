/* Functions of gpio_client.c, callable from the C++ tests. */
#ifndef LATASIM_TEST_GPIO_CLIENT_H
#define LATASIM_TEST_GPIO_CLIENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int c_char_literal_size(void);
void c_led_pin_init(uint32_t port, uint32_t pin);
void c_input_pin_init(uint32_t port, uint32_t pin);
void c_pin_write(uint32_t port, uint32_t pin, uint32_t value);
uint32_t c_pin_read(uint32_t port, uint32_t pin);

#ifdef __cplusplus
}
#endif

#endif /* LATASIM_TEST_GPIO_CLIENT_H */
