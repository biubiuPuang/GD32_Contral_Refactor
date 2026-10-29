#include "wired_control.h"
#include "wired_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static WiredLogic logic;
static WiredInput input;
static uint32_t now;

static void invariant(void)
{
    assert(!(logic.output.up && logic.output.down));
    assert(!logic.output.fast || logic.output.up || logic.output.down);
    if(logic.output.allow) {
        assert(logic.phase == WIRED_RUN);
        assert(!logic.output.inhibit && !logic.input.software_stop);
        assert(logic.output.up != logic.output.down);
        assert(!(logic.input.up && logic.input.down));
        assert(logic.output.up ? logic.input.up : logic.input.down);
        assert(!strcmp(WiredLogic_ModeName(&logic), logic.output.up ?
                       "you xian model up" : "you xian model down"));
    } else {
        assert(strncmp(WiredLogic_ModeName(&logic), "you xian model", 13U));
    }
}

static void step(void)
{
    WiredLogic_Step(&logic, input, now);
    invariant();
}

static void advance(uint32_t count)
{
    while(count--) { ++now; step(); }
}

static void reset_at(uint32_t start)
{
    memset(&input, 0, sizeof(input));
    now = start;
    WiredLogic_Init(&logic, now);
    invariant();
}

static void idle(void)
{
    reset_at(0U); advance(101U);
    assert(logic.phase == WIRED_IDLE && !logic.output.inhibit);
    assert(!strcmp(WiredLogic_ModeName(&logic), "sleep model"));
}

static void run_up(void)
{
    idle(); input.up = 1U; step(); advance(10U);
    assert(logic.phase == WIRED_PREPARE && !logic.output.allow);
    assert(!strcmp(WiredLogic_ModeName(&logic), "wait model"));
    advance(29U); assert(!logic.output.allow);
    advance(1U); assert(logic.output.allow && logic.output.up);
}

static void stop_and_restart(uint8_t down, uint8_t fast)
{
    /* Called immediately after RUN loses its target: KM goes off now, old
     * relays retained 50 ms, all released 30 ms, new prepared 30 ms. */
    assert(logic.phase == WIRED_KM_OFF && !logic.output.allow);
    advance(49U); assert(logic.output.up && !logic.output.down);
    advance(1U);
    assert(logic.phase == WIRED_RELEASE);
    assert(!logic.output.up && !logic.output.down && !logic.output.fast);
    advance(29U); assert(logic.phase == WIRED_RELEASE);
    advance(1U); assert(logic.phase == WIRED_IDLE);
    advance(1U); assert(logic.phase == WIRED_PREPARE);
    assert(logic.output.down == down && logic.output.up == !down);
    assert(logic.output.fast == fast && !logic.output.allow);
    advance(29U); assert(!logic.output.allow);
    advance(1U); assert(logic.output.allow);
}

