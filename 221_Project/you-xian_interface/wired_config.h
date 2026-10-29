#ifndef WIRED_CONFIG_H
#define WIRED_CONFIG_H

/* Initial values from the old project; verify relay/contact timing on the
 * complete board. These waits do not measure mechanical stopping time. */
#define WIRED_STARTUP_HOLD_MS       50U
#define WIRED_PERMIT_SETTLE_MS      50U
#define WIRED_INPUT_ON_MS           10U
#define WIRED_KM_OFF_MS             50U
#define WIRED_RELAY_RELEASE_MS      30U
#define WIRED_RELAY_PREPARE_MS      30U
#define WIRED_REPORT_MS             500U
#define WIRED_RF_SETTLE_MS          20U /* Initial relay-path settling value. */

#endif
