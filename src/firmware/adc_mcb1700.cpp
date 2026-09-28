// Keil's MCB1700 ADC board driver (LPC1700_DFP 2.6.0,
// Boards/Keil/MCB1700/Common/ADC_MCB1700.c, BSD-3-Clause), compiled unchanged as
// C++ from the installed pack: it accesses LPC_SC, LPC_ADC and the NVIC directly,
// and host register proxies need C++ (docs/phase2/host-registers.md).
//
// Declaring its C interfaces first gives the definitions in the included file C
// linkage, so C callers (IRQ.c, the host port) link to them.
extern "C" {
#include "Board_ADC.h"
#include "PIN_LPC17xx.h"
void ADC_IRQHandler(void);
}

#include "ADC_MCB1700.c"
