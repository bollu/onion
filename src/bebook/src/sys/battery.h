#ifndef SYS_BATTERY_H_
#define SYS_BATTERY_H_

// Current battery charge, 0-100, or -1 when it cannot be read.
//
// Onion's batmon daemon keeps the level in /tmp/percBat as a plain integer, which is
// the same file Onion's own battery_getPercentage() reads. -1 is the ordinary case
// off-device, and on-device it means batmon is not running -- either way there is
// nothing to show rather than an error to report.
//
// Deliberately not reusing Onion's system/battery.h despite -I../common being on the
// include path. That header pulls in six more (device_model, system, file, log,
// msleep, process), each carrying static state and function bodies, which is a lot of
// C to drag into this C++ build for one integer. The hash in util/rom_screen.cpp is
// shared because it is an algorithm that must stay bit-identical to Onion's forever;
// this is a file format, and a far more stable interface to depend on.
int read_battery_percent();

#endif
