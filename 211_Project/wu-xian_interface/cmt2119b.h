#ifndef CMT2119B_H
#define CMT2119B_H

/* CMOSTEK core API is retained for register/FIFO operations and state changes.
 * Use only TX/common functions on CMT2119B; it does not support RX or RSSI.
 */
#include "cmt2300a.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Project adapter: reset, check SPI access, remain in STBY. No RF table loaded. */
BOOL CMT2119B_Init(void);
/* Project adapter: call after loading the CMT2119B RFPDK table, before TX. */
void CMT2119B_ConfigPolling(void);
/* Non-destructive SPI query. TRUE means TX_DONE; no GPIO pin is read. */
BOOL CMT2119B_TxDone(void);

#ifdef __cplusplus
}
#endif
#endif
