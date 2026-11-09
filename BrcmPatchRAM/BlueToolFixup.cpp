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

// Optional universal workaround for bluetoothd Read Clock failures.
// Many third-party Bluetooth controllers (like Intel) fail Apple's HCI_Read_Clock check.
// When connecting to modern Apple headphones with W1/H1/H2 chips (e.g., AirPods Pro 2),
// this causes bluetoothd to abort A2DP audio bring-up in the Fast-Connect reconnect path.
// This results in the infamous "connected but no sound" issue for these devices.
// See: https://github.com/OpenIntelWireless/IntelBluetoothFirmware/issues/462
// We patch the Fast-Connect A2DP gate to unconditionally force the success path.
// Activated by boot-arg -btlfxa2dpcheck

// Apple FastConnect A2DP reconnect gate.
// Replaces "test r14d, r14d; je" with "xor r14d, r14d; jmp" to
// unconditionally take the audio-publish path.

// macOS 14.x/15.x/16.x (Sonoma/Sequoia/Tahoe)
static const uint8_t kFastConnectA2DPGateOriginal14_0[] =
{
    0x45, 0x85, 0xF6,
    0x74, 0x25,
    0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x18
};

static const uint8_t kFastConnectA2DPGatePatched14_0[] =
{
    0x45, 0x31, 0xF6,
    0xEB, 0x25,
    0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x18
};

static const uint8_t kFastConnectA2DPGateMask14_0[] =
{
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};

// macOS 12.x/13.x (Monterey/Ventura)
static const uint8_t kFastConnectA2DPGateOriginal12_0[] =
{
    0x85, 0xDB,
    0x74, 0x25,
    0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x18
};

static const uint8_t kFastConnectA2DPGatePatched12_0[] =
{
    0x31, 0xDB,
    0xEB, 0x25,
    0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x18
};

static const uint8_t kFastConnectA2DPGateMask12_0[] =
{
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};

// Optional: let bluetoothd use Bluetooth-spec (5.0) LE advertising sets on a THIRD_PARTY_DONGLE.
// bluetoothd decides "BT chip support for BT Spec advertisingsets" with a capability method
// (vtable +0x9c0 on 26.4.1) that only looks at the chipset enum, and chipset 0
// (THIRD_PARTY_DONGLE) is never in its list. With the flag off, LE_InitMultipleAdvertising
// falls back to the vendor (VSC) path, which also needs a non-zero chipset, so it returns
// 0x98 and LE advertising never starts ("Stack is not started"). That breaks
// AirDrop/Continuity discovery from iPhone.
// The patch replaces the capability call with "mov al, 1" at the only call site, so the
// rest (DisableSpecSets override, multi-advertising init) runs as Apple wrote it and sends
// standard HCI LE extended advertising commands, which BT 5.x controllers implement.
// Activated by boot-arg -btlfxadvsets

// macOS 26.4.1 (25E253) x86_64 bluetoothd, 0x10040e2ca
// mov rcx, [rax]; mov rdi, rax; call [rcx+0x9c0]; mov r14d, eax
static const uint8_t kSpecAdvSetsCapOriginal26[] =
{
    0x48, 0x8B, 0x08,
    0x48, 0x89, 0xC7,
    0xFF, 0x91, 0xC0, 0x09, 0x00, 0x00,
    0x41, 0x89, 0xC6
};

// mov rcx, [rax]; mov rdi, rax; mov al, 1; nop x4; mov r14d, eax
static const uint8_t kSpecAdvSetsCapPatched26[] =
{
    0x48, 0x8B, 0x08,
    0x48, 0x89, 0xC7,
    0xB0, 0x01, 0x90, 0x90, 0x90, 0x90,
    0x41, 0x89, 0xC6
};

// The extended scanning capability (vtable +0x9a8, 0x10040aae2 on 26.4.1). Apple enables it
// together with BT Spec advertising sets for every chipset it knows. Once the host has used
// LE extended advertising commands, the controller rejects legacy LE scan commands with
// Command Disallowed, so both must be on.
// mov rcx, [rax]; mov rdi, rax; call [rcx+0x9a8]; mov r14d, eax
static const uint8_t kExtScanCapOriginal26[] =
{
    0x48, 0x8B, 0x08,
    0x48, 0x89, 0xC7,
    0xFF, 0x91, 0xA8, 0x09, 0x00, 0x00,
    0x41, 0x89, 0xC6
};

// LE event mask (0x10017015c on 26.4.1): bluetoothd writes 0x1f (legacy LE events only) when
// the chipset is 0, so Extended Advertising Report, Advertising Set Terminated, etc. are
// masked and an extended scan never reports anything. Drop the cmove so the full mask
// (same as every Apple chipset) is used.
// test eax, eax; lea rax, [mask1f]; lea rdx, [maskFull]; cmove rdx, rax
static const uint8_t kLeEventMaskOriginal26[] =
{
    0x85, 0xC0,
    0x48, 0x8D, 0x05, 0xA0, 0xF2, 0x8F, 0x00,
    0x48, 0x8D, 0x15, 0xA1, 0xF2, 0x8F, 0x00,
    0x48, 0x0F, 0x44, 0xD0
};

