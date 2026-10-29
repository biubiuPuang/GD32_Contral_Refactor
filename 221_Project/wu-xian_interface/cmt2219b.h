#ifndef CMT2219B_H
#define CMT2219B_H

/* CMOSTEK core API is retained for register/FIFO operations and state changes.
 * Use only RX/common functions on CMT2219B; it does not support TX.
 */
#include "cmt2300a.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Project adapter: reset, check SPI access, remain in STBY. No RF table loaded. */
BOOL CMT2219B_Init(void);
/* Project adapter: call after loading the CMT2219B RFPDK table, before RX. */
void CMT2219B_ConfigPolling(void);
/* Non-destructive SPI queries. No GPIO1/2/3 pin is read. */
BOOL CMT2219B_PacketReady(void);
BOOL CMT2219B_PacketError(void);
BOOL CMT2219B_FifoOverflow(void);

#ifdef __cplusplus
}
#endif
#endif
