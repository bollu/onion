# pcLink

Shares the SD card with a computer over the device's **own** Wi-Fi hotspot. The
Miyoo becomes an access point, your computer joins it directly, and the card is
served over SMB. No router, no internet, no network credentials beyond one fixed
password.

**Status: builds clean, screens rendered offscreen, never run on hardware.**
Nothing in the start/stop path has been executed on a device — see Verification.

## Why not a USB cable

The Miyoo Mini Plus USB-C port is **charge-only**: its D+/D- lines are not routed
to the SoC. [1arthur1/MiyooMini-USB-Support](https://github.com/1arthur1/MiyooMini-USB-Support)
documents a hardware mod that solders SoC pins 111/112 (DM_P2/DP_P2) to the
connector — which would be pointless if the traces existed — and even then it
yields USB *host* mode and needs external 5V. A
[linux-chenxing](https://github.com/linux-chenxing/linux-chenxing.org/discussions/41)
dev separately states the SSD202D has no USB device port, and the stock kernel is
closed, so a UDC plus `g_mass_storage` cannot be added. No software can create a
data path the board does not have.

## Why not the campus Wi-Fi

eduroam is WPA2-Enterprise. Onion has **no** EAP support anywhere (no matches for
`eap`/`WPA-EAP`/`peap`/`mschap` in the tree; `wpa_supplicant.reset` is two lines),
and MainUI's Wi-Fi picker is PSK-only, so an enterprise identity cannot even be
entered. Campus networks also typically enforce client isolation, which would block
the computer from reaching the device's SMB port even if it did associate.

The hotspot sidesteps both, and works on a train.

## Usage

Launch it, press **A**. The screen then shows what to type:

```
1. Join this Wi-Fi network:
     network:  MiyooMini+APOnionOS
     password: onionos+
2. Open this address:
     smb://192.168.100.100
     user: onion    password: onion
```

On macOS: Finder → Go → Connect to Server. Press **B** to stop sharing and restore
normal Wi-Fi.

## How it works

Nothing here is new — Onion already shipped every piece for netplay, which sources
the same hotspot script (`easy-netplay_server.sh:201`). This drives them and shows
the user what to type.

| Step | Reuses |
| --- | --- |
| Bring up the AP (`hostapd` + `dnsmasq`, `wlan0` down / `wlan1` up) | `.tmp_update/script/network/hotspot_create.sh` |
| Start samba | `.tmp_update/script/network/start_smbd.sh` |
| Tear down, restore station Wi-Fi + DHCP | `.tmp_update/script/network/hotspot_cleanup.sh` |
| SSID / passphrase (read at runtime, not hardcoded) | `.tmp_update/config/hostapd.conf` |
| Address, DHCP pool, lease file | `.tmp_update/config/dnsmasq.conf` |
| Which folders are shared | `.tmp_update/config/smb.conf` |

`wlan1` exists because the Realtek **RTL8188FU** driver (`8188fu.ko`) is built with
`CONFIG_CONCURRENT_MODE` and exports the `HOSTAPD_ACL_*` hooks — it provides a second
virtual interface on the one physical radio for AP mode.

Two deliberate choices:

- **`start_smbd.sh` is called directly**, not via `update_networking.sh smbd toggle`.
  The toggle sets a persistent flag that a periodic check re-evaluates, which would
  fight this app for control of smbd and leave the flag set after exit.
- **Teardown is unconditional**, and runs from `SIGTERM`/`SIGINT` too. `pressMenu2Kill`
  can kill the app mid-share, and leaving the device in AP mode with `wlan0` down
  looks exactly like "Wi-Fi is broken".

## Expect it to be slow

The RTL8188FU is USB-attached, 2.4GHz only, **1T1R**. `hostapd.conf` sets `hw_mode=g`
with `ieee80211n=1` and no `ht_capab`, so that is HT20 — a 72 Mbps PHY ceiling and
realistically a few MB/s after samba on this SoC. Fine for saves, configs and a few
ROMs; slow for a PS1 library. **This number has not been measured** — do so and
record it here.

`channel=6` is hardcoded in `hostapd.conf` and may be congested. A channel selector
(1/6/11) is an easy win if throughput is poor.

## Verification

**Step 1 must be done on a device, by hand, before trusting any of this.** Over
telnet or SSH:

```sh
sh /mnt/SDCARD/.tmp_update/script/network/hotspot_create.sh
pgrep hostapd && pgrep dnsmasq && ifconfig wlan1
sh /mnt/SDCARD/.tmp_update/script/network/start_smbd.sh onion
pgrep smbd
```

Then join `MiyooMini+APOnionOS` / `onionos+` and `open smb://192.168.100.100`.
Tear down with `killall smbd; sh .../hotspot_cleanup.sh` and confirm normal Wi-Fi
returns (`ifconfig wlan0`, `pgrep wpa_supplicant`).

**If that fails, the app cannot work and the scripts need fixing first.** The
hotspot has only ever run inside netplay, which defines `log`/`cleanup` helpers
first; the scripts fall back to `echo` without them, but this path is untested
standalone.

Then: `make with-toolchain CMD=pc-link`, copy `build/App/PCLink/` to
`/mnt/SDCARD/App/PCLink/`, run it, mount from the Mac, and exit via **MENU** —
confirming Wi-Fi comes back. Also kill it mid-share to exercise the signal handler.

Screens can be checked without a device using the offscreen render harness described
in `src/musicPlayer/README.md` (point it at `src/pcLink` and call `render_*`).

## Known gaps

- **Never run on hardware.** The entire start/stop path is unexercised.
- **Startup blocks the UI.** Each step draws its frame first, then runs
  synchronously, so the app is unresponsive for a few seconds per step. Acceptable,
  but it cannot be cancelled mid-startup.
- **`sharing_stop()` runs on exit even if sharing never started.** Harmless —
  `hotspot_cleanup.sh` branches on `pgrep hostapd` — but it does restart
  `wpa_supplicant` and `udhcpc`, so launching and immediately quitting will bounce
  Wi-Fi. Worth guarding once confirmed on device.
- **No QR code** for the SMB URL, which would save typing.
- **Shares whatever `smb.conf` lists** (BIOS, Media, Roms, Saves, …), not the whole
  card.
