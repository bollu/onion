#!/bin/sh
# One-time: install this Mac's SSH public key on the device so deploys stop asking for a
# password. Separate from deploy_wifi.sh because it is setup, not part of a deploy.
#
# The key goes to /mnt/SDCARD/.ssh/authorized_keys -- the 'onion' account's home is
# /mnt/SDCARD (see static/configs/.tmp_update/config/passwd), so dropbear looks there.
# Because it lives on the card rather than in the bind-mounted /etc, it survives reboots
# and Onion updates.
#
#   make deploy-wifi-key        # prompts for the device password ('onion') once
set -e

DEVICE_HOST="${DEVICE_HOST:-192.168.100.100}"
DEVICE_USER="${DEVICE_USER:-onion}"
SSH_OPTS="-o MACs=hmac-sha1 -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5"

key=""
for candidate in "$HOME/.ssh/id_ed25519.pub" "$HOME/.ssh/id_rsa.pub" "$HOME/.ssh/id_ecdsa.pub"; do
    if [ -f "$candidate" ]; then
        key="$candidate"
        break
    fi
done

if [ -z "$key" ]; then
    printf 'error: no SSH public key found in ~/.ssh\n' >&2
    printf '       Make one with:  ssh-keygen -t ed25519\n' >&2
    exit 1
fi

printf 'Installing %s on %s@%s\n' "$key" "$DEVICE_USER" "$DEVICE_HOST"
printf "You will be asked for the device password once (default 'onion').\n\n"

# The key is piped rather than interpolated into the remote command: a key comment
# containing a quote would otherwise break the quoting. Appended only if absent, so
# re-running is harmless.
# shellcheck disable=SC2086  # SSH_OPTS is several flags and must word-split
ssh $SSH_OPTS "$DEVICE_USER@$DEVICE_HOST" '
    mkdir -p /mnt/SDCARD/.ssh &&
    touch /mnt/SDCARD/.ssh/authorized_keys &&
    key=$(cat) &&
    grep -qxF "$key" /mnt/SDCARD/.ssh/authorized_keys ||
        printf "%s\n" "$key" >> /mnt/SDCARD/.ssh/authorized_keys
' < "$key"

printf '\nDone. Check it with:  make deploy-wifi DRY_RUN=1\n'
