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


#define MODULE_SHORT "btlfx"


class EXPORT BlueToolFixup : public IOService {
    OSDeclareDefaultStructors(BlueToolFixup)
public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
};

OSDefineMetaClassAndStructors(BlueToolFixup, IOService)

PRODUCT_NAME *ADDPR(selfInstance) = nullptr;

IOService *BlueToolFixup::probe(IOService *provider, SInt32 *score) {
    ADDPR(selfInstance) = this;
    setProperty("VersionInfo", kextVersion);
    setName("bluetooth");
    uint8_t bytes[] {0x00, 0x00, 0x00, 0x00};
    setProperty("transport-encoding", bytes, sizeof(bytes));
    auto service = IOService::probe(provider, score);
    return ADDPR(startSuccess) ? service : nullptr;
}

bool BlueToolFixup::start(IOService *provider) {
    ADDPR(selfInstance) = this;
    if (!IOService::start(provider)) {
        SYSLOG("init", "failed to start the parent");
        return false;
    }
    
    if (ADDPR(startSuccess)) {
        registerService();
    }
    
    return ADDPR(startSuccess);
}

void BlueToolFixup::stop(IOService *provider) {
    ADDPR(selfInstance) = nullptr;
    IOService::stop(provider);
}

static const int kPathMaxLen = 1024;

#pragma mark - Patches

static const uint8_t kSkipUpdateFilePathOriginal[] = "/etc/bluetool/SkipBluetoothAutomaticFirmwareUpdate";
static const uint8_t kSkipUpdateFilePathPatched[] = "/System/Library/CoreServices/boot.efi\0\0\0\0\0\0\0\0\0\0\0\0\0";

static_assert(sizeof(kSkipUpdateFilePathOriginal) == sizeof(kSkipUpdateFilePathPatched), "patch size invalid");

static const char blueToolPath[kPathMaxLen] = "/usr/sbin/BlueTool";

static mach_vm_address_t orig_cs_validate {};

#pragma mark - Kernel patching code

template <size_t patchSize>
static inline void searchAndPatch(const void *haystack,
                                  size_t haystackSize,
                                  const char (&path)[kPathMaxLen],
                                  const uint8_t (&needle)[patchSize],
                                  const uint8_t (&patch)[patchSize]) {
    if (UNLIKELY(strncmp(path, blueToolPath, sizeof(blueToolPath)) == 0)) {
        if (KernelPatcher::findAndReplace(const_cast<void *>(haystack), haystackSize, needle, patchSize, patch, patchSize)) {
            DBGLOG(MODULE_SHORT, "patch succeeded");
        } else {
            DBGLOG(MODULE_SHORT, "patch failed");
        }
    }
}

#pragma mark - Patched functions

// For Big Sur
static void patched_cs_validate_page(vnode_t vp,
                                     memory_object_t pager,
                                     memory_object_offset_t page_offset,
                                     const void *data,
                                     int *arg4,
                                     int *arg5,
                                     int *arg6) {
    char path[kPathMaxLen];
    int pathlen = kPathMaxLen;
    FunctionCast(patched_cs_validate_page, orig_cs_validate)(vp, pager, page_offset, data, arg4, arg5, arg6);
    if (vn_getpath(vp, path, &pathlen) == 0) {
        searchAndPatch(data, PAGE_SIZE, path, kSkipUpdateFilePathOriginal, kSkipUpdateFilePathPatched);
    }
}

#pragma mark - Patches on start/stop

static void pluginStart() {
    LiluAPI::Error error;
    
    SYSLOG(MODULE_SHORT, "start");
    // There is no point in routing cs_validate_range, because this kext should only be running on Monterey+
    if (getKernelVersion() >= KernelVersion::Monterey) {
        error = lilu.onPatcherLoad([](void *user, KernelPatcher &patcher){
            DBGLOG(MODULE_SHORT, "patching cs_validate_page");
            mach_vm_address_t kern = patcher.solveSymbol(KernelPatcher::KernelID, "_cs_validate_page");
            
            if (patcher.getError() == KernelPatcher::Error::NoError) {
                orig_cs_validate = patcher.routeFunctionLong(kern, reinterpret_cast<mach_vm_address_t>(patched_cs_validate_page), true, true);
                
                if (patcher.getError() != KernelPatcher::Error::NoError) {
                    SYSLOG(MODULE_SHORT, "failed to hook _cs_validate_page");
                } else {
                    DBGLOG(MODULE_SHORT, "hooked cs_validate_page");
                }
            } else {
                SYSLOG(MODULE_SHORT, "failed to find _cs_validate_page");
            }
        });
        
        if (error != LiluAPI::Error::NoError) {
            SYSLOG(MODULE_SHORT, "failed to register onPatcherLoad method: %d", error);
        }
    }
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
    LiluAPI::AllowNormal,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),
    bootargBeta,
    arrsize(bootargBeta),
    KernelVersion::Monterey,
    KernelVersion::Monterey,
    pluginStart
};

