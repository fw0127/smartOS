/* Kernel parameters for the Nano demo: small tables for 2 KB of SRAM,
 * optional ring-buffer modules off. */
#ifndef SOS_CFG_H
#define SOS_CFG_H

#define SOS_CFG_MAX_TASKS       4u
#define SOS_CFG_ERRLOG_DEPTH    4u
#define SOS_CFG_RING_ENABLE     0

/* Supervision (and with it the watchdog-serving health signal) every 250 ms
 * instead of the default 10 ms: the watchdog times out after 2 s, and an
 * idle Nano then wakes ~9 times a second instead of ~107. */
#define SOS_CFG_HEALTH_MIN_INTERVAL  250000u

#endif
