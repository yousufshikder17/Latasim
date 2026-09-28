// Keil's LPC17xx USB device driver (LPC1700_DFP 2.6.0, RTE_Driver/USBD_LPC17xx.c,
// Apache-2.0), compiled unchanged as C++ from the installed pack, so its register
// accesses bind to the host register proxies (docs/phase5/usb-audio.md).
//
// It is compiled without LPC175x_6x: USBD_LPC17xx.h then declares only its register
// bit constants, and LPC_USB below points at the host's USB register proxies
// instead of the chip address. The device macro gates nothing else the LPC1768 path
// uses. The Cortex-M intrinsics the driver uses (LDREX/STREX for its busy flag,
// unaligned word access) have single-threaded host equivalents here.
#include <cstdint>
#include <cstring>

#include "LPC17xx.h"

#define LPC_USB (&latasim_usb)
#define OTGClkCtrl USBClkCtrl  // one register pair on the LPC175x/6x
#define OTGClkSt USBClkSt

inline uint32_t __LDREXW(volatile uint32_t* address) { return *address; }
inline uint32_t __STREXW(uint32_t value, volatile uint32_t* address) {
    *address = value;
    return 0;  // stored: nothing can intervene on the host
}
inline void __CLREX() {}
inline void __UNALIGNED_UINT32_WRITE(void* address, uint32_t value) { std::memcpy(address, &value, 4); }
inline uint32_t __UNALIGNED_UINT32_READ(const void* address) {
    uint32_t value;
    std::memcpy(&value, address, 4);
    return value;
}

// The driver's C interface, declared first so the definitions below get C linkage.
extern "C" {
#include "Driver_USBD.h"
extern ARM_DRIVER_USBD Driver_USBD0;
void USBD_IRQ(void);
extern uint8_t usb_role;
extern uint8_t usb_state;
int32_t USB_PinsConfigure(void);
int32_t USB_PinsUnconfigure(void);
}

#include "USBD_LPC17xx.c"
