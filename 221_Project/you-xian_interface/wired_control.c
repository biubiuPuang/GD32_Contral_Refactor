#include "wired_control.h"
#include "wired_config.h"
#include <string.h>

static void Wired_SetPhase(WiredLogic *logic, WiredPhase phase, uint32_t now)
{
    logic->phase = phase;
    logic->phase_at = now;
}

static void Wired_AllRelaysOff(WiredLogic *logic)
{
    logic->output.up = 0U;
    logic->output.down = 0U;
    logic->output.fast = 0U;
    logic->output.allow = 0U;
}

static void Wired_ResetInputs(WiredLogic *logic, uint32_t now)
{
    unsigned i;
    logic->raw_bits = 0U;
    logic->stable_bits = 0U;
    for(i = 0U; i < 3U; ++i) logic->on_since[i] = now;
}

/* Qualify assertion for 10 ms. Deassertion is immediate so a release,
 * direction-circuit interruption or limit removes KM permission promptly. */
static void Wired_Filter(WiredLogic *logic, uint8_t raw, uint32_t now)
{
    unsigned i;
    for(i = 0U; i < 3U; ++i) {
        uint8_t bit = (uint8_t)(1U << i);
        if(!(raw & bit)) {
            logic->stable_bits &= (uint8_t)~bit;
            logic->on_since[i] = now;
        } else {
            if(!(logic->raw_bits & bit)) logic->on_since[i] = now;
            if((uint32_t)(now - logic->on_since[i]) >= WIRED_INPUT_ON_MS)
                logic->stable_bits |= bit;
        }
    }
    logic->raw_bits = raw;
}

static void Wired_BeginStop(WiredLogic *logic, WiredReason reason, uint32_t now)
{
    logic->output.allow = 0U; /* Always remove KM before changing relay drives. */
    logic->reason = reason;
    Wired_SetPhase(logic, WIRED_KM_OFF, now);
}

void WiredLogic_Init(WiredLogic *logic, uint32_t now_ms)
{
    memset(logic, 0, sizeof(*logic));
    logic->output.inhibit = 1U;
    logic->reason = WIRED_REASON_STARTUP;
    Wired_SetPhase(logic, WIRED_STARTUP, now_ms);
    Wired_ResetInputs(logic, now_ms);
}