static const uint8_t kLeEventMaskPatched26[] =
{
    0x85, 0xC0,
    0x48, 0x8D, 0x05, 0xA0, 0xF2, 0x8F, 0x00,
    0x48, 0x8D, 0x15, 0xA1, 0xF2, 0x8F, 0x00,
    0x0F, 0x1F, 0x40, 0x00
};

// Advertising set random address (0x10050ef2e on 26.4.1): for chipset <= 3 bluetoothd turns
// off LE Set Advertising Set Random Address, then enables sets whose own address type is
// random, which the controller rejects with Invalid HCI Command Parameters (0x12).
// Only skip that one call (jmp +7); the chipset == 0 block after it is left as is.
// cmp dword [rax+0x334], 3; ja +0x2a -> jmp +7
static const uint8_t kAdvSetAddrOriginal26[] =
{
    0x83, 0xB8, 0x34, 0x03, 0x00, 0x00, 0x03,
    0x77, 0x2A,
    0x31, 0xFF, 0xE8, 0xA2, 0x7D, 0xCE, 0xFF
};

static const uint8_t kAdvSetAddrPatched26[] =
{
    0x83, 0xB8, 0x34, 0x03, 0x00, 0x00, 0x03,
    0xEB, 0x07,
    0x31, 0xFF, 0xE8, 0xA2, 0x7D, 0xCE, 0xFF
};

// LE Extended Create Connection (0x10050eb5c on 26.4.1). With extended scanning on, the
// controller rejects legacy LE Create Connection (0x200d) with Command Disallowed, and after
// enough of those bluetoothd restarts itself ("Exceed max allowed consecutive command
// disallowed events"). Feed 1 into the setter of gEnableExtendedCreateConnectionCommand.
// movzx edi, byte [rbp-0xa2]; call ... -> mov edi, 1; nop2; call ...
static const uint8_t kExtCreateConnOriginal26[] =
{
    0x0F, 0xB6, 0xBD, 0x5E, 0xFF, 0xFF, 0xFF,
    0xE8, 0x2F, 0xB0, 0xCF, 0xFF
};

static const uint8_t kExtCreateConnPatched26[] =
{
    0xBF, 0x01, 0x00, 0x00, 0x00, 0x66, 0x90,
    0xE8, 0x2F, 0xB0, 0xCF, 0xFF
};

// Advertising set random address before every enable (0x1001f951b on 26.4.1). bluetoothd
// caches each set's random address and only sends LE Set Advertising Set Random Address
// when it changed. The Intel controller forgets the address when the set's parameters are
// rewritten (LE Set Extended Advertising Parameters with a random own address type), so
// the following enable fails with 0x12 over and over until the address rotates (in an
// HCI trace: 64693 failures without a fresh 0x2035, 0 with one). Always send it.
// mov edx, [rdx+rdi+3]; xor edx, [rax+rcx+0xd11]; or edx, r8d; je +0x80 -> nop6
static const uint8_t kAdvSetAddrResendOriginal26[] =
{
    0x8B, 0x54, 0x3A, 0x03,
    0x33, 0x94, 0x08, 0x11, 0x0D, 0x00, 0x00,
    0x44, 0x09, 0xC2,
    0x0F, 0x84, 0x80, 0x00, 0x00, 0x00
};

static const uint8_t kAdvSetAddrResendPatched26[] =
{
    0x8B, 0x54, 0x3A, 0x03,
    0x33, 0x94, 0x08, 0x11, 0x0D, 0x00, 0x00,
    0x44, 0x09, 0xC2,
    0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00
};

static bool shouldPatchBoardId = false;
static bool shouldPatchAddress = false;
static bool shouldPatchNvramCheck = false;
static bool shouldPatchA2DPCheck = false;
static bool loggedA2DPCheckMiss = false;
static bool shouldPatchSpecAdvSets = false;

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
    searchAndPatchWithMask(haystack, haystackSize, path, needle, findSize * sizeof(T), findMask, findMaskSize * sizeof(T), patch, replaceSize * sizeof(T), patchMask, replaceMaskSize * sizeof(T));
}




static bool searchAndPatchNamedWithMask(const void *haystack, size_t haystackSize, const char *path, const char *name, const void *needle, size_t findSize, const void *findMask, size_t findMaskSize, const void *patch, size_t replaceSize, const void *patchMask, size_t replaceMaskSize) {
    if (!KernelPatcher::findAndReplaceWithMask(const_cast<void *>(haystack), haystackSize, needle, findSize, findMask, findMaskSize, patch, replaceSize, patchMask, replaceMaskSize))
        return false;

    SYSLOG(MODULE_SHORT, "[a2dpcheck] patched %s in %s", name, path);
    return true;
}

