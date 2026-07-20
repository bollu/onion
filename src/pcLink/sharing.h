#ifndef PCLINK_SHARING_H__
#define PCLINK_SHARING_H__

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/log.h"
#include "utils/msleep.h"
#include "utils/netinfo.h"
#include "utils/process.h"
#include "utils/str.h"

// Brings up the device's own AP plus samba, reusing the scripts netplay already uses.
#define NET_SCRIPT_DIR "/mnt/SDCARD/.tmp_update/script/network"
#define DHCP_LEASES "/mnt/SDCARD/.tmp_update/config/dhcp.leases"
#define HOSTAPD_CONF "/mnt/SDCARD/.tmp_update/config/hostapd.conf"

// The AP interface. hotspot_create.sh puts wlan0 down and runs hostapd on wlan1,
// which the RTL8188FU driver provides as a second virtual interface.
#define AP_INTERFACE "wlan1"

// Samba account created by start_smbd.sh.
#define SMB_USER "onion"
#define SMB_PASS "onion"

typedef enum SharingStep {
    STEP_HOTSPOT = 0, // hostapd + dnsmasq
    STEP_DIRS,        // runtime dirs samba needs
    STEP_SMBD,        // the file server itself
    STEP_COUNT
} SharingStep;

static const char *sharing_stepLabel(SharingStep step)
{
    switch (step) {
    case STEP_HOTSPOT:
        return "Starting hotspot...";
    case STEP_DIRS:
        return "Preparing shares...";
    case STEP_SMBD:
        return "Starting file server...";
    default:
        return "";
    }
}

bool sharing_hotspotRunning(void) { return process_isRunning("hostapd"); }
bool sharing_smbdRunning(void) { return process_isRunning("smbd"); }

// Read from hostapd.conf, not hardcoded, so the on-screen instructions stay true.
void sharing_getApCredentials(char *ssid_out, char *pass_out, size_t size)
{
    snprintf(ssid_out, size, "%s", "(unknown)");
    snprintf(pass_out, size, "%s", "(unknown)");

    FILE *fp = fopen(HOSTAPD_CONF, "r");
    if (fp == NULL) {
        printf_debug("Cannot read %s\n", HOSTAPD_CONF);
        return;
    }

    char line[STR_MAX];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *newline = strchr(line, '\n');
        if (newline != NULL)
            *newline = '\0';

        if (strncmp(line, "ssid=", 5) == 0)
            snprintf(ssid_out, size, "%s", line + 5);
        else if (strncmp(line, "wpa_passphrase=", 15) == 0)
            snprintf(pass_out, size, "%s", line + 15);
    }

    fclose(fp);
}

// Bare IPv4 of the AP interface; netinfo returns a label, so parse the address back.
void sharing_getApAddress(char *out, size_t size)
{
    char label[STR_MAX] = "";
    netinfo_getIpAddress(label, AP_INTERFACE);

    if (sscanf(label, "IP address: %15s", out) != 1)
        snprintf(out, size, "%s", "0.0.0.0");
}

// DHCP leases handed out, i.e. computers currently joined.
int sharing_clientCount(void)
{
    FILE *fp = fopen(DHCP_LEASES, "r");
    if (fp == NULL)
        return 0;

    int count = 0;
    char line[STR_MAX];
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strlen(line) > 1)
            count++;
    }

    fclose(fp);
    return count;
}

// One startup step, so the caller can draw progress between the slow ones.
bool sharing_runStep(SharingStep step)
{
    switch (step) {
    case STEP_HOTSPOT:
        system("sh " NET_SCRIPT_DIR "/hotspot_create.sh 2>&1");
        return sharing_hotspotRunning();

    case STEP_DIRS:
        // start_smbd.sh assumes these exist; update_networking.sh creates them
        // before calling it, and we bypass that path deliberately (see below).
        system("mkdir -p /var/lib/samba /var/run/samba/ncalrpc /var/private /var/log");
        return true;

    case STEP_SMBD:
        // Called directly rather than via `update_networking.sh smbd toggle`: the
        // toggle sets a persistent flag that a periodic check re-evaluates, which
        // would fight this app for control of smbd and leave the flag set on exit.
        system("sh " NET_SCRIPT_DIR "/start_smbd.sh " SMB_PASS " 2>&1 &");

        // smbd is backgrounded by the script, so give it a moment to appear.
        for (int i = 0; i < 20 && !sharing_smbdRunning(); i++)
            msleep(100);

        return sharing_smbdRunning();

    default:
        return false;
    }
}

// Tears everything down and restores normal WiFi.
void sharing_stop(void)
{
    if (sharing_smbdRunning())
        process_killall("smbd");

    system("sh " NET_SCRIPT_DIR "/hotspot_cleanup.sh 2>&1");
}

#endif // PCLINK_SHARING_H__
