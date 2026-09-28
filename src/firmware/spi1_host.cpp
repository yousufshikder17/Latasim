// Host Driver_SPI1: the CMSIS-Driver SPI interface Keil's GLCD_MCB1700.c talks to,
// in place of the pack's SSP driver (the SSP1 peripheral registers are not
// modelled). Bytes go to the bound board's GLCD as its serial interface would
// carry them; software slave select drives P0.6 through the modelled GPIO
// registers, as Keil's SSP driver does for ARM_SPI_SS_MASTER_SW. Transfers take no
// virtual time; bus speed and mode settings are accepted and not modelled.
extern "C" {
#include "Driver_SPI.h"
}

#include <cstdint>

#include "host/binding.hpp"
#include "host/registers.hpp"

namespace {

constexpr std::uint32_t kFio0Set = 0x2009C018;
constexpr std::uint32_t kFio0Clr = 0x2009C01C;
constexpr std::uint32_t kChipSelect = 1u << 6;  // P0.6

std::uint32_t transferred = 0;

latasim::mcb1700::Board& board(const char* caller) { return latasim::host::require_bound_board(caller); }

ARM_DRIVER_VERSION get_version() { return {ARM_SPI_API_VERSION, ARM_DRIVER_VERSION_MAJOR_MINOR(1, 0)}; }
ARM_SPI_CAPABILITIES get_capabilities() { return {}; }
int32_t initialize(ARM_SPI_SignalEvent_t) { return ARM_DRIVER_OK; }
int32_t uninitialize() { return ARM_DRIVER_OK; }
int32_t power_control(ARM_POWER_STATE) { return ARM_DRIVER_OK; }

int32_t send(const void* data, uint32_t num) {
    auto& b = board("Driver_SPI1.Send");
    for (uint32_t i = 0; i < num; ++i) b.glcd_transfer(static_cast<const std::uint8_t*>(data)[i]);
    transferred = num;
    return ARM_DRIVER_OK;
}

int32_t receive(void* data, uint32_t num) {
    auto& b = board("Driver_SPI1.Receive");
    for (uint32_t i = 0; i < num; ++i) static_cast<std::uint8_t*>(data)[i] = b.glcd_transfer(0xFF);
    transferred = num;
    return ARM_DRIVER_OK;
}

int32_t transfer(const void* out, void* in, uint32_t num) {
    auto& b = board("Driver_SPI1.Transfer");
    for (uint32_t i = 0; i < num; ++i)
        static_cast<std::uint8_t*>(in)[i] = b.glcd_transfer(static_cast<const std::uint8_t*>(out)[i]);
    transferred = num;
    return ARM_DRIVER_OK;
}

uint32_t get_data_count() { return transferred; }

int32_t control(uint32_t control, uint32_t arg) {
    if ((control & ARM_SPI_CONTROL_Msk) == ARM_SPI_CONTROL_SS) {
        board("Driver_SPI1.Control");
        latasim::host::mmio_write(arg == ARM_SPI_SS_ACTIVE ? kFio0Clr : kFio0Set, 4, kChipSelect);
    }
    return ARM_DRIVER_OK;
}

ARM_SPI_STATUS get_status() { return {}; }  // never busy: transfers complete at once

}  // namespace

extern "C" {
extern ARM_DRIVER_SPI Driver_SPI1;
ARM_DRIVER_SPI Driver_SPI1 = {get_version, get_capabilities, initialize,     uninitialize, power_control, send,
                              receive,     transfer,         get_data_count, control,      get_status};
}
