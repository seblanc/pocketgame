#pragma once
#include <stdbool.h>

/* Battery voltage on IO1 (VBAT_ADC, 1:3 divider). */
bool battery_init(void);
/* battery voltage in mV (0 if the ADC is unavailable) */
int battery_mv(void);
/* charge estimate 0..100 from a Li-ion discharge curve */
int battery_pct(int mv);
