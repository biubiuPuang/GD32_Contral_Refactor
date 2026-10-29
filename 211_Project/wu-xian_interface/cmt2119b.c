/* Project adapters over the CMOSTEK driver; no RF parameters are embedded. */
#include "cmt2119b.h"

BOOL CMT2119B_Init(void)
{
    CMT2300A_InitGpio();
    CMT2300A_Init();
    if(!CMT2300A_GoStby())
        return FALSE;
    /* IsExist checks register read/write communication, not the chip model. */
    if(!CMT2300A_IsExist())
        return FALSE;
    CMT2119B_ConfigPolling();
    return TRUE;
}

void CMT2119B_ConfigPolling(void)
{
    CMT2300A_SetInterruptPolar(TRUE);
    CMT2300A_EnableInterrupt(CMT2300A_MASK_TX_DONE_EN);
    CMT2300A_ClearInterruptFlags();
}

BOOL CMT2119B_TxDone(void)
{
    return (CMT2300A_ReadPollingFlags(CMT2300A_CUS_INT_CLR1)
            & CMT2300A_MASK_TX_DONE_FLG) ? TRUE : FALSE;
}