void WiredLogic_Step(WiredLogic *logic, WiredInput input, uint32_t now_ms)
{
    uint8_t raw = (uint8_t)((input.up ? 1U : 0U) |
                           (input.down ? 2U : 0U) | (input.fast ? 4U : 0U));
    uint8_t conflict = (uint8_t)((raw & 3U) == 3U);
    uint8_t fast;
    WiredReason stop_reason;
    WiredDirection direction;
    if(logic->fault) input.software_stop = 1U;
    logic->input = input;
    Wired_Filter(logic, raw, now_ms);

    /* Inhibit is independent of the direction feedback. Keeping PB11 high
     * until PB3/PB4 reappear would deadlock the operation circuit. */
    if(input.software_stop) {
        logic->output.inhibit = 1U;
        logic->output.allow = 0U;
        logic->reason = WIRED_REASON_SOFTWARE_STOP;
        if(logic->phase == WIRED_RUN || logic->phase == WIRED_PREPARE)
            Wired_BeginStop(logic, WIRED_REASON_SOFTWARE_STOP, now_ms);
        else if(logic->phase != WIRED_KM_OFF && logic->phase != WIRED_RELEASE)
            Wired_SetPhase(logic, WIRED_BLOCKED, now_ms);
    } else if(logic->previous_stop) {
        logic->output.inhibit = 0U;
        logic->resume_settle = 1U;
        Wired_ResetInputs(logic, now_ms);
    }
    logic->previous_stop = (uint8_t)(input.software_stop != 0U);

    direction = WIRED_DIR_NONE;
    if(!conflict) {
        if((raw & 1U) && (logic->stable_bits & 1U)) direction = WIRED_DIR_UP;
        if((raw & 2U) && (logic->stable_bits & 2U)) direction = WIRED_DIR_DOWN;
    }
    fast = (uint8_t)(direction != WIRED_DIR_NONE &&
                    (raw & 4U) && (logic->stable_bits & 4U));
    logic->target = direction;
    stop_reason = conflict ? WIRED_REASON_CONFLICT :
                  (direction == WIRED_DIR_NONE ? WIRED_REASON_NO_DIRECTION : WIRED_REASON_CHANGE);

    switch(logic->phase) {
    case WIRED_STARTUP:
        if((uint32_t)(now_ms - logic->phase_at) >= WIRED_STARTUP_HOLD_MS) {
            logic->output.inhibit = 0U;
            Wired_ResetInputs(logic, now_ms);
            Wired_SetPhase(logic, WIRED_PERMIT_SETTLE, now_ms);
        }
        break;
    case WIRED_BLOCKED:
        Wired_AllRelaysOff(logic);
        if(!input.software_stop) {
            logic->resume_settle = 0U;
            Wired_SetPhase(logic, WIRED_PERMIT_SETTLE, now_ms);
        }
        break;
    case WIRED_PERMIT_SETTLE:
        if((uint32_t)(now_ms - logic->phase_at) >= WIRED_PERMIT_SETTLE_MS)
            Wired_SetPhase(logic, WIRED_IDLE, now_ms);
        break;
    case WIRED_IDLE:
        logic->reason = conflict ? WIRED_REASON_CONFLICT : WIRED_REASON_NO_DIRECTION;
        if(direction != WIRED_DIR_NONE) {
            logic->output.up = (uint8_t)(direction == WIRED_DIR_UP);
            logic->output.down = (uint8_t)(direction == WIRED_DIR_DOWN);
            logic->output.fast = fast;
            logic->reason = WIRED_REASON_NONE;
            Wired_SetPhase(logic, WIRED_PREPARE, now_ms);
        }
        break;
    case WIRED_PREPARE:
    case WIRED_RUN:
        if(direction == WIRED_DIR_NONE ||
           logic->output.up != (uint8_t)(direction == WIRED_DIR_UP) ||
           logic->output.down != (uint8_t)(direction == WIRED_DIR_DOWN) ||
           logic->output.fast != fast) {
            Wired_BeginStop(logic, stop_reason, now_ms);
        } else if(logic->phase == WIRED_PREPARE &&
                  (uint32_t)(now_ms - logic->phase_at) >= WIRED_RELAY_PREPARE_MS) {
            logic->output.allow = 1U;
            logic->reason = WIRED_REASON_NONE;
            Wired_SetPhase(logic, WIRED_RUN, now_ms);
        }
        break;
    case WIRED_KM_OFF:
        if((uint32_t)(now_ms - logic->phase_at) >= WIRED_KM_OFF_MS) {
            Wired_AllRelaysOff(logic);
            Wired_SetPhase(logic, WIRED_RELEASE, now_ms);
        }
        break;
    case WIRED_RELEASE:
        if((uint32_t)(now_ms - logic->phase_at) >= WIRED_RELAY_RELEASE_MS) {
            if(input.software_stop) Wired_SetPhase(logic, WIRED_BLOCKED, now_ms);
            else if(logic->resume_settle) {
                logic->resume_settle = 0U;
                Wired_SetPhase(logic, WIRED_PERMIT_SETTLE, now_ms);
            } else Wired_SetPhase(logic, WIRED_IDLE, now_ms);
        }
        break;
    default:
        logic->fault = 1U; /* Corrupt internal phase requires reinitialization. */
        Wired_AllRelaysOff(logic);
        logic->output.inhibit = 1U;
        logic->reason = WIRED_REASON_SOFTWARE_STOP;
        Wired_SetPhase(logic, WIRED_BLOCKED, now_ms);
        break;
    }
}

const char *WiredLogic_PhaseName(WiredPhase phase)
{
    switch(phase) {
    case WIRED_STARTUP: return "STARTUP";
    case WIRED_PERMIT_SETTLE: return "PERMIT_SETTLE";
    case WIRED_IDLE: return "IDLE";
    case WIRED_PREPARE: return "PREPARE";
    case WIRED_RUN: return "RUN";
    case WIRED_KM_OFF: return "KM_OFF";
    case WIRED_RELEASE: return "RELEASE";
    case WIRED_BLOCKED: return "BLOCKED";
    default: return "INVALID";
    }
}

const char *WiredLogic_ReasonName(WiredReason reason)
{
    switch(reason) {
    case WIRED_REASON_NONE: return "NONE";
    case WIRED_REASON_STARTUP: return "STARTUP";
    case WIRED_REASON_NO_DIRECTION: return "NO_DIRECTION";
    case WIRED_REASON_CONFLICT: return "DIRECTION_CONFLICT";
    case WIRED_REASON_CHANGE: return "DIRECTION_OR_SPEED_CHANGE";
    case WIRED_REASON_SOFTWARE_STOP: return "SOFTWARE_STOP";
    default: return "INVALID";
    }
}

const char *WiredLogic_ModeName(const WiredLogic *logic)
{
    if(logic->phase == WIRED_RUN && logic->output.allow && !logic->output.inhibit) {
        if(logic->output.up && !logic->output.down) return "you xian model up";
        if(logic->output.down && !logic->output.up) return "you xian model down";
    }
    if(logic->output.inhibit || logic->input.software_stop ||
       (logic->input.up && logic->input.down) ||
       logic->phase == WIRED_KM_OFF || logic->phase == WIRED_RELEASE ||
       logic->phase == WIRED_BLOCKED) return "stop model";
    if(logic->phase == WIRED_IDLE && logic->target == WIRED_DIR_NONE &&
       !logic->input.up && !logic->input.down) return "sleep model";
    return "wait model";
}
