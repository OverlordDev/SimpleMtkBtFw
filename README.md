# SimpleMtkBtFw

[中文说明](README_CN.md)

SimpleMtkBtFw is a MediaTek USB Bluetooth driver for macOS.

It is not a Wi-Fi driver and not a standalone Bluetooth stack. After firmware initialization is complete, Bluetooth functionality is handled by macOS.

## Supported Hardware

See [SimpleMtkBtFw/Info.plist](SimpleMtkBtFw/Info.plist) for the exact USB matching list.

Current personalities cover MT7921, MT7922, MT7922A, and a generic MediaTek Bluetooth USB class match. At runtime, the firmware path only accepts MediaTek internal device IDs `0x7961` and `0x7922`.

Included firmware:

- `BT_RAM_CODE_MT7961_1_2_hdr.bin`
- `BT_RAM_CODE_MT7961_1a_2_hdr.bin`
- `BT_RAM_CODE_MT7922_1_1_hdr.bin`

## Build

From the project root:

```sh
xcodebuild -project SimpleMtkBtFw.xcodeproj -scheme SimpleMtkBtFw -configuration Release build
xcodebuild -project SimpleMtkBtFw.xcodeproj -scheme SimpleMtkBtPatcher -configuration Release build
```

The project contains two KEXTs:

- `SimpleMtkBtFw.kext`: loads MediaTek Bluetooth firmware.
- `SimpleMtkBtPatcher.kext`: Lilu-based compatibility patcher.

## Installation

Only OpenCore injection is recommended.

Load order:

1. `Lilu.kext`
2. `SimpleMtkBtFw.kext`
3. `SimpleMtkBtPatcher.kext`

Do not inject another Bluetooth firmware loader that binds to the same USB Bluetooth device.

