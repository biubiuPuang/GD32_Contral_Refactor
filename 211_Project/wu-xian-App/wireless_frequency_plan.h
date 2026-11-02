#ifndef WIRELESS_FREQUENCY_PLAN_H
#define WIRELESS_FREQUENCY_PLAN_H

/* F50 candidate: one configured, fixed channel per TX/RX pair.
 * AN197: F = base + 2.5 kHz * FH_OFFSET * FH_CHANNEL.
 * CH0..49: nominal 433.1250..434.7175 MHz; FH_OFFSET=13.
 * This configures the official manual-hop mechanism, not a timed FHSS MAC.
 * Adjacent-channel isolation and 50 simultaneous pairs are NOT validated.
 * Keep this file byte-identical in the TX211 and RX221 applications.
 */
#define DEMO_CHANNEL_COUNT       50U
#define DEMO_CHANNEL_STEP        13U
#define DEMO_BASE_FREQUENCY_HZ    433125000UL
#define DEMO_CHANNEL_SPACING_HZ   32500UL
#define DEMO_AFC_OVF_TH           21U

/* Application parameters over the unchanged CMOSTEK BSP.
 * AN199, 26 MHz, DIVIDER=4: RX LO=RF+26MHz/92; TX LO=RF.
 * Integer/fractional words use truncation, matching the supplied .exp.
 * FREQ_DIVX_CODE=1, FREQ_VCO_BANK=1, PALDO_SEL=0, FSK_SWT=0.
 * Only the frequency bank replaces the original 433.920 MHz bank.
 * AN1971, TX/RX 40 ppm each: all 50 AFC_OVF_TH values are 21.
 */
static const u8 g_demo_frequency_registers[8] = {
    0x42U, 0x78U, 0xD9U, 0x1AU, /* RX N/K, DIVX */
    0x42U, 0x62U, 0x27U, 0x1AU  /* TX N/K, VCO bank */
};

#endif