int main(void)
{
    unsigned i;
    uint32_t seed = 0x12345678U;
    uint32_t off_at = 0U, changed_at = 0U, released_at = 0U;

    /* Startup permits a continuously held button, per the final requirement. */
    reset_at(0U); input.up = 1U; step();
    advance(49U); assert(logic.output.inhibit && !logic.output.allow);
    advance(1U); assert(!logic.output.inhibit && !logic.output.allow);
    advance(49U); assert(!logic.output.up && !logic.output.allow);
    advance(32U); assert(logic.output.up && logic.output.allow);

    /* Fast by itself must never enable a direction or KM. */
    idle(); input.fast = 1U; step(); advance(100U);
    assert(!logic.output.fast && !logic.output.allow);
    assert(!strcmp(WiredLogic_ModeName(&logic), "sleep model"));

    /* Assertion bounce: never energize on pulses shorter than 10 ms. */
    idle();
    for(i = 0; i < 20U; ++i) {
        input.up = 1U; step(); advance(9U);
        input.up = 0U; step(); advance(1U);
        assert(!logic.output.up && !logic.output.allow);
    }
    input.up = 1U; step(); advance(10U);
    assert(logic.phase == WIRED_PREPARE);
    /* Release during preparation cancels before RUN. */
    advance(29U); input.up = 0U; step();
    assert(!logic.output.allow && logic.phase == WIRED_KM_OFF);
    advance(81U); assert(logic.phase == WIRED_IDLE);

    /* A single sample of loss removes permission immediately. Restoring the
     * held signal still has to complete the full break-before-make sequence. */
    run_up(); input.up = 0U; step();
    assert(!logic.output.allow && logic.output.up);
    input.up = 1U; step(); stop_and_restart(0U, 0U);

    /* Reversal cannot energize the other direction before old release. */
    run_up(); input.up = 0U; input.down = 1U; step();
    stop_and_restart(1U, 0U);

    /* Changing speed also interrupts KM; no live speed switching. */
    run_up(); input.fast = 1U; step(); advance(9U);
    assert(logic.output.allow && !logic.output.fast);
    advance(1U); stop_and_restart(0U, 1U);
    input.fast = 0U; step();
    assert(!logic.output.allow && logic.output.fast);
    stop_and_restart(0U, 0U);

    /* Conflict is immediate, but PB11 remains low to observe its clearance. */
    run_up(); input.down = 1U; step();
    assert(!logic.output.allow && !logic.output.inhibit);
    assert(!strcmp(WiredLogic_ModeName(&logic), "stop model"));
    advance(200U);
    assert(logic.phase == WIRED_IDLE && logic.reason == WIRED_REASON_CONFLICT);
    input.down = 0U; step(); advance(30U);
    assert(logic.output.up && logic.output.allow);

    /* Software stop immediately cuts both permits, then releases relays. */
    run_up(); input.software_stop = 1U; step();
    assert(logic.output.inhibit && !logic.output.allow && logic.output.up);
    advance(50U); assert(!logic.output.up);
    advance(30U); assert(logic.phase == WIRED_BLOCKED);
    advance(100U); assert(logic.output.inhibit && !logic.output.allow);
    input.software_stop = 0U; step();
    assert(!logic.output.inhibit && logic.phase == WIRED_PERMIT_SETTLE);
    advance(82U); assert(logic.output.up && logic.output.allow);
    /* Very short stop requests cannot bypass the release sequence. */
    input.software_stop = 1U; step();
    input.software_stop = 0U; step();
    advance(49U); assert(!logic.output.allow && logic.output.up);
    advance(1U); assert(!logic.output.up);
    advance(29U); assert(logic.phase == WIRED_RELEASE);
    advance(1U); assert(logic.phase == WIRED_PERMIT_SETTLE);
    advance(82U); assert(logic.output.allow);

    /* Timer arithmetic remains valid across uint32 rollover. */
    reset_at(0xFFFFFFF0U); input.down = 1U; step(); advance(131U);
    assert(logic.output.allow && logic.output.down);
    input.down = 0U; step(); advance(80U);
    assert(logic.phase == WIRED_IDLE && !logic.output.allow);

    /* Invalid internal state fails closed and stays inhibited. */
    logic.phase = (WiredPhase)99; step(); advance(500U);
    assert(logic.fault && logic.output.inhibit && !logic.output.allow);

    /* Deterministic input trace: exercise thousands of transitions and
     * independently check the electrical ordering/minimum wait invariants. */
    reset_at(0U);
    for(i = 0U; i < 200000U; ++i) {
        WiredOutput before = logic.output;
        if(i % 47U == 0U) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            input.up = (uint8_t)(seed & 1U);
            input.down = (uint8_t)((seed >> 1) & 1U);
            input.fast = (uint8_t)((seed >> 2) & 1U);
            input.software_stop = (uint8_t)((seed & 0xF0U) == 0U);
        }
        ++now; step();
        if(before.allow && !logic.output.allow) off_at = now;
        if(before.up != logic.output.up || before.down != logic.output.down ||
           before.fast != logic.output.fast) {
            assert(!before.allow && !logic.output.allow);
            if(before.up || before.down) assert((uint32_t)(now - off_at) >= 50U);
            if(!before.up && !before.down && (logic.output.up || logic.output.down))
                assert((uint32_t)(now - released_at) >= 30U);
            changed_at = now;
            if(!logic.output.up && !logic.output.down) released_at = now;
        }
        if(!before.allow && logic.output.allow)
            assert((uint32_t)(now - changed_at) >= 30U);
    }
    puts("PASS: startup, debounce, immediate stop, up/down, fast-only, reversal, speed, conflict, software stop, recovery, rollover, 200000-step interlocks.");
    return 0;
}
