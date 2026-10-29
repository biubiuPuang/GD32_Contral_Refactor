/* Project adapters over the CMOSTEK driver; no RF parameters are embedded. */
#include "cmt2219b.h"

BOOL CMT2219B_Init(void)
{
    CMT2300A_InitGpio();
    CMT2300A_Init();
    if(!CMT2300A_GoStby())
        return FALSE;
    if(!CMT2300A_IsExist())
        return FALSE;
    CMT2219B_ConfigPolling();
    return TRUE;
}

void CMT2219B_ConfigPolling(void)
{
    CMT2300A_SetInterruptPolar(TRUE);
    CMT2300A_EnableInterrupt(CMT2300A_MASK_PKT_DONE_EN);
    CMT2300A_ClearInterruptFlags();
}

BOOL CMT2219B_PacketReady(void)
{
    return (CMT2300A_ReadPollingFlags(CMT2300A_CUS_INT_FLAG)
            & CMT2300A_MASK_PKT_OK_FLG) ? TRUE : FALSE;
}

BOOL CMT2219B_PacketError(void)
{
    return (CMT2300A_ReadPollingFlags(CMT2300A_CUS_INT_FLAG)
            & (CMT2300A_MASK_PKT_ERR_FLG | CMT2300A_MASK_COL_ERR_FLG)) ? TRUE : FALSE;
}

BOOL CMT2219B_FifoOverflow(void)
{
    /* FIFO status bits are not affected by INT_POLAR. */
    return (CMT2300A_ReadReg(CMT2300A_CUS_FIFO_FLAG)
            & CMT2300A_MASK_RX_FIFO_OVF_FLG) ? TRUE : FALSE;
}
