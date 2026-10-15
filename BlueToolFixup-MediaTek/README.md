# BlueToolFixup (MediaTek whitelist build)

A small modification of Acidanthera's [BlueToolFixup](https://github.com/acidanthera/BrcmPatchRAM)
(from BrcmPatchRAM, commit `6538902`, version 2.7.3) that lets the **native MediaTek Bluetooth
path of macOS `bluetoothd`** accept a card other than the single one Apple whitelisted.

* `BlueToolFixup.cpp` – the modified source (GPLv2, see `LICENSE`, original copyright headers kept)
* `mediatek.patch` – the same change as a diff against upstream `BlueToolFixup.cpp`
* `build.sh` – reproducible build recipe (clone upstream, apply the patch, build)

## What it changes

`bluetoothd` on macOS 15 (checked on 15.8.1 / 24H32, x86_64) contains a port of Linux `btmtk`
(`MtkLoadRomPatch`, `MtkPowerOn`, "USB SRS" transport, chipset type 0xB). It is whitelisted for exactly
one USB device, **Foxconn 0489:E0D8**, in two places:

| function | instruction | bytes |
|---|---|---|
| `Found_USB_Dongle` | `xorl $0xE0D8, %esi` | `81 F6 D8 E0 00 00` |
| `OI_HciIfc_Init_USB_SRS` | `cmpw $0xE0D8, 0x16(%rdi)` | `66 81 7F 16 D8 E0` |

Both byte sequences are unique in the binary. This build rewrites the PID immediate in the
`cs_validate_page` hook (the same mechanism stock BlueToolFixup uses), so the card is treated like
the whitelisted one. Defaults to **PID 0xE0CD** (Foxconn MT7921).

| boot-arg | effect |
|---|---|
| `smbtpid=<n>` | use another PID (decimal or `0x..`), vendor stays 0x0489 |
| `-smbtmtkoff` | disable the whitelist patch |
| `-smbtchunkoff`, `smbtchunk=<n>` | an additional, optional patch of the ROM-patch chunk size (not needed when the firmware is loaded by `SimpleMtkBtFw`) |

All stock BlueToolFixup boot-args (`-btlfxoff`, `-btlfxdbg`, ...) keep working.

The patches match **byte patterns**, so they may apply to other 15.x builds, but the immediates and
the surrounding code were only verified on 24H32. If a macOS update changes the code the patch simply
does not match and Bluetooth falls back to the stock behaviour.

## Build

```sh
./build.sh      # needs Xcode, and a MacKernelSDK + Lilu.kext (DEBUG SDK) next to the cloned repo
```

Do **not** load this together with the stock BlueToolFixup (same bundle identifier).
