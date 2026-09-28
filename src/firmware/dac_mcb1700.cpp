// Keil's MCB1700 DAC board driver (LPC1700_DFP 2.6.0,
// Boards/Keil/MCB1700/Common/DAC_MCB1700.c, BSD-3-Clause), compiled unchanged as
// C++ from the installed pack: it accesses LPC_SC and LPC_DAC directly, and host
// register proxies need C++ (docs/phase2/host-registers.md). Its C interface is
// declared first so the definitions keep C linkage.
extern "C" {
#include "Board_DAC.h"
#include "PIN_LPC17xx.h"
}

#include "DAC_MCB1700.c"