> **macOS 15 (Sequoia) + MT7921/MT7922:** the steps above are not enough. Follow the
> [Sequoia installation guide](#mt7921--mt7922-on-macos-sequoia--installation-guide) below
> (Russian version: [docs/INSTALL_RU.md](docs/INSTALL_RU.md)).

## MT7921 / MT7922 on macOS Sequoia — installation guide

**Result on the test machine:** Foxconn MT7921 (`0489:E0CD`), macOS 15.8.1 (24H32), OpenCore, AMD platform →
Bluetooth powers on, `Chipset: SRS_7922`, scanning works. Technical background: [docs/MT7921-Sequoia.md](docs/MT7921-Sequoia.md).
Русская версия: [docs/INSTALL_RU.md](docs/INSTALL_RU.md).

macOS 15 `bluetoothd` has its own MediaTek Bluetooth code, but only for one card (`0489:E0D8`). This setup makes it
accept your card, lets `SimpleMtkBtFw` load the firmware, and tells `bluetoothd` that USB dongles may be used.
**All five pieces below are required.**

### 0. Check that your card is supported

```sh
ioreg -p IOUSB -l -w0 | grep -E '"(idVendor|idProduct|USB Product Name)"'
printf 'PID = 0x%04X\n' 57549      # put your decimal idProduct here
```

* The MediaTek Bluetooth function must be a **USB** device with **idVendor = 1161 (0x0489, Foxconn)**.
  Typical product name: `Wireless_Device`. Other vendors are *not* handled by this build (only the PID is rewritten).
* Remember your PID (`57549` = `0xE0CD` is the default of this build).
* The Bluetooth USB port must be enabled in your USB map (USBToolBox/UTBMap, port type *internal*). If the card is on an
  unmapped port macOS never sees it.
* Tested with `amfi_get_out_of_my_way=0x1` and relaxed SIP; whether it also works with default AMFI/SIP was not verified.

### 1. Remove conflicting Bluetooth kexts

Remove from `EFI/OC/Kexts` and from `config.plist → Kernel → Add`:

* the **stock** `BlueToolFixup.kext` (the build from this release replaces it, same bundle id),
* `BrcmPatchRAM*`, `BrcmFirmwareData`, `BrcmBluetoothInjector`, `IntelBluetoothFirmware`, `IntelBTPatcher`,
  `IntelBluetoothInjector`, and any other Bluetooth firmware loader,
* `SimpleMtkBtPatcher.kext` – **not used** in this setup (do not enable it).

Keep `Lilu.kext` (1.4.7 or newer; tested 1.7.2).

### 2. Download the release

Get `SimpleMtkBtFw-MT7921-Sequoia-v1.0.0.zip` from the
[Releases](../../releases) page, verify it (`shasum -a 256 -c SimpleMtkBtFw-MT7921-Sequoia-v1.0.0.zip.sha256`) and unzip.
It contains: `Kexts/` (2 kexts), `config/`, `tools/`, `firmware/`, `docs/`, `LICENSES/`.

### 3. Add the kexts

Copy both kexts from `Kexts/` into `EFI/OC/Kexts/` and add them to `config.plist → Kernel → Add` **in this order**:

| order | BundlePath | ExecutablePath | PlistPath |
|---|---|---|---|
| 1 | `Lilu.kext` | `Contents/MacOS/Lilu` | `Contents/Info.plist` |
| 2 | `SimpleMtkBtFw.kext` | `Contents/MacOS/SimpleMtkBtFw` | `Contents/Info.plist` |
| 3 | `BlueToolFixup.kext` | `Contents/MacOS/BlueToolFixup` | `Contents/Info.plist` |

`Arch = Any`, `Enabled = true`, `MinKernel`/`MaxKernel` empty (or limit to Sequoia: `MinKernel = 24.0.0`).
The kexts are unsigned: OpenCore injection only, do not install them into `/Library/Extensions`.

### 4. Add the NVRAM variables (important!)

Without these two variables `bluetoothd` believes the Mac has no internal controller and **never even looks for
USB dongles** (`External Bluetooth dongle was not found`). Merge [config/NVRAM-bluetooth.plist](config/NVRAM-bluetooth.plist)
into your `config.plist` (ProperTree / OpenCore Configurator):

* `NVRAM → Add → 7C436110-AB2A-4BBB-A880-FE41995C9F82`:
  `bluetoothInternalControllerInfo` = `<0000000000000000000000000000>` (Data, **14** zero bytes),
  `bluetoothExternalDongleFailed` = `<00>` (Data, **1** zero byte);
* `NVRAM → Delete → 7C436110-AB2A-4BBB-A880-FE41995C9F82`: add the same two key names, so they are rewritten
  with zeros on every boot (`bluetoothd` writes `bluetoothExternalDongleFailed = 1` after a failed attempt).

### 5. Install the firmware into `/etc/bluetool`

```sh
cd <unzipped release folder>
bash tools/install-firmware.sh               # asks for sudo; uses firmware/BT_RAM_CODE_MT7961_1_2_hdr.bin
ls -la /etc/bluetool                         # BT_RAM_CODE_MT7922_1_1_hdr.bin must be there
```

`bluetoothd` picks any file in `/etc/bluetool` whose name contains `BT_RAM_CODE_MT7922`. MT7921 and MT7922 use the same
ROM-patch format, and the script stores the MT7961 patch under that name (the real loading is done by `SimpleMtkBtFw`).
The folder does not exist on a fresh macOS; the script creates it. It persists across reboots (re-run the script if it is ever missing).

### 6. Other PID? Add a boot-arg

Only if your PID is not `0xE0CD`: add `smbtpid=<pid>` to the `boot-args` string (`NVRAM → Add → 7C436110-AB2A-4BBB-A880-FE41995C9F82`), decimal or `0x` hex, for example
`smbtpid=0xE0F2`. Vendor must be `0x0489`. Other options: `-smbtmtkoff` (disable the whitelist patch),
`-btlfxdbg` (stock BlueToolFixup debug).

### 7. Cold boot

**Shut down completely**, wait 10–15 seconds, then power on. A reboot is not enough: a chip that was touched by a
failed attempt keeps its state across warm reboots and the very first firmware command then times out. Do the same
after every change in this guide.

### 8. Verify

```sh
kextstat | grep -E 'SimpleMtkBtFw|BlueToolFixup'          # both loaded, no SimpleMtkBtPatcher
nvram -p | grep -i bluetooth                              # the two NVRAM variables
ls -la /etc/bluetool                                      # firmware present
system_profiler SPBluetoothDataType | head -12            # Address != NULL, State: On, Chipset: SRS_7922
```

Expected `bluetoothd` log:

```sh
log show --last boot --info --debug --predicate 'process == "bluetoothd"' | grep -E "dongle|SRS|Chip ID|firmware|patch_status|PowerOn|initialization"
```
```
Found external Bluetooth dongle -- setting chipset to THIRD_PARTY_DONGLE
HCI Transport is set to USB SRS
[OI_HciIfc_Init_USB_SRS] -- Read Chip ID 0x00007961
Found firmware: BT_RAM_CODE_MT7922_1_1_hdr.bin
mtkSendFwRomPatch: patch_status 2          (patch was already loaded by SimpleMtkBtFw: "no need to load rom patch")
MtkPowerOn: successful
Stack initialization complete
```

Then open *System Settings → Bluetooth*; devices around you should appear.

### If it does not work

| Symptom (in `system_profiler` / log) | Cause and fix |
|---|---|
| `Address: NULL`, `BCM_4350C2`, `External Bluetooth dongle was not found`, no `Found_USB_Dongle` lines | NVRAM variables missing or wrong length → step 4 |
| `HCI Transport is set to H4BC`, `/dev/cu.BLTH ... failed to appear` | the USB Bluetooth device was not named `Bluetooth USB Host Controller` → `SimpleMtkBtFw` not loaded/not matched (check its `idVendor/idProduct` in `Info.plist`, USB map) |
| `Found external Bluetooth dongle`, transport `USB` (not `USB SRS`), `ReadLocalVersionInformation failed STATUS 408` | whitelist patch not applied: wrong PID (step 6) or the stock `BlueToolFixup` is still loaded (step 1) |
| `patch_status 0`, then `endpoint 0x00 ... timeout: 64 bytes transferred`, `bluetoothd` restarts every ~10 s | firmware was not loaded by `SimpleMtkBtFw` first (load order / not matched) **or** the chip was not cold booted (step 7) |
| device missing from `ioreg -p IOUSB`, `createDevice ... 0xe00002bc` in the kernel log | chip wedged → full power off (unplug AC for a minute if needed); check the USB map |
| works, then disappears after a restart | do a full shutdown instead of reboot |
| `USB Product Name` set to "Bluetooth USB Host Controller" by some other tool | remove it: `bluetoothd` then treats the card as the internal controller and skips it |

### Updating / uninstalling

* After a macOS update: the patch matches byte patterns of `bluetoothd`; if Bluetooth breaks after an update, check the log
  above and open an issue with `sw_vers` and the log.
* Uninstall: remove `SimpleMtkBtFw.kext` and the MediaTek `BlueToolFixup.kext` (and their `config.plist` entries), delete
  the two NVRAM entries, `sudo rm -r /etc/bluetool`.

### Building from source

`BlueToolFixup-MediaTek/build.sh` (needs Xcode, a MacKernelSDK and the Lilu 1.7.x DEBUG kext for its SDK headers) rebuilds the
patched `BlueToolFixup.kext` from the upstream BrcmPatchRAM commit plus `mediatek.patch`. `SimpleMtkBtFw.kext` is
built as described in [Build](#build).

## Troubleshooting

- Check kernel logs for `SimpleMtkBtFw` and `SimpleMtkBtPatcher`.
- Confirm that the USB device is matched by `SimpleMtkBtFw/Info.plist`.
- Confirm that the internal device ID in logs is `0x7961` or `0x7922`.
- Check IORegistry for `FirmwareLoaded` and `fw_name`.

## Credits

- `laobamac` - SimpleMtkBtFw author, responsible for the MediaTek Bluetooth port, integration, and testing.
- `zxystd` and IntelBluetoothFirmware contributors - original framework, retained upstream code, and firmware generation helpers.
- MediaTek Inc. - MediaTek Bluetooth firmware format and Linux driver reference material.
- Linux kernel Bluetooth contributors - `btmtk` / `btusb` logic and device ID references.
- `vit9696`, acidanthera, and Lilu contributors - Lilu patching framework.
- `dawalishi821` provides network card hardware

## Copyright And Licensing

See individual source file headers and included license files. Files adapted from or retained from upstream keep their original copyright and SPDX/license headers; newly written SimpleMtkBtFw files use `laobamac` copyright headers.

Before publishing binary packages that include firmware blobs, confirm the firmware redistribution terms.

## Disclaimer

This is an experimental driver for unsupported hardware. Use it at your own risk.
