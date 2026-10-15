# MediaTek MT7921 / MT7922 Bluetooth on macOS Sequoia (native `bluetoothd` path)

Status: **working** on Foxconn MT7921 (USB `0489:E0CD`), macOS 15.8.1 (24H32), OpenCore, AMD platform.
Bluetooth powers on, controller reports `Chipset: SRS_7922`, scanning works.

## Why this is different from "just load the firmware"

macOS 15 `bluetoothd` ships its own MediaTek driver (a port of Linux `btmtk`: `MtkLoadRomPatch`,
`MtkPowerOn`, transport "USB SRS", chipset type 0xB, ROM patch from `/etc/bluetool`). It is
whitelisted for one USB device only (**Foxconn 0489:E0D8**). For everything else `bluetoothd` falls back
to the Broadcom code path, which never works with MediaTek silicon (`Chipset: BCM_4350C2`, address `NULL`).

Credits for the discovery: the thread *"WiFi 6(E) & Bluetooth 5.x MEDIATEK chipset on native Sequoia &
Tahoe"* on tonymacx86 (MT7922, Tahoe). This repository adds MT7921 on Sequoia and the reasons below.

## What is needed (all five pieces)

| # | Piece | Why |
|---|---|---|
| 1 | `BlueToolFixup.kext` from `BlueToolFixup-MediaTek/` (**replaces** stock BlueToolFixup) | stock fixes + whitelist `E0D8 -> your PID` inside `bluetoothd` |
| 2 | `SimpleMtkBtFw.kext` (unchanged) | loads the ROM patch; Apple's own loader stalls the MT7921 ROM after the first 64-byte packet (its WMT writes use `bRequest=1, wValue=0x30`; Linux and this driver use `bRequest=0`) |
| 3 | NVRAM `bluetoothInternalControllerInfo` = 14 zero bytes, `bluetoothExternalDongleFailed` = 1 zero byte | without them `bluetoothd` believes there is no internal controller and never probes USB dongles (`External Bluetooth dongle was not found`) |
| 4 | `/etc/bluetool/BT_RAM_CODE_MT7922_1_1_hdr.bin` (any name containing `BT_RAM_CODE_MT7922`) | `bluetoothd` reads/parses it; with the patch already loaded by SimpleMtkBtFw it logs `no need to load rom patch` |
| 5 | **Cold boot** (power off 10+ s) after every change | warm reboots leave the chip in a state where the first WMT command times out |

Do not use `SimpleMtkBtPatcher.kext` for this setup (Lilu `UserPatcher` does not patch `bluetoothd` on
current macOS; the Intel random-address hook is not needed).
Do **not** set the `USB Product Name` property to "Bluetooth USB Host Controller": `Found_USB_Dongle`
treats such a device as the internal controller and skips it.

## Install

See the step-by-step guide in the [README](../README.md#mt7921--mt7922-on-macos-sequoia--installation-guide)
(Russian: [INSTALL_RU.md](INSTALL_RU.md)).

## Expected log (`log show --last boot --predicate 'process == "bluetoothd"'`)

```
Found external Bluetooth dongle -- setting chipset to THIRD_PARTY_DONGLE
HCI Transport is set to USB SRS
[OI_HciIfc_Init_USB_SRS] -- Read Chip ID 0x00007961
Found firmware: BT_RAM_CODE_MT7922_1_1_hdr.bin
mtkSendFwRomPatch: patch_status 2
mtkSendFwRomPatch: no need to load rom patch section0
MtkPowerOn: successful
Stack initialization complete
SRS_7922 supports the following VSCs
```

## Troubleshooting

* `External Bluetooth dongle was not found` with no `Found_USB_Dongle` lines -> NVRAM variables missing.
* `HCI Transport is set to H4BC` / `/dev/cu.BLTH` timeouts -> device name not set (SimpleMtkBtFw not
  loaded) or BlueToolFixup not loaded.
* `FoundBluetoothInterfaces() ... returned true`, transport `USB` (not `USB SRS`) and
  `ReadLocalVersionInformation failed STATUS 408` -> the whitelist patch did not apply (wrong PID, or
  stock BlueToolFixup still loaded).
* `patch_status 0` then `endpoint 0x00 ... timeout: 64 bytes transferred` -> firmware was not loaded by
  SimpleMtkBtFw before `bluetoothd` tried (load order / driver not matched), or the chip was not cold booted.
* Everything stops working after a warm reboot -> do a full power off.

## Notes

* Verified only on macOS 15.8.1 (24H32). Tahoe lists more Foxconn PIDs natively (`E0E0, E0F1, E0F2,
  E0F6`, plus `0E8D:7922`, `13D3:3568`); the same patch idea applies there but byte patterns differ.
* Patching of `bluetoothd` happens in the kernel `cs_validate_page` hook (as in stock BlueToolFixup);
  `bluetoothd` itself and the signed system volume are not modified on disk.
* The firmware blobs come from linux-firmware (MediaTek, redistributable as-is) and are already part of
  upstream SimpleMtkBtFw.
