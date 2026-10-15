# MT7921 / MT7922 Bluetooth на macOS Sequoia — пошаговая установка

**Результат на тестовой машине:** Foxconn MT7921 (`0489:E0CD`), macOS 15.8.1 (24H32), OpenCore, AMD →
Bluetooth включается, `Chipset: SRS_7922`, поиск устройств работает.
Технические причины: [MT7921-Sequoia.md](MT7921-Sequoia.md). Английская версия: [README](../README.md#mt7921--mt7922-on-macos-sequoia--installation-guide).

В `bluetoothd` macOS 15 есть собственный MediaTek-драйвер, но он включён только для одной карты (`0489:E0D8`).
Эта схема заставляет его принять вашу карту, даёт `SimpleMtkBtFw` загрузить прошивку и «разрешает» USB-донглы.
**Нужны все пять частей ниже, пропускать нельзя.**

## 0. Подходит ли ваша карта

```sh
ioreg -p IOUSB -l -w0 | grep -E '"(idVendor|idProduct|USB Product Name)"'
printf 'PID = 0x%04X\n' 57549      # подставьте свой десятичный idProduct
```

* Bluetooth-часть карты должна быть **USB-устройством** с **idVendor = 1161 (0x0489, Foxconn)**. Обычное имя: `Wireless_Device`.
  Другие вендоры в этой сборке **не** поддерживаются (патчится только PID).
* Запомните свой PID (`57549` = `0xE0CD` — значение по умолчанию).
* USB-порт Bluetooth должен быть включён в вашей USB-карте (USBToolBox/UTBMap, тип порта *internal*).
  Если карта на «немаппнутом» порту, macOS её не увидит.
* Проверено с `amfi_get_out_of_my_way=0x1` и ослабленным SIP; без этого не проверялось.

## 1. Уберите конфликтующие кексты

Удалите из `EFI/OC/Kexts` и из `config.plist → Kernel → Add`:

* **штатный** `BlueToolFixup.kext` (версия из релиза заменяет его, bundle id тот же);
* `BrcmPatchRAM*`, `BrcmFirmwareData`, `BrcmBluetoothInjector`, `IntelBluetoothFirmware`, `IntelBTPatcher`,
  `IntelBluetoothInjector` и любые другие загрузчики прошивки Bluetooth;
* `SimpleMtkBtPatcher.kext` — в этой схеме **не нужен** (не включайте его).

`Lilu.kext` оставьте (1.4.7 или новее; проверено на 1.7.2).

## 2. Скачайте релиз

Возьмите `SimpleMtkBtFw-MT7921-Sequoia-v1.0.0.zip` на странице [Releases](../../../releases), проверьте
(`shasum -a 256 -c SimpleMtkBtFw-MT7921-Sequoia-v1.0.0.zip.sha256`) и распакуйте. Внутри: `Kexts/` (2 кекста),
`config/`, `tools/`, `firmware/`, `docs/`, `LICENSES/`.

## 3. Добавьте кексты

Скопируйте оба кекста из `Kexts/` в `EFI/OC/Kexts/` и добавьте в `config.plist → Kernel → Add` **в таком порядке**:

| № | BundlePath | ExecutablePath | PlistPath |
|---|---|---|---|
| 1 | `Lilu.kext` | `Contents/MacOS/Lilu` | `Contents/Info.plist` |
| 2 | `SimpleMtkBtFw.kext` | `Contents/MacOS/SimpleMtkBtFw` | `Contents/Info.plist` |
| 3 | `BlueToolFixup.kext` | `Contents/MacOS/BlueToolFixup` | `Contents/Info.plist` |

`Arch = Any`, `Enabled = true`, `MinKernel`/`MaxKernel` пустые (или `MinKernel = 24.0.0`, только Sequoia).
Кексты не подписаны: только внедрение через OpenCore, в `/Library/Extensions` не ставить.

## 4. NVRAM-переменные (важно!)

Без этих двух переменных `bluetoothd` считает, что у Mac нет внутреннего контроллера, и **вообще не ищет USB-донглы**
(`External Bluetooth dongle was not found`). Добавьте содержимое [config/NVRAM-bluetooth.plist](../config/NVRAM-bluetooth.plist)
в ваш `config.plist` (ProperTree / OpenCore Configurator):

* `NVRAM → Add → 7C436110-AB2A-4BBB-A880-FE41995C9F82`:
  `bluetoothInternalControllerInfo` = `<0000000000000000000000000000>` (Data, **14** нулевых байт),
  `bluetoothExternalDongleFailed` = `<00>` (Data, **1** нулевой байт);
* `NVRAM → Delete → 7C436110-AB2A-4BBB-A880-FE41995C9F82`: добавьте те же два имени, чтобы они перезаписывались нулями
  при каждой загрузке (после неудачной попытки `bluetoothd` сам пишет `bluetoothExternalDongleFailed = 1`).

## 5. Прошивка в `/etc/bluetool`

```sh
cd <папка распакованного релиза>
bash tools/install-firmware.sh               # попросит sudo; берёт firmware/BT_RAM_CODE_MT7961_1_2_hdr.bin
ls -la /etc/bluetool                         # там должен быть BT_RAM_CODE_MT7922_1_1_hdr.bin
```

`bluetoothd` берёт любой файл в `/etc/bluetool`, в имени которого есть `BT_RAM_CODE_MT7922`. У MT7921 и MT7922 общий формат
ROM-патча, скрипт кладёт патч MT7961 под этим именем (реально прошивку грузит `SimpleMtkBtFw`). На чистой macOS папки нет —
скрипт создаёт её; она переживает перезагрузки (если вдруг пропала, запустите скрипт снова).

## 6. Другой PID — boot-arg

Только если PID не `0xE0CD`: добавьте `smbtpid=<pid>` в строку `boot-args`
(`NVRAM → Add → 7C436110-AB2A-4BBB-A880-FE41995C9F82`), десятичный или `0x`-hex, например `smbtpid=0xE0F2`.
Вендор должен быть `0x0489`. Прочее: `-smbtmtkoff` (отключить патч белого списка), `-btlfxdbg` (отладка штатного BlueToolFixup).

## 7. Холодная загрузка

**Полностью выключите** компьютер, подождите 10–15 секунд и включите. Перезагрузки недостаточно: чип, к которому
обращались неудачной попыткой, сохраняет состояние при тёплой перезагрузке, и первая же команда прошивки уходит в таймаут.
То же самое делайте после каждого изменения из этого руководства.

## 8. Проверка

```sh
kextstat | grep -E 'SimpleMtkBtFw|BlueToolFixup'          # оба загружены, SimpleMtkBtPatcher нет
nvram -p | grep -i bluetooth                              # две NVRAM-переменные
ls -la /etc/bluetool                                      # прошивка на месте
system_profiler SPBluetoothDataType | head -12            # Address != NULL, State: On, Chipset: SRS_7922
```

Ожидаемый лог `bluetoothd`:

```sh
log show --last boot --info --debug --predicate 'process == "bluetoothd"' | grep -E "dongle|SRS|Chip ID|firmware|patch_status|PowerOn|initialization"
```
```
Found external Bluetooth dongle -- setting chipset to THIRD_PARTY_DONGLE
HCI Transport is set to USB SRS
[OI_HciIfc_Init_USB_SRS] -- Read Chip ID 0x00007961
Found firmware: BT_RAM_CODE_MT7922_1_1_hdr.bin
mtkSendFwRomPatch: patch_status 2          (патч уже загружен SimpleMtkBtFw: «no need to load rom patch»)
MtkPowerOn: successful
Stack initialization complete
```

Затем откройте *Системные настройки → Bluetooth*: устройства рядом должны появиться.

## Если не работает

| Симптом (`system_profiler` / лог) | Причина и решение |
|---|---|
| `Address: NULL`, `BCM_4350C2`, `External Bluetooth dongle was not found`, нет строк `Found_USB_Dongle` | нет NVRAM-переменных или неверная длина → шаг 4 |
| `HCI Transport is set to H4BC`, `/dev/cu.BLTH ... failed to appear` | USB-устройство не получило имя `Bluetooth USB Host Controller` → `SimpleMtkBtFw` не загружен/не сматчился (проверьте `idVendor/idProduct` в его `Info.plist`, USB-карту) |
| `Found external Bluetooth dongle`, транспорт `USB` (не `USB SRS`), `ReadLocalVersionInformation failed STATUS 408` | патч белого списка не применился: неверный PID (шаг 6) или всё ещё загружен штатный `BlueToolFixup` (шаг 1) |
| `patch_status 0`, затем `endpoint 0x00 ... timeout: 64 bytes transferred`, `bluetoothd` перезапускается каждые ~10 с | прошивку не загрузил `SimpleMtkBtFw` (порядок/не сматчился) **или** не было холодной загрузки (шаг 7) |
| устройства нет в `ioreg -p IOUSB`, в логе ядра `createDevice ... 0xe00002bc` | чип завис → полное выключение (при необходимости отключить питание на минуту); проверьте USB-карту |
| заработало, но пропало после перезапуска | делайте полное выключение вместо перезагрузки |
| кто-то выставляет `USB Product Name` = "Bluetooth USB Host Controller" | уберите: `bluetoothd` тогда считает карту внутренним контроллером и пропускает её |

## Обновление macOS / удаление

* После обновления macOS: патч ищет байтовые шаблоны в `bluetoothd`; если Bluetooth сломался, проверьте лог выше и
  заведите issue с выводом `sw_vers` и логом.
* Удаление: уберите `SimpleMtkBtFw.kext` и MediaTek-`BlueToolFixup.kext` (и их записи в `config.plist`), удалите две
  NVRAM-записи, `sudo rm -r /etc/bluetool`.
