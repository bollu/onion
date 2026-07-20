#ifndef SYS_BATTERY_H_
#define SYS_BATTERY_H_

// Charge 0-100 from batmon's /tmp/percBat, or -1 when unreadable (nothing to show).
// Not reusing Onion's system/battery.h: it drags in six headers for one integer.
int read_battery_percent();

#endif
