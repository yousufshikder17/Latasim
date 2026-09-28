#pragma once
// Register-level GLCD firmware over SSP1 (host/ssp_glcd.cpp), in the shape of
// Keil's legacy GLCD_SPI_LPC1700.c: chip select on P0.6 by GPIO, each byte written
// to SSP1DR, SSP1SR.RNE polled, the reply read back from SSP1DR.
#include <stdint.h>

void ssp_glcd_init(void);  // SSP1 as the legacy driver sets it up
void ssp_glcd_write_reg(uint8_t reg, uint16_t value);
uint16_t ssp_glcd_read_reg(uint8_t reg);
// A w x h window at (x, y) in the driver's landscape coordinates, filled with color.
void ssp_glcd_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color);
