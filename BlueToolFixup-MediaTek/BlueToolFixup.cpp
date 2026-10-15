//
//  BlueToolFixup.cpp
//  BrcmPatchRAM
//
//  Created by Dhinak G on 6/11/21.
//


#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_user.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_version.hpp>
#include <Headers/kern_devinfo.hpp>


#define MODULE_SHORT "btlfx"


class EXPORT BlueToolFixup : public IOService {
    OSDeclareDefaultStructors(BlueToolFixup)
public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(BlueToolFixup, IOService)


IOService *BlueToolFixup::probe(IOService *provider, SInt32 *score) {
    return ADDPR(startSuccess) ? IOService::probe(provider, score) : nullptr;
}

bool BlueToolFixup::start(IOService *provider) {
    if (!IOService::start(provider)) {
        SYSLOG("init", "failed to start the parent");
        return false;
    }
    setProperty("VersionInfo", kextVersion);
    registerService();
    
    return true;
}


#pragma mark - Patches

static const uint8_t kSkipUpdateFilePathOriginal[] = "/etc/bluetool/SkipBluetoothAutomaticFirmwareUpdate";
static const uint8_t kSkipUpdateFilePathPatched[]  = "/System/Library/CoreServices/boot.efi";


// Workaround 12.4 Beta 3+ bug where macOS may detect the Bluetooth chipset twice
// Once as internal, and second as an external dongle:
// 'ERROR -- Third Party Dongle has the same address as the internal module'
// Mainly applicable for BCM2046 and BCM2070 chipsets (BT2.1)
static const uint8_t kSkipAddressCheckOriginal[] =
{
    0x48, 0x89, 0xF3,             // mov    rbx, rsi
    0xE8, 0x00, 0x00, 0x00, 0x00, // call   <somewhere>
    0x85, 0xC0,                   // test   eax, eax
    0x74, 0x1D,                   // je
};

static const uint8_t kSkipAddressCheckPatched[] =
{
    0x48, 0x89, 0xF3,             // mov        rbx, rsi
    0xE8, 0x00, 0x00, 0x00, 0x00, // call       <somewhere>
    0x85, 0xC0,                   // test       eax, eax
    0x72, 0x1D,                   // jb short
};

static const uint8_t kSkipAddressCheckMask[] =
{
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
};

static const uint8_t kVendorCheckOriginal[] =
{
    0x81, 0xFA,              // cmp edx
    0x5C, 0x0A, 0x00, 0x00,  // Vendor BRCM,
    0x74                     // jnz short
};

static const uint8_t kVendorCheckPatched[] =
{
    0x81, 0xFA,              // cmp edx
    0x5C, 0x0A, 0x00, 0x00,  // Vendor BRCM,
    0xEB                     // jmp short
};

// Workaround for bugged chipset range check that
// doesn't consider 0 "THIRD_PARTY_DONGLE" as valid.
// This patch allows bluetooth to turn back on after the first power cycle.
// See https://github.com/acidanthera/BrcmPatchRAM/pull/18 for more details.
static const uint8_t kBadChipsetCheckOriginal[] =
{
    0x81, 0xF9,              // cmp ecx
    0xCF, 0x07, 0x00, 0x00,  // int 1999
    0x72                     // jb short
};

static const uint8_t kBadChipsetCheckPatched[] =
{
    0x81, 0xF9,              // cmp ecx
    0xCF, 0x07, 0x00, 0x00,  // int 1999
    0xEB                     // jmp short
};

static const uint8_t kBadChipsetCheckOriginal13_3[] =
{
    0x81, 0xF9,              // cmp ecx
    0x9E, 0x0F, 0x00, 0x00,  // int 3998
    0x77, 0x1A               // ja short
};

static const uint8_t kBadChipsetCheckPatched13_3[] =
{
    0x90, 0x90,
    0x90, 0x90, 0x90, 0x90,
    0x90, 0x90
};

static const uint8_t kSkipInternalControllerNVRAMCheck13_3[] =
{
    0x41, 0x80, 0x00, 0x01, // xor     r15b, 1
    0x75, 0x00,             // jnz     short
    0x84, 0xDB,             // test    bl, bl
    0x75, 0x00              // jnz     short
};

static const uint8_t kSkipInternalControllerNVRAMCheckMask13_3[] =
{
    0xFF, 0xFF, 0x00, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF,
    0xFF, 0x00
};

static const uint8_t kSkipInternalControllerNVRAMCheckPatched13_3[] =
{
    0x90, 0x90, 0x90, 0x90,
    0x90, 0x90,
    0x90, 0x90,
    0x90, 0x90
};

static const uint8_t kBadChipsetCheckOriginal15_4[] =
{
    0x81, 0xF9,              // cmp ecx
    0x6F, 0x17, 0x00, 0x00,  // int 5999
    0x77, 0x30               // ja short
};

static const uint8_t kBadChipsetCheckPatched15_4[] =
{
    0x90, 0x90,
    0x90, 0x90, 0x90, 0x90,
    0x90, 0x90
};

// --- MediaTek native path of bluetoothd (macOS 15.x, 24H32 x86_64) -----------------------------------------
// bluetoothd ships a Linux-btmtk port (firmware from /etc/bluetool, "USB SRS" transport, chipset type 0xB) that is
// whitelisted for exactly one USB device, Foxconn 0489:E0D8, in two places:
//   Found_USB_Dongle:       xorl  $0xE0D8, %esi           81 F6 D8 E0 00 00
//   OI_HciIfc_Init_USB_SRS: cmpw  $0xE0D8, 0x16(%rdi)     66 81 7F 16 D8 E0
// Both sequences are unique in the binary. The PID immediate is rewritten to the PID of the card in this machine
// (default 0xE0CD, Foxconn MT7921; override with the smbtpid=<n> boot-arg). Disable with -smbtmtkoff.
static const uint8_t kMtkXorOriginal[]  = { 0x81, 0xF6, 0xD8, 0xE0, 0x00, 0x00 };
static uint8_t       kMtkXorPatched[]   = { 0x81, 0xF6, 0xCD, 0xE0, 0x00, 0x00 };
static const uint8_t kMtkCmpwOriginal[] = { 0x66, 0x81, 0x7F, 0x16, 0xD8, 0xE0 };
static uint8_t       kMtkCmpwPatched[]  = { 0x66, 0x81, 0x7F, 0x16, 0xCD, 0xE0 };
static bool shouldPatchMtk = true;

// bluetoothd's mtkFWPatchUsingUsingCmd() (VA 0x1000ce5ab on 24H32) sends ROM-patch data in WMT chunks of
// 0x7BA (1978) bytes. The MT7921 ROM accepts the first command (section map, 61 bytes) but stalls the control
// pipe after one 64-byte packet of the first big chunk (kernel: "endpoint 0x00 status timeout: 64 bytes").
// Linux btmtk and the old SimpleMtkBtFw use 250. The three immediates are rewritten to the chunk size
// (default 250, override: smbtchunk=<n>, n <= 1978; disable with -smbtchunkoff).
static const uint8_t kMtkChunk1Original[] = { 0xBF, 0xBA, 0x07, 0x00, 0x00, 0x41, 0x39, 0xFC };
static uint8_t       kMtkChunk1Patched[]  = { 0xBF, 0xFA, 0x00, 0x00, 0x00, 0x41, 0x39, 0xFC };
static const uint8_t kMtkChunk2Original[] = { 0x81, 0xF9, 0xBA, 0x07, 0x00, 0x00, 0x0F, 0x92, 0xC0 };
static uint8_t       kMtkChunk2Patched[]  = { 0x81, 0xF9, 0xFA, 0x00, 0x00, 0x00, 0x0F, 0x92, 0xC0 };
static const uint8_t kMtkChunk3Original[] = { 0x81, 0xF9, 0xBA, 0x07, 0x00, 0x00, 0x72, 0x05 };
static uint8_t       kMtkChunk3Patched[]  = { 0x81, 0xF9, 0xFA, 0x00, 0x00, 0x00, 0x72, 0x05 };
static bool shouldPatchMtkChunk = true;

static bool shouldPatchBoardId = false;
static bool shouldPatchAddress = false;
static bool shouldPatchNvramCheck = false;

static constexpr size_t kBoardIdSize = sizeof("Mac-F60DEB81FF30ACF6");
static constexpr size_t kBoardIdSizeLegacy = sizeof("Mac-F22586C8");

static const char boardIdsWithUSBBluetooth[][kBoardIdSize] = {
    "Mac-F60DEB81FF30ACF6",
    "Mac-9F18E312C5C2BF0B",
    "Mac-937CB26E2E02BB01",
    "Mac-E43C1C25D4880AD6",
    "Mac-06F11FD93F0323C5",
    "Mac-06F11F11946D27C5",
    "Mac-A369DDC4E67F1C45",
    "Mac-FFE5EF870D7BA81A",
    "Mac-DB15BD556843C820",
    "Mac-B809C3757DA9BB8D",
    "Mac-65CE76090165799A",
    "Mac-4B682C642B45593E",
    "Mac-77F17D7DA9285301",
    "Mac-BE088AF8C5EB4FA2"
};

static mach_vm_address_t orig_cs_validate {};

#pragma mark - Kernel patching code

static inline void searchAndPatch(const void *haystack, size_t haystackSize, const char *path, const void *needle, size_t findSize, const void *patch, size_t replaceSize) {
    if (KernelPatcher::findAndReplace(const_cast<void *>(haystack), haystackSize, needle, findSize, patch, replaceSize))
        DBGLOG(MODULE_SHORT, "found string to patch at %s!", path);
}

template <size_t findSize, size_t replaceSize, typename T>
static inline void searchAndPatch(const void *haystack, size_t haystackSize, const char *path, const T (&needle)[findSize], const T (&patch)[replaceSize]) {
    searchAndPatch(haystack, haystackSize, path, needle, findSize * sizeof(T), patch, replaceSize * sizeof(T));
}

static inline void searchAndPatchWithMask(const void *haystack, size_t haystackSize, const char *path, const void *needle, size_t findSize, const void *findMask, size_t findMaskSize, const void *patch, size_t replaceSize, const void *patchMask, size_t replaceMaskSize) {
    if (KernelPatcher::findAndReplaceWithMask(const_cast<void *>(haystack), haystackSize, needle, findSize, findMask, findMaskSize, patch, replaceSize, patchMask, replaceMaskSize))
        DBGLOG(MODULE_SHORT, "found string to patch at %s!", path);
}

template <size_t findSize, size_t findMaskSize, size_t replaceSize, size_t replaceMaskSize, typename T>
static inline void searchAndPatchWithMask(const void *haystack, size_t haystackSize, const char *path, const T (&needle)[findSize], const T (&findMask)[findMaskSize], const T (&patch)[replaceSize], const T (&patchMask)[replaceMaskSize]) {
    searchAndPatchWithMask(haystack, haystackSize, path, needle, findSize * sizeof(T), findMask, findMaskSize * sizeof(T), patch, replaceSize * sizeof(T), patchMask, replaceSize * sizeof(T));
}


#pragma mark - Patched functions

static void patched_cs_validate_page(vnode_t vp, memory_object_t pager, memory_object_offset_t page_offset, const void *data, int *validated_p, int *tainted_p, int *nx_p) {
    char path[PATH_MAX];
    int pathlen = PATH_MAX;
    FunctionCast(patched_cs_validate_page, orig_cs_validate)(vp, pager, page_offset, data, validated_p, tainted_p, nx_p);
    static constexpr size_t dirLength = sizeof("/usr/sbin/")-1;
    if (vn_getpath(vp, path, &pathlen) == 0 && UNLIKELY(strncmp(path, "/usr/sbin/", dirLength) == 0)) {
        if (strcmp(path + dirLength, "BlueTool") == 0) {
            searchAndPatch(data, PAGE_SIZE, path, kSkipUpdateFilePathOriginal, kSkipUpdateFilePathPatched);
            if (shouldPatchBoardId)
                searchAndPatch(data, PAGE_SIZE, path, boardIdsWithUSBBluetooth[0], kBoardIdSize, BaseDeviceInfo::get().boardIdentifier, kBoardIdSize);
        }
        else if (strcmp(path + dirLength, "bluetoothd") == 0) {
            searchAndPatch(data, PAGE_SIZE, path, kVendorCheckOriginal, kVendorCheckPatched);
            searchAndPatch(data, PAGE_SIZE, path, kBadChipsetCheckOriginal, kBadChipsetCheckPatched);
            if (getKernelVersion() >= KernelVersion::Ventura && getKernelVersion() < KernelVersion::Sequoia)
                searchAndPatch(data, PAGE_SIZE, path, kBadChipsetCheckOriginal13_3, kBadChipsetCheckPatched13_3);
            if (getKernelVersion() >= KernelVersion::Sequoia)
                searchAndPatch(data, PAGE_SIZE, path, kBadChipsetCheckOriginal15_4, kBadChipsetCheckPatched15_4);
            if (shouldPatchNvramCheck)
                searchAndPatchWithMask(data, PAGE_SIZE, path, kSkipInternalControllerNVRAMCheck13_3, sizeof(kSkipInternalControllerNVRAMCheck13_3), kSkipInternalControllerNVRAMCheckMask13_3, sizeof(kSkipInternalControllerNVRAMCheckMask13_3), kSkipInternalControllerNVRAMCheckPatched13_3, sizeof(kSkipInternalControllerNVRAMCheckPatched13_3), nullptr, 0);
            if (shouldPatchBoardId)
                searchAndPatch(data, PAGE_SIZE, path, boardIdsWithUSBBluetooth[0], kBoardIdSize, BaseDeviceInfo::get().boardIdentifier, kBoardIdSize);
            if (shouldPatchAddress)
                searchAndPatchWithMask(data, PAGE_SIZE, path, kSkipAddressCheckOriginal, kSkipAddressCheckMask, kSkipAddressCheckPatched, kSkipAddressCheckMask);
            if (shouldPatchMtk) {
                searchAndPatch(data, PAGE_SIZE, path, kMtkXorOriginal, sizeof(kMtkXorOriginal), kMtkXorPatched, sizeof(kMtkXorPatched));
                searchAndPatch(data, PAGE_SIZE, path, kMtkCmpwOriginal, sizeof(kMtkCmpwOriginal), kMtkCmpwPatched, sizeof(kMtkCmpwPatched));
            }
            if (shouldPatchMtkChunk) {
                searchAndPatch(data, PAGE_SIZE, path, kMtkChunk1Original, sizeof(kMtkChunk1Original), kMtkChunk1Patched, sizeof(kMtkChunk1Patched));
                searchAndPatch(data, PAGE_SIZE, path, kMtkChunk2Original, sizeof(kMtkChunk2Original), kMtkChunk2Patched, sizeof(kMtkChunk2Patched));
                searchAndPatch(data, PAGE_SIZE, path, kMtkChunk3Original, sizeof(kMtkChunk3Original), kMtkChunk3Patched, sizeof(kMtkChunk3Patched));
            }
        }
    }
}


#pragma mark - Patches on start/stop

static void pluginStart() {
    SYSLOG(MODULE_SHORT, "start");
    // There is no point in routing cs_validate_range, because this kext should only be running on Monterey+
    if (getKernelVersion() < KernelVersion::Monterey)
        return;
    lilu.onPatcherLoadForce([](void *user, KernelPatcher &patcher) {
        if (getKernelVersion() >= KernelVersion::Sonoma) {
            shouldPatchBoardId = checkKernelArgument("-btlfxboardid");
        } else {
            auto boardId = BaseDeviceInfo::get().boardIdentifier;
            auto boardIdSize = strlen(boardId) + 1;
            shouldPatchBoardId = boardIdSize == kBoardIdSize || boardIdSize == kBoardIdSizeLegacy;
            if (shouldPatchBoardId)
                for (size_t i = 0; i < arrsize(boardIdsWithUSBBluetooth); i++)
                    if (strcmp(boardIdsWithUSBBluetooth[i], boardId) == 0) {
                        shouldPatchBoardId = false;
                        break;
                    }
        }
        if ((getKernelVersion() == KernelVersion::Monterey && getKernelMinorVersion() >= 5) || getKernelVersion() > KernelVersion::Monterey)
            // 12.4 Beta 3+, XNU 21.5
            shouldPatchAddress = checkKernelArgument("-btlfxallowanyaddr");
        if (getKernelVersion() <= KernelVersion::Sonoma)
            shouldPatchNvramCheck = checkKernelArgument("-btlfxnvramcheck");
        shouldPatchMtk = !checkKernelArgument("-smbtmtkoff");
        shouldPatchMtkChunk = !checkKernelArgument("-smbtchunkoff");
        uint32_t mtkChunk = 250;
        if (lilu_get_boot_args("smbtchunk", &mtkChunk, sizeof(mtkChunk)) && (mtkChunk == 0 || mtkChunk > 0x7BA))
            mtkChunk = 250;
        kMtkChunk1Patched[1] = static_cast<uint8_t>(mtkChunk & 0xFF); kMtkChunk1Patched[2] = static_cast<uint8_t>((mtkChunk >> 8) & 0xFF);
        kMtkChunk2Patched[2] = static_cast<uint8_t>(mtkChunk & 0xFF); kMtkChunk2Patched[3] = static_cast<uint8_t>((mtkChunk >> 8) & 0xFF);
        kMtkChunk3Patched[2] = static_cast<uint8_t>(mtkChunk & 0xFF); kMtkChunk3Patched[3] = static_cast<uint8_t>((mtkChunk >> 8) & 0xFF);
        uint32_t mtkPid = 0;
        if (lilu_get_boot_args("smbtpid", &mtkPid, sizeof(mtkPid)) && mtkPid > 0 && mtkPid <= 0xFFFF) {
            kMtkXorPatched[2]  = kMtkCmpwPatched[4] = static_cast<uint8_t>(mtkPid & 0xFF);
            kMtkXorPatched[3]  = kMtkCmpwPatched[5] = static_cast<uint8_t>((mtkPid >> 8) & 0xFF);
        }
        KernelPatcher::RouteRequest csRoute = KernelPatcher::RouteRequest("_cs_validate_page", patched_cs_validate_page, orig_cs_validate);
        if (!patcher.routeMultipleLong(KernelPatcher::KernelID, &csRoute, 1))
            SYSLOG(MODULE_SHORT, "failed to route cs validation pages");
    });
}

// Boot args.
static const char *bootargOff[] {
    "-btlfxoff"
};
static const char *bootargDebug[] {
    "-btlfxdbg"
};
static const char *bootargBeta[] {
    "-btlfxbeta"
};

// Plugin configuration.
PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),
    bootargBeta,
    arrsize(bootargBeta),
    KernelVersion::Monterey,
    KernelVersion::Tahoe,
    pluginStart
};
