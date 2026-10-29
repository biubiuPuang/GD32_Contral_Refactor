#ifndef WIRED_CONTROL_H
#define WIRED_CONTROL_H

#include <stdint.h>

/* Project-defined types/functions. No radio source is permitted to drive
 * these execution GPIOs separately when wireless is integrated later. */
typedef enum {
    WIRED_DIR_NONE = 0, WIRED_DIR_UP, WIRED_DIR_DOWN
} WiredDirection;

typedef enum {
    WIRED_STARTUP = 0, WIRED_PERMIT_SETTLE, WIRED_IDLE,
    WIRED_PREPARE, WIRED_RUN, WIRED_KM_OFF, WIRED_RELEASE, WIRED_BLOCKED
} WiredPhase;

typedef enum {
    WIRED_REASON_NONE = 0, WIRED_REASON_STARTUP, WIRED_REASON_NO_DIRECTION,
    WIRED_REASON_CONFLICT, WIRED_REASON_CHANGE, WIRED_REASON_SOFTWARE_STOP
} WiredReason;

typedef struct {
    uint8_t up, down, fast; /* 1 = active (GPIO low); not motor feedback */
    uint8_t software_stop; /* explicit software inhibit, NOT an E-stop input */
} WiredInput;

typedef struct {
    uint8_t up, down, fast; /* 1 = relay drive active (GPIO low) */
    uint8_t allow;         /* 1 = PB6 low, KM1 permission only */
    uint8_t inhibit;       /* 1 = PB11 high, cuts operation circuit */
} WiredOutput;

typedef struct {
    WiredPhase phase;
    WiredReason reason;
    WiredDirection target;
    WiredInput input;
    WiredOutput output;
    uint32_t phase_at, on_since[3];
    uint8_t raw_bits, stable_bits, previous_stop, resume_settle, fault;
} WiredLogic;

/* Pure control logic: nonblocking, caller supplies monotonic milliseconds. */
void WiredLogic_Init(WiredLogic *logic, uint32_t now_ms);
void WiredLogic_Step(WiredLogic *logic, WiredInput input, uint32_t now_ms);
const char *WiredLogic_PhaseName(WiredPhase phase);
const char *WiredLogic_ReasonName(WiredReason reason);
const char *WiredLogic_ModeName(const WiredLogic *logic);

/* Board entry points: call Init once, Process continuously without delays.
 * Owns PA8/9/10, PB3/4/5/6/11/12/13, TIMER13 and USART1 output.
 * The existing radio Demo must not be initialized alongside this app. */
void WiredControl_Init(void);
void WiredControl_Process(void);
void WiredControl_SetSoftwareStop(uint8_t active);

#endif
