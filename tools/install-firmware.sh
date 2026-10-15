#!/bin/bash
# Copies the MT7961 ROM patch to /etc/bluetool under a name macOS bluetoothd looks for.
# bluetoothd picks any file in /etc/bluetool whose name contains "BT_RAM_CODE_MT7922"
# (the same patch file is shipped as BT_RAM_CODE_MT7961_1_2_hdr.bin in SimpleMtkBtFw/fw).
# Usage: tools/install-firmware.sh [path/to/BT_RAM_CODE_MT7961_1_2_hdr.bin]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
FW="${1:-}"
if [ -z "$FW" ]; then
    for c in "$HERE/../firmware/BT_RAM_CODE_MT7961_1_2_hdr.bin" \
             "$HERE/../SimpleMtkBtFw/fw/BT_RAM_CODE_MT7961_1_2_hdr.bin"; do
        [ -f "$c" ] && FW="$c" && break
    done
fi
[ -f "${FW:-/nonexistent}" ] || { echo "firmware file not found; pass it as the first argument" >&2; exit 1; }
sudo mkdir -p /etc/bluetool
sudo cp "$FW" /etc/bluetool/BT_RAM_CODE_MT7922_1_1_hdr.bin
sudo chown root:wheel /etc/bluetool/BT_RAM_CODE_MT7922_1_1_hdr.bin
sudo chmod 644 /etc/bluetool/BT_RAM_CODE_MT7922_1_1_hdr.bin
ls -la /etc/bluetool
echo "Done. Do a COLD boot (full power off 10+ seconds) before testing."