template <size_t findSize, size_t findMaskSize, size_t replaceSize, size_t replaceMaskSize, typename T>
static bool searchAndPatchNamedWithMask(const void *haystack, size_t haystackSize, const char *path, const char *name, const T (&needle)[findSize], const T (&findMask)[findMaskSize], const T (&patch)[replaceSize], const T (&patchMask)[replaceMaskSize]) {
    return searchAndPatchNamedWithMask(haystack, haystackSize, path, name, needle, findSize * sizeof(T), findMask, findMaskSize * sizeof(T), patch, replaceSize * sizeof(T), patchMask, replaceMaskSize * sizeof(T));
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
            if (shouldPatchA2DPCheck) {
                bool patched = false;
                
                // macOS 14.x/15.x/26.x (Sonoma/Sequoia/Tahoe) pattern
                if (getKernelVersion() >= KernelVersion::Sonoma) {
                    patched |= searchAndPatchNamedWithMask(data, PAGE_SIZE, path, "fast-connect A2DP BT-clock gate (macOS 14+)", kFastConnectA2DPGateOriginal14_0, kFastConnectA2DPGateMask14_0, kFastConnectA2DPGatePatched14_0, kFastConnectA2DPGateMask14_0);
                }
                // macOS 12.x/13.x (Monterey/Ventura) pattern
                else if (getKernelVersion() >= KernelVersion::Monterey) {
                    patched |= searchAndPatchNamedWithMask(data, PAGE_SIZE, path, "fast-connect A2DP BT-clock gate (macOS 12/13)", kFastConnectA2DPGateOriginal12_0, kFastConnectA2DPGateMask12_0, kFastConnectA2DPGatePatched12_0, kFastConnectA2DPGateMask12_0);
                }

                if (!patched && !loggedA2DPCheckMiss) {
                    loggedA2DPCheckMiss = true;
                    SYSLOG(MODULE_SHORT, "[a2dpcheck] enabled for bluetoothd, but no known FastConnect Gate pattern was seen yet");
                }
            }
            if (shouldPatchSpecAdvSets &&
                KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kSpecAdvSetsCapOriginal26, sizeof(kSpecAdvSetsCapOriginal26), kSpecAdvSetsCapPatched26, sizeof(kSpecAdvSetsCapPatched26)))
                SYSLOG(MODULE_SHORT, "[advsets] patched BT Spec advertising sets capability in %s", path);
            if (shouldPatchSpecAdvSets &&
                KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kExtScanCapOriginal26, sizeof(kExtScanCapOriginal26), kSpecAdvSetsCapPatched26, sizeof(kSpecAdvSetsCapPatched26)))
                SYSLOG(MODULE_SHORT, "[advsets] patched extended scanning capability in %s", path);
            if (shouldPatchSpecAdvSets &&
                KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kLeEventMaskOriginal26, sizeof(kLeEventMaskOriginal26), kLeEventMaskPatched26, sizeof(kLeEventMaskPatched26)))
                SYSLOG(MODULE_SHORT, "[advsets] patched LE event mask in %s", path);
            if (shouldPatchSpecAdvSets &&
                KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kAdvSetAddrOriginal26, sizeof(kAdvSetAddrOriginal26), kAdvSetAddrPatched26, sizeof(kAdvSetAddrPatched26)))
                SYSLOG(MODULE_SHORT, "[advsets] patched advertising set random address in %s", path);
            if (shouldPatchSpecAdvSets &&
                KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kExtCreateConnOriginal26, sizeof(kExtCreateConnOriginal26), kExtCreateConnPatched26, sizeof(kExtCreateConnPatched26)))
                SYSLOG(MODULE_SHORT, "[advsets] patched extended create connection in %s", path);
            if (shouldPatchSpecAdvSets &&
                KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kAdvSetAddrResendOriginal26, sizeof(kAdvSetAddrResendOriginal26), kAdvSetAddrResendPatched26, sizeof(kAdvSetAddrResendPatched26)))
                SYSLOG(MODULE_SHORT, "[advsets] patched advertising set address resend in %s", path);
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
        shouldPatchA2DPCheck = checkKernelArgument("-btlfxa2dpcheck");
        shouldPatchSpecAdvSets = checkKernelArgument("-btlfxadvsets");
        if (shouldPatchSpecAdvSets)
            SYSLOG(MODULE_SHORT, "[advsets] BT Spec LE advertising sets for third-party controllers enabled");
        if (shouldPatchA2DPCheck)
            SYSLOG(MODULE_SHORT, "[a2dpcheck] bluetoothd Read Clock workaround enabled (fast-connect A2DP bypass)");
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
