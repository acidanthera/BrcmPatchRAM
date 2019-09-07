/*
 *  Released under "The GNU General Public License (GPL-2.0)"
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef TARGET_ELCAPITAN
#include <IOKit/usb/IOUSBInterface.h>
#else
#include <IOKit/usb/USB.h>
#endif
#include <IOKit/IOCatalogue.h>

#include <libkern/version.h>
#include <libkern/OSKextLib.h>

#include "Common.h"
#include "hci.h"
#include "BrcmPatchRAM.h"

//////////////////////////////////////////////////////////////////////////////////////////////////

enum { kMyOffPowerState = 0, kMyOnPowerState = 1 };

static IOPMPowerState myTwoStates[2] =
{
    { kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { kIOPMPowerStateVersion1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0 }
};

#ifndef TARGET_ELCAPITAN
OSDefineMetaClassAndStructors(BrcmPatchRAM, IOService)
#else
OSDefineMetaClassAndStructors(BrcmPatchRAM2, IOService)
#endif

OSString* BrcmPatchRAM::brcmBundleIdentifier = NULL;
OSString* BrcmPatchRAM::brcmIOClass = NULL;
OSString* BrcmPatchRAM::brcmProviderClass = NULL;

#ifndef NON_RESIDENT
IOLock* BrcmPatchRAM::mLoadFirmwareLock = IOLockAlloc();
#endif

extern "C"
{

__attribute__((visibility("hidden")))
kern_return_t BrcmPatchRAM_Start(kmod_info_t* ki, void * d)
{
#ifndef NON_RESIDENT
    if (!(BrcmPatchRAM::mLoadFirmwareLock = IOLockAlloc()))
        return KERN_FAILURE;
#endif

    return KERN_SUCCESS;
}

__attribute__((visibility("hidden")))
kern_return_t BrcmPatchRAM_Stop(kmod_info_t* ki, void * d)
{
#ifndef NON_RESIDENT
    if (BrcmPatchRAM::mLoadFirmwareLock)
    {
        IOLockFree(BrcmPatchRAM::mLoadFirmwareLock);
        BrcmPatchRAM::mLoadFirmwareLock = NULL;
    }
#endif

    return KERN_SUCCESS;
}

} // extern "C"

void BrcmPatchRAM::initBrcmStrings()
{
    if (!brcmBundleIdentifier)
    {
        const char* bundle = NULL;
        const char* ioclass = NULL;
        const char* providerclass = kIOUSBDeviceClassName;
        
        // OS X - Snow Leopard
        // OS X - Lion
        if (version_major == 10 || version_major == 11)
        {
            bundle = "com.apple.driver.BroadcomUSBBluetoothHCIController";
            ioclass = "BroadcomUSBBluetoothHCIController";
        }
        // OS X - Mountain Lion (12.0 until 12.4)
        else if (version_major == 12 && version_minor <= 4)
        {
            bundle = "com.apple.iokit.BroadcomBluetoothHCIControllerUSBTransport";
            ioclass = "BroadcomBluetoothHCIControllerUSBTransport";
        }
        // OS X - Mountain Lion (12.5.0)
        // OS X - Mavericks
        // OS X - Yosemite
        else if (version_major == 12 || version_major == 13 || version_major == 14)
        {
            bundle = "com.apple.iokit.BroadcomBluetoothHostControllerUSBTransport";
            ioclass = "BroadcomBluetoothHostControllerUSBTransport";
        }
        // OS X - El Capitan
		// macOS - Sierra
		// macOS - High Sierra
		// macOS - Mojave
		// macOS - Catalina
        else if (version_major == 15 || version_major == 16 || version_major == 17 || version_major == 18)
        {
            bundle = "com.apple.iokit.BroadcomBluetoothHostControllerUSBTransport";
            ioclass = "BroadcomBluetoothHostControllerUSBTransport";
            providerclass = kIOUSBHostDeviceClassName;
        }
        // OS X - Future releases....
        else if (version_major > 18)
        {
            AlwaysLog("Unknown new Darwin version %d.%d, using possible compatible personality.\n", version_major, version_minor);
            bundle = "com.apple.iokit.BroadcomBluetoothHostControllerUSBTransport";
            ioclass = "BroadcomBluetoothHostControllerUSBTransport";
            providerclass = kIOUSBHostDeviceClassName;
        }
        else
        {
            AlwaysLog("Unknown Darwin version %d.%d, no compatible personality known.\n", version_major, version_minor);
        }
        brcmBundleIdentifier = OSString::withCStringNoCopy(bundle);
        brcmIOClass = OSString::withCStringNoCopy(ioclass);
        brcmProviderClass = OSString::withCStringNoCopy(providerclass);
    }
}

IOService* BrcmPatchRAM::probe(IOService *provider, SInt32 *probeScore)
{
    uint64_t start_time, end_time, nano_secs;

    DebugLog("probe\n");

    AlwaysLog("Version %s starting on OS X Darwin %d.%d.\n", OSKextGetCurrentVersionString(), version_major, version_minor);

#ifdef TARGET_ELCAPITAN
    // preference towards starting BrcmPatchRAM2.kext when BrcmPatchRAM.kext also exists
    *probeScore = 2000;
#endif

#ifndef TARGET_ELCAPITAN
    // BrcmPatchRAM.kext, if installed on 10.11+... fails immediately
    if (version_major >= 15)
    {
        AlwaysLog("Aborting -- BrcmPatchRAM.kext should not be installed on 10.11+.  Use BrcmPatchRAM2.kext instead.\n");
        return NULL;
    }
#endif

    clock_get_uptime(&start_time);

#ifndef NON_RESIDENT
    mWorkLock = IOLockAlloc();
    if (!mWorkLock)
        return NULL;

    // Note: mLoadFirmwareLock is static (global), not instance data...
    if (!mLoadFirmwareLock)
        return NULL;
#endif

    mCompletionLock = IOLockAlloc();
    if (!mCompletionLock)
        return NULL;

    mDevice.setDevice(provider);
    if (!mDevice.getValidatedDevice())
    {
        AlwaysLog("Provider type is incorrect (not IOUSBDevice or IOUSBHostDevice)\n");
        return NULL;
    }

    // personality strings depend on version
    initBrcmStrings();

#ifndef NON_RESIDENT
    // longest time seen in normal re-probe was ~200ms (400+ms on 10.11)
    if (version_major >= 15)
        mBlurpWait = 800;
    else
        mBlurpWait = 400;
#endif

    UInt32 delay;
    mProbeDelay = 0;
    if (OSNumber* probeDelay = OSDynamicCast(OSNumber, getProperty("ProbeDelay")))
        mProbeDelay = probeDelay->unsigned32BitValue();
    if (PE_parse_boot_argn("bpr_probedelay", &delay, sizeof delay))
        mProbeDelay = delay;

    mInitialDelay = 100;
    if (OSNumber* initialDelay = OSDynamicCast(OSNumber, getProperty("InitialDelay")))
        mInitialDelay = initialDelay->unsigned32BitValue();
    if (PE_parse_boot_argn("bpr_initialdelay", &delay, sizeof delay))
        mInitialDelay = delay;

    mPostResetDelay = 100;
    if (OSNumber* postResetDelay = OSDynamicCast(OSNumber, getProperty("PostResetDelay")))
        mPostResetDelay = postResetDelay->unsigned32BitValue();
    if (PE_parse_boot_argn("bpr_postresetdelay", &delay, sizeof delay))
        mPostResetDelay = delay;

    mPreResetDelay = 20;
    if (OSNumber* preResetDelay = OSDynamicCast(OSNumber, getProperty("PreResetDelay")))
        mPreResetDelay = preResetDelay->unsigned32BitValue();
    if (PE_parse_boot_argn("bpr_preresetdelay", &delay, sizeof delay))
        mPreResetDelay = delay;

    if (OSString* displayName = OSDynamicCast(OSString, getProperty(kDisplayName)))
        provider->setProperty(kUSBProductString, displayName);
    
    mVendorId = mDevice.getVendorID();
    mProductId = mDevice.getProductID();

    // get firmware here to pre-cache for eventual use on wakeup or now
    if (OSString* firmwareKey = OSDynamicCast(OSString, getProperty(kFirmwareKey)))
    {
        if (BrcmFirmwareStore* firmwareStore = getFirmwareStore())
            firmwareStore->getFirmware(mVendorId, mProductId, firmwareKey);
    }

    IOSleep(mProbeDelay);

    uploadFirmware();
#ifndef TARGET_CATALINA
    publishPersonality();
#endif

    clock_get_uptime(&end_time);
    absolutetime_to_nanoseconds(end_time - start_time, &nano_secs);
    uint64_t milli_secs = nano_secs / 1000000;
    AlwaysLog("Processing time %llu.%llu seconds.\n", milli_secs / 1000, milli_secs % 1000);

#ifdef NON_RESIDENT
    // maybe residency is not required for 10.11?
    mDevice.setDevice(NULL);
    return NULL;
#endif

    return this;
}

#ifndef NON_RESIDENT
bool BrcmPatchRAM::start(IOService *provider)
{
    DebugLog("start\n");

    if (!super::start(provider))
        return false;

    // place version/build info in ioreg properties RM,Build and RM,Version
    char buf[128];
    snprintf(buf, sizeof(buf), "%s %s", OSKextGetCurrentIdentifier(), OSKextGetCurrentVersionString());
    setProperty("RM,Version", buf);
#ifdef DEBUG
    setProperty("RM,Build", "Debug-" LOGNAME);
#else
    setProperty("RM,Build", "Release-" LOGNAME);
#endif

   // add interrupt source for delayed actions...
    IOWorkLoop* workLoop = getWorkLoop();
    if (!workLoop)
        return false;
    mWorkSource = IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &BrcmPatchRAM::processWorkQueue));
    if (!mWorkSource)
        return false;
    workLoop->addEventSource(mWorkSource);
    mWorkPending = 0;

    // add timer for firmware load in the case no re-probe after wake
    mTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &BrcmPatchRAM::onTimerEvent));
    if (!mTimer)
    {
        workLoop->removeEventSource(mWorkSource);
        mWorkSource->release();
        mWorkSource = NULL;
        return false;
    }
    workLoop->addEventSource(mTimer);

    // register for power state notifications
    PMinit();
    registerPowerDriver(this, myTwoStates, 2);
    provider->joinPMtree(this);
    
    return true;
}

#ifdef DEBUG
static uint64_t wake_time;
#endif

void BrcmPatchRAM::stop(IOService* provider)
{
#ifdef DEBUG
    uint64_t stop_time, nano_secs;
    clock_get_uptime(&stop_time);
    absolutetime_to_nanoseconds(stop_time - wake_time, &nano_secs);
    uint64_t milli_secs = nano_secs / 1000000;
    AlwaysLog("Time since wake %llu.%llu seconds.\n", milli_secs / 1000, milli_secs % 1000);
#endif

    DebugLog("stop\n");

#if 0
#ifndef TARGET_ELCAPITAN
//REVIEW: so kext can be unloaded with kextunload -p
    // unload native bluetooth driver
    IOReturn result = gIOCatalogue->terminateDriversForModule(brcmBundleIdentifier, false);
    if (result != kIOReturnSuccess)
        AlwaysLog("[%04x:%04x]: failure terminating native Broadcom bluetooth (%08x)\n", mVendorId, mProductId, result);
    else
        DebugLog("[%04x:%04x]: success terminating native Broadcom bluetooth\n", mVendorId, mProductId);

    // unpublish native bluetooth personality
    removePersonality();
#endif
#endif

    mStopping = true;

    // allow firmware load already started to finish
    IOLockLock(mLoadFirmwareLock);

    OSSafeReleaseNULL(mFirmwareStore);

    IOWorkLoop* workLoop = getWorkLoop();
    if (workLoop)
    {
        if (mTimer)
        {
            mTimer->cancelTimeout();
            workLoop->removeEventSource(mTimer);
            mTimer->release();
            mTimer = NULL;
        }
        if (mWorkSource)
        {
            workLoop->removeEventSource(mWorkSource);
            mWorkSource->release();
            mWorkSource = NULL;
            mWorkPending = 0;
        }
    }

    PMstop();

    if (mCompletionLock)
    {
        IOLockFree(mCompletionLock);
        mCompletionLock = NULL;
    }
#ifndef NON_RESIDENT
    if (mWorkLock)
    {
        IOLockFree(mWorkLock);
        mWorkLock = NULL;
    }

    IOLockUnlock(mLoadFirmwareLock);
#endif // #ifndef NON_RESIDENT

    mDevice.setDevice(NULL);

    mStopping = false;

    super::stop(provider);
}

IOReturn BrcmPatchRAM::onTimerEvent()
{
    DebugLog("onTimerEvent\n");

    if (!mDevice.getProperty(kFirmwareLoaded))
    {
        AlwaysLog("BLURP!! no firmware loaded and timer expiried (no re-probe)\n");
        scheduleWork(kWorkLoadFirmware);
    }

    return kIOReturnSuccess;
}

void BrcmPatchRAM::scheduleWork(unsigned int newWork)
{
    IOLockLock(mWorkLock);
    mWorkPending |= newWork;
    mWorkSource->interruptOccurred(0, 0, 0);
    IOLockUnlock(mWorkLock);
}

void BrcmPatchRAM::processWorkQueue(IOInterruptEventSource*, int)
{
    IOLockLock(mWorkLock);

    // start firmware loading process in a non-workloop thread
    if (mWorkPending & kWorkLoadFirmware)
    {
        DebugLog("_workPending kWorkLoadFirmware\n");
        mWorkPending &= ~kWorkLoadFirmware;
        retain();
        kern_return_t result = kernel_thread_start(&BrcmPatchRAM::uploadFirmwareThread, this, &mWorker);
        if (KERN_SUCCESS == result)
            DebugLog("Success creating firmware uploader thread\n");
        else
        {
            AlwaysLog("ERROR creating firmware uploader thread.\n");
            release();
        }
    }

    // firmware loading thread is finished
    if (mWorkPending & kWorkFinished)
    {
        DebugLog("_workPending kWorkFinished\n");
        mWorkPending &= ~kWorkFinished;
        thread_deallocate(mWorker);
        mWorker = 0;
        release();  // matching retain when thread created successfully
    }

    IOLockUnlock(mWorkLock);
}

void BrcmPatchRAM::uploadFirmwareThread(void *arg, wait_result_t wait)
{
    DebugLog("sendFirmwareThread enter\n");

    // don't start firmware load when lock is held (instance is shutting down)
    if (IOLockTryLock(mLoadFirmwareLock))
    {
        BrcmPatchRAM* me = static_cast<BrcmPatchRAM*>(arg);
        me->resetDevice();
        IOSleep(me->mPostResetDelay);
        me->uploadFirmware();
#ifndef TARGET_ELCAPITAN
        me->publishPersonality();
#endif
        me->scheduleWork(kWorkFinished);
        IOLockUnlock(mLoadFirmwareLock);
    }

    DebugLog("sendFirmwareThread termination\n");
    thread_terminate(current_thread());
    DebugLog("!!! sendFirmwareThread post-terminate !!! should not be here\n");
}

#endif // #ifndef NON_RESIDENT

void BrcmPatchRAM::uploadFirmware()
{
    // signal to timer that firmware already loaded
    mDevice.setProperty(kFirmwareLoaded, true);

    // don't bother with devices that have no firmware
    if (!getProperty(kFirmwareKey))
        return;

    if (!mDevice.open(this))
    {
        AlwaysLog("uploadFirmware could not open the device!\n");
        return;
    }

    //REVIEW: this block to avoid merge conflicts (remove once merged)
    //if (mDevice.open(this))
    {
        // Print out additional device information
        printDeviceInfo();
        
        // Set device configuration to composite configuration index 0
        // Obtain first interface
        if (setConfiguration(0) && findInterface(&mInterface) && mInterface.open(this))
        {
            DebugLog("set configuration and interface opened\n");
            mInterface.findPipe(&mInterruptPipe, kUSBInterrupt, kUSBIn);
            mInterface.findPipe(&mBulkPipe, kUSBBulk, kUSBOut);
            if (mInterruptPipe.getValidatedPipe() && mBulkPipe.getValidatedPipe())
            {
                DebugLog("got pipes\n");
                if (performUpgrade())
                    if (mDeviceState == kUpdateComplete)
                        AlwaysLog("[%04x:%04x]: Firmware upgrade completed successfully.\n", mVendorId, mProductId);
                    else
                        AlwaysLog("[%04x:%04x]: Firmware upgrade not needed.\n", mVendorId, mProductId);
                else
                    AlwaysLog("[%04x:%04x]: Firmware upgrade failed.\n", mVendorId, mProductId);
                OSSafeReleaseNULL(mReadBuffer); // mReadBuffer is allocated by performUpgrade but not released
            }
            mInterface.close(this);
        }
        
        // cleanup
        if (mInterruptPipe.getValidatedPipe())
        {
            mInterruptPipe.abort();
            mInterruptPipe.setPipe(NULL);
        }
        if (mBulkPipe.getValidatedPipe())
        {
            mBulkPipe.abort();
            mBulkPipe.setPipe(NULL);
        }
        mInterface.setInterface(NULL);
        mDevice.close(this);
    }
}

#ifndef NON_RESIDENT
IOReturn BrcmPatchRAM::setPowerState(unsigned long which, IOService *whom)
{
    DebugLog("setPowerState: which = 0x%lx\n", which);
    
    if (which == kMyOffPowerState)
    {
        // consider firmware no longer loaded
        mDevice.removeProperty(kFirmwareLoaded);

        // in the case the instance is shutting down, don't do anything
        if (!mStopping)
        {
#ifndef TARGET_ELCAPITAN
            // unload native bluetooth driver
            IOReturn result = gIOCatalogue->terminateDriversForModule(brcmBundleIdentifier, false);
            if (result != kIOReturnSuccess)
                AlwaysLog("[%04x:%04x]: failure terminating native Broadcom bluetooth (%08x)\n", mVendorId, mProductId, result);
            else
                DebugLog("[%04x:%04x]: success terminating native Broadcom bluetooth\n", mVendorId, mProductId);

            // unpublish native bluetooth personality
            removePersonality();
#endif
        }
    }
    else if (which == kMyOnPowerState)
    {
#ifdef DEBUG
        clock_get_uptime(&wake_time);
#endif
        // start a timer for loading firmware for case probe is never called after wake
        if (!mDevice.getProperty(kFirmwareLoaded))
            mTimer->setTimeoutMS(mBlurpWait);
    }

    return IOPMAckImplied;
}
#endif // #ifndef NON_RESIDENT

static void setStringInDict(OSDictionary* dict, const char* key, const char* value)
{
    OSString* str = OSString::withCStringNoCopy(value);
    if (str)
    {
        dict->setObject(key, str);
        str->release();
    }
}

static void setNumberInDict(OSDictionary* dict, const char* key, UInt16 value)
{
    OSNumber* num = OSNumber::withNumber(value, 16);
    if (num)
    {
        dict->setObject(key, num);
        num->release();
    }
}

#ifdef DEBUG
void BrcmPatchRAM::printPersonalities()
{
    // Matching dictionary for the current device
    OSDictionary* dict = OSDictionary::withCapacity(5);
    if (!dict) return;
    dict->setObject(kIOProviderClassKey, brcmProviderClass);
    setNumberInDict(dict, kUSBProductID, mProductId);
    setNumberInDict(dict, kUSBVendorID, mVendorId);
    
    SInt32 generatonCount;
    if (OSOrderedSet* set = gIOCatalogue->findDrivers(dict, &generatonCount))
    {
        AlwaysLog("[%04x:%04x]: %d matching driver personalities.\n", mVendorId, mProductId, set->getCount());
        if (OSCollectionIterator* iterator = OSCollectionIterator::withCollection(set))
        {
            while (OSDictionary* personality = static_cast<OSDictionary*>(iterator->getNextObject()))
            {
                OSString* bundleId = OSDynamicCast(OSString, personality->getObject(kBundleIdentifier));
                AlwaysLog("[%04x:%04x]: existing IOKit personality \"%s\".\n", mVendorId, mProductId, bundleId->getCStringNoCopy());
            }
            iterator->release();
        }
        set->release();
    }
    dict->release();
}
#endif //DEBUG

#ifndef NON_RESIDENT
#ifndef TARGET_ELCAPITAN
void BrcmPatchRAM::removePersonality()
{
    DebugLog("removePersonality\n");
    
#ifdef DEBUG
    printPersonalities();
#endif
    
    // remove Broadcom matching personality
    OSDictionary* dict = OSDictionary::withCapacity(4);
    if (!dict) return;
    dict->setObject(kIOProviderClassKey, brcmProviderClass);
    setNumberInDict(dict, kUSBProductID, mProductId);
    setNumberInDict(dict, kUSBVendorID, mVendorId);
    dict->setObject(kBundleIdentifier, brcmBundleIdentifier);
    gIOCatalogue->removeDrivers(dict, false); // no nub matching on removal

    // remove generic matching personality
    dict->removeObject(kUSBProductID);
    dict->removeObject(kUSBVendorID);
    setStringInDict(dict, kBundleIdentifier, "com.apple.iokit.IOBluetoothHostControllerUSBTransport");
    setNumberInDict(dict, "bDeviceClass", 224);
    setNumberInDict(dict, "bDeviceProtocol", 1);
    setNumberInDict(dict, "bDeviceSubClass", 1);
    gIOCatalogue->removeDrivers(dict, false); // no nub matching on removal

    dict->release();
    
#ifdef DEBUG
    printPersonalities();
#endif
}
#endif // #ifndef TARGET_ELCAPITAN
#endif // #ifndef NON_RESIDENT

#ifndef TARGET_CATALINA
void BrcmPatchRAM::publishPersonality()
{
    // Matching dictionary for the current device
    OSDictionary* dict = OSDictionary::withCapacity(5);
    if (!dict) return;
    dict->setObject(kIOProviderClassKey, brcmProviderClass);
    dict->setObject(kIOClassKey, brcmIOClass);
    setNumberInDict(dict, kUSBProductID, mProductId);
    setNumberInDict(dict, kUSBVendorID, mVendorId);

    // Retrieve currently matching IOKit driver personalities
    OSDictionary* personality = NULL;
    SInt32 generationCount;
    if (OSOrderedSet* set = gIOCatalogue->findDrivers(dict, &generationCount))
    {
        if (set->getCount())
            DebugLog("[%04x:%04x]: %d matching driver personalities.\n", mVendorId, mProductId, set->getCount());
        
        if (OSCollectionIterator* iterator = OSCollectionIterator::withCollection(set))
        {
            while ((personality = OSDynamicCast(OSDictionary, iterator->getNextObject())))
            {
                if (OSString* bundleId = OSDynamicCast(OSString, personality->getObject(kBundleIdentifier)))
                    if (strncmp(bundleId->getCStringNoCopy(), kAppleBundlePrefix, strlen(kAppleBundlePrefix)) == 0)
                    {
                        AlwaysLog("[%04x:%04x]: Found existing IOKit personality \"%s\".\n", mVendorId, mProductId, bundleId->getCStringNoCopy());
                        break;
                    }
            }
            iterator->release();
        }
        set->release();
    }
    
    if (!personality && brcmBundleIdentifier)
    {
        // OS X does not have a driver personality for this device yet, publish one
        DebugLog("brcmBundIdentifier: \"%s\"\n", brcmBundleIdentifier->getCStringNoCopy());
        DebugLog("brcmIOClass: \"%s\"\n", brcmIOClass->getCStringNoCopy());
        DebugLog("brcmProviderClass: \"%s\"\n", brcmProviderClass->getCStringNoCopy());
        dict->setObject(kBundleIdentifier, brcmBundleIdentifier);

        // Add new personality into the kernel
        if (OSArray* array = OSArray::withCapacity(1))
        {
            array->setObject(dict);
            if (gIOCatalogue->addDrivers(array, false))
            {
                AlwaysLog("[%04x:%04x]: Published new IOKit personality.\n", mVendorId, mProductId);
                if (OSDictionary* dict1 = OSDynamicCast(OSDictionary, dict->copyCollection()))
                {
                    //dict1->removeObject(kIOClassKey);
                    //dict1->removeObject(kIOProviderClassKey);
                    dict1->removeObject(kUSBProductID);
                    dict1->removeObject(kUSBVendorID);
                    dict1->removeObject(kBundleIdentifier);
                    if (!gIOCatalogue->startMatching(dict1))
                        AlwaysLog("[%04x:%04x]: startMatching failed.\n", mVendorId, mProductId);
                    dict1->release();
                }
            }
            else
                AlwaysLog("[%04x:%04x]: ERROR in addDrivers for new IOKit personality.\n", mVendorId, mProductId);
            array->release();
        }
    }
    dict->release();

#ifdef DEBUG
    printPersonalities();
#endif
}

bool BrcmPatchRAM::publishResourcePersonality(const char* classname)
{
    // matching dictionary for disabled BrcmFirmwareStore
    OSDictionary* dict = OSDictionary::withCapacity(3);
    if (!dict) return false;
    setStringInDict(dict, kIOProviderClassKey, "disabled_IOResources");
    setStringInDict(dict, kIOClassKey, classname);
    setStringInDict(dict, kIOMatchCategoryKey, classname);

    // retrieve currently matching IOKit driver personalities
    OSDictionary* personality = NULL;
    SInt32 generationCount;
    if (OSOrderedSet* set = gIOCatalogue->findDrivers(dict, &generationCount))
    {
        if (set->getCount())
            DebugLog("%d matching driver personalities for %s.\n", set->getCount(), classname);

        // should be only one, so we can grab just the first
        if (OSCollectionIterator* iterator = OSCollectionIterator::withCollection(set))
        {
            personality = OSDynamicCast(OSDictionary, iterator->getNextObject());
            iterator->release();
        }
        set->release();
    }
    // if we don't find it, then something is really wrong...
    if (!personality)
    {
        AlwaysLog("unable to find disabled %s personality.\n", classname);
        dict->release();
        return false;
    }
    // make copy of personality *before* removing from IOcatalog
    personality = OSDynamicCast(OSDictionary, personality->copyCollection());
    if (!personality)
    {
        AlwaysLog("copyCollection failed.");
        return false;
    }

    // unpublish disabled personality
    gIOCatalogue->removeDrivers(dict, false);  // no nub matching on removal
    dict->release();

    // Add new personality into the kernel
    if (OSArray* array = OSArray::withCapacity(1))
    {
        // change from disabled_IOResources to IOResources
        setStringInDict(personality, kIOProviderClassKey, "IOResources");
        array->setObject(personality);
        if (gIOCatalogue->addDrivers(array, true))
            AlwaysLog("Published new IOKit personality for %s.\n", classname);
        else
            AlwaysLog("ERROR in addDrivers for new %s personality.\n", classname);
        array->release();
    }
    personality->release();

    return true;
}
#endif

BrcmFirmwareStore* BrcmPatchRAM::getFirmwareStore()
{
    if (!mFirmwareStore)
    {
        // check to see if it already loaded
        mFirmwareStore = OSDynamicCast(BrcmFirmwareStore, waitForMatchingService(serviceMatching(kBrcmFirmwareStoreService), 0));
        if (!mFirmwareStore)
        {
            // not loaded, so publish personality...
#ifndef TARGET_CATALINA
            publishResourcePersonality(kBrcmFirmwareStoreService);
#endif
            // and wait...
            mFirmwareStore = OSDynamicCast(BrcmFirmwareStore, waitForMatchingService(serviceMatching(kBrcmFirmwareStoreService), 2000UL*1000UL*1000UL));
        }

#ifdef NON_RESIDENT
        // also need BrcmPatchRAMResidency
        IOService* residency = OSDynamicCast(BrcmPatchRAMResidency, waitForMatchingService(serviceMatching(kBrcmPatchRAMResidency), 0));
        if (!residency)
        {
            // not loaded, so publish personality...
#ifndef TARGET_CATALINA
            publishResourcePersonality(kBrcmPatchRAMResidency);
#endif
            // and wait...
            residency = OSDynamicCast(BrcmPatchRAMResidency, waitForMatchingService(serviceMatching(kBrcmPatchRAMResidency), 2000UL*1000UL*1000UL));
            if (residency)
                residency->release();
            else
                AlwaysLog("[%04x:%04x]: BrcmPatchRAMResidency does not appear to be available.\n", mVendorId, mProductId);
        }
#endif
    }
    
    if (!mFirmwareStore)
        AlwaysLog("[%04x:%04x]: BrcmFirmwareStore does not appear to be available.\n", mVendorId, mProductId);
    
    return mFirmwareStore;
}

void BrcmPatchRAM::printDeviceInfo()
{
    char product[255];
    char manufacturer[255];
    char serial[255];
    
    // Retrieve device information
    mDevice.getStringDescriptor(mDevice.getProductStringIndex(), product, sizeof(product));
    mDevice.getStringDescriptor(mDevice.getManufacturerStringIndex(), manufacturer, sizeof(manufacturer));
    mDevice.getStringDescriptor(mDevice.getSerialNumberStringIndex(), serial, sizeof(serial));
    
    AlwaysLog("[%04x:%04x]: USB [%s v%d] \"%s\" by \"%s\"\n",
              mVendorId,
              mProductId,
              serial,
              mDevice.getDeviceRelease(),
              product,
              manufacturer);
}

int BrcmPatchRAM::getDeviceStatus()
{
    IOReturn result;
    USBStatus status;
    
    if ((result = mDevice.getDeviceStatus(this, &status)) != kIOReturnSuccess)
    {
        AlwaysLog("[%04x:%04x]: Unable to get device status (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        return 0;
    }
    else
        DebugLog("[%04x:%04x]: Device status 0x%08x.\n", mVendorId, mProductId, (int)status);
    
    return (int)status;
}

bool BrcmPatchRAM::resetDevice()
{
    IOReturn result;
    
    if ((result = mDevice.resetDevice()) != kIOReturnSuccess)
    {
        AlwaysLog("[%04x:%04x]: Failed to reset the device (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        return false;
    }
    else
        DebugLog("[%04x:%04x]: Device reset.\n", mVendorId, mProductId);
    
    return true;
}

bool BrcmPatchRAM::setConfiguration(int configurationIndex)
{
    IOReturn result;
    const USBCONFIGURATIONDESCRIPTOR* configurationDescriptor;
    UInt8 currentConfiguration = 0xFF;
    
    // Find the first config/interface
    UInt8 numconf = 0;
    
    if ((numconf = mDevice.getNumConfigurations()) < (configurationIndex + 1))
    {
        AlwaysLog("[%04x:%04x]: Composite configuration index %d is not available, %d total composite configurations.\n",
                  mVendorId, mProductId, configurationIndex, numconf);
        return false;
    }
    else
        DebugLog("[%04x:%04x]: Available composite configurations: %d.\n", mVendorId, mProductId, numconf);
    
    configurationDescriptor = mDevice.getFullConfigurationDescriptor(configurationIndex);
    
    // Set the configuration to the requested configuration index
    if (!configurationDescriptor)
    {
        AlwaysLog("[%04x:%04x]: No configuration descriptor for configuration index: %d.\n", mVendorId, mProductId, configurationIndex);
        return false;
    }
    
    if ((result = mDevice.getConfiguration(this, &currentConfiguration)) != kIOReturnSuccess)
    {
        AlwaysLog("[%04x:%04x]: Unable to retrieve active configuration (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        return false;
    }
    
    // Device is already configured
    if (currentConfiguration != 0)
    {
        DebugLog("[%04x:%04x]: Device configuration is already set to configuration index %d.\n",
                 mVendorId, mProductId, configurationIndex);
        return true;
    }
    
    // Set the configuration to the first configuration
    if ((result = mDevice.setConfiguration(this, configurationDescriptor->bConfigurationValue, true)) != kIOReturnSuccess)
    {
        AlwaysLog("[%04x:%04x]: Unable to (re-)configure device (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        return false;
    }
    
    DebugLog("[%04x:%04x]: Set device configuration to configuration index %d successfully.\n",
             mVendorId, mProductId, configurationIndex);
    
    return true;
}

bool BrcmPatchRAM::findInterface(USBInterfaceShim* shim)
{
    mDevice.findFirstInterface(shim);
    if (IOService* interface = shim->getValidatedInterface())
    {
        DebugLog("[%04x:%04x]: Interface %d (class %02x, subclass %02x, protocol %02x) located.\n",
                 mVendorId,
                 mProductId,
                 shim->getInterfaceNumber(),
                 shim->getInterfaceClass(),
                 shim->getInterfaceSubClass(),
                 shim->getInterfaceProtocol());
        
        return true;
    }
    
    AlwaysLog("[%04x:%04x]: No interface could be located.\n", mVendorId, mProductId);
    
    return false;
}

bool BrcmPatchRAM::findPipe(USBPipeShim* shim, UInt8 type, UInt8 direction)
{
    if (!mInterface.findPipe(shim, type, direction))
    {
        AlwaysLog("[%04x:%04x]: Unable to locate pipe.\n", mVendorId, mProductId);
        return false;
    }
    
#ifdef DEBUG
    const USBENDPOINTDESCRIPTOR* desc = shim->getEndpointDescriptor();
    if (!desc)
        DebugLog("[%04x:%04x]: No endpoint descriptor for pipe.\n", mVendorId, mProductId);
    else
        DebugLog("[%04x:%04x]: Located pipe at 0x%02x.\n", mVendorId, mProductId, desc->bEndpointAddress);
#endif

    return true;
}

bool BrcmPatchRAM::continuousRead()
{
    if (!mReadBuffer)
    {
        mReadBuffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionIn, 0x200);
        if (!mReadBuffer)
        {
            AlwaysLog("[%04x:%04x]: continuousRead - failed to allocate read buffer.\n", mVendorId, mProductId);
            return false;
        }
#ifndef TARGET_ELCAPITAN
        mInterruptCompletion.target = this;
#else
        mInterruptCompletion.owner = this;
#endif
        mInterruptCompletion.action = readCompletion;
        mInterruptCompletion.parameter = NULL;
    }

    IOReturn result = mReadBuffer->prepare(kIODirectionIn);
    if (result != kIOReturnSuccess)
    {
        AlwaysLog("[%04x:%04x]: continuousRead - failed to prepare buffer (0x%08x)\n", mVendorId, mProductId, result);
        return false;
    }

    if ((result = mInterruptPipe.read(mReadBuffer, 0, 0, mReadBuffer->getLength(), &mInterruptCompletion)) != kIOReturnSuccess)
    {
        AlwaysLog("[%04x:%04x]: continuousRead - Failed to queue read (0x%08x)\n", mVendorId, mProductId, result);

        if (result == kIOUSBPipeStalled)
        {
            mInterruptPipe.clearStall();
            result = mInterruptPipe.read(mReadBuffer, 0, 0, mReadBuffer->getLength(), &mInterruptCompletion);
            
            if (result != kIOReturnSuccess)
            {
                AlwaysLog("[%04x:%04x]: continuousRead - Failed, read dead (0x%08x)\n", mVendorId, mProductId, result);
                return false;
            }
        }
    }

    return true;
}

#ifndef TARGET_ELCAPITAN
void BrcmPatchRAM::readCompletion(void* target, void* parameter, IOReturn status, UInt32 bufferSizeRemaining)
#else
void BrcmPatchRAM::readCompletion(void* target, void* parameter, IOReturn status, uint32_t bytesTransferred)
#endif
{
    BrcmPatchRAM *me = (BrcmPatchRAM*)target;

    IOLockLock(me->mCompletionLock);

    IOReturn result = me->mReadBuffer->complete(kIODirectionIn);
    if (result != kIOReturnSuccess)
        DebugLog("[%04x:%04x]: ReadCompletion failed to complete read buffer (\"%s\" 0x%08x).\n", me->mVendorId, me->mProductId, me->stringFromReturn(result), result);

    switch (status)
    {
        case kIOReturnSuccess:
#ifndef TARGET_ELCAPITAN
            me->hciParseResponse(me->mReadBuffer->getBytesNoCopy(), me->mReadBuffer->getLength() - bufferSizeRemaining, NULL, NULL);
#else
            me->hciParseResponse(me->mReadBuffer->getBytesNoCopy(), bytesTransferred, NULL, NULL);
#endif
            break;
        case kIOReturnAborted:
            AlwaysLog("[%04x:%04x]: readCompletion - Return aborted (0x%08x)\n", me->mVendorId, me->mProductId, status);
            me->mDeviceState = kUpdateAborted;
            break;
        case kIOReturnNoDevice:
            AlwaysLog("[%04x:%04x]: readCompletion - No such device (0x%08x)\n", me->mVendorId, me->mProductId, status);
            me->mDeviceState = kUpdateAborted;
            break;
        case kIOUSBTransactionTimeout:
            AlwaysLog("[%04x:%04x]: readCompletion - Transaction timeout (0x%08x)\n", me->mVendorId, me->mProductId, status);
            break;
        case kIOReturnNotResponding:
            AlwaysLog("[%04x:%04x]: Not responding - Delaying next read.\n", me->mVendorId, me->mProductId);
            me->mInterruptPipe.clearStall();
            break;
        default:
            AlwaysLog("[%04x:%04x]: readCompletion - Unknown error (0x%08x)\n", me->mVendorId, me->mProductId, status);
            me->mDeviceState = kUpdateAborted;
            break;
    }

    IOLockUnlock(me->mCompletionLock);

    // wake waiting task in performUpgrade (in IOLockSleep)...
    IOLockWakeup(me->mCompletionLock, me, true);
}

IOReturn BrcmPatchRAM::hciCommand(void * command, UInt16 length)
{
    IOReturn result;
    if ((result = mInterface.hciCommand(command, length)) != kIOReturnSuccess)
        AlwaysLog("[%04x:%04x]: device request failed (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
    
    return result;
}

IOReturn BrcmPatchRAM::hciParseResponse(void* response, UInt16 length, void* output, UInt8* outputLength)
{
    HCI_RESPONSE* header = (HCI_RESPONSE*)response;
    IOReturn result = kIOReturnSuccess;

    switch (header->eventCode)
    {
        case HCI_EVENT_COMMAND_COMPLETE:
        {
            HCI_COMMAND_COMPLETE* event = (HCI_COMMAND_COMPLETE*)response;
            
            switch (event->opcode)
            {
                case HCI_OPCODE_READ_VERBOSE_CONFIG:
                    DebugLog("[%04x:%04x]: READ VERBOSE CONFIG complete (status: 0x%02x, length: %d bytes).\n",
                             mVendorId, mProductId, event->status, header->length);
                    
                    mFirmwareVersion = *(UInt16*)(((char*)response) + 10);
                    
                    DebugLog("[%04x:%04x]: Firmware version: v%d.\n",
                             mVendorId, mProductId, mFirmwareVersion + 0x1000);
                    
                    // Device does not require a firmware patch at this time
                    if (mFirmwareVersion > 0)
                        mDeviceState = kUpdateNotNeeded;
                    else
                        mDeviceState = kFirmwareVersion;
                    break;
                case HCI_OPCODE_DOWNLOAD_MINIDRIVER:
                    DebugLog("[%04x:%04x]: DOWNLOAD MINIDRIVER complete (status: 0x%02x, length: %d bytes).\n",
                             mVendorId, mProductId, event->status, header->length);
                    
                    mDeviceState = kMiniDriverComplete;
                    break;
                case HCI_OPCODE_LAUNCH_RAM:
                    //DebugLog("[%04x:%04x]: LAUNCH RAM complete (status: 0x%02x, length: %d bytes).\n",
                    //          mVendorId, mProductId, event->status, header->length);
                    
                    mDeviceState = kInstructionWritten;
                    break;
                case HCI_OPCODE_END_OF_RECORD:
                    DebugLog("[%04x:%04x]: END OF RECORD complete (status: 0x%02x, length: %d bytes).\n",
                             mVendorId, mProductId, event->status, header->length);
                    
                    mDeviceState = kFirmwareWritten;
                    break;
                case HCI_OPCODE_RESET:
                    DebugLog("[%04x:%04x]: RESET complete (status: 0x%02x, length: %d bytes).\n",
                             mVendorId, mProductId, event->status, header->length);
                    
                    mDeviceState = kResetComplete;
                    break;
                default:
                    DebugLog("[%04x:%04x]: Event COMMAND COMPLETE (opcode 0x%04x, status: 0x%02x, length: %d bytes).\n",
                             mVendorId, mProductId, event->opcode, event->status, header->length);
                    break;
            }
            
            if (output && outputLength)
            {
                bzero(output, *outputLength);
                
                // Return the received data
                if (*outputLength >= length)
                {
                    DebugLog("[%04x:%04x]: Returning output data %d bytes.\n", mVendorId, mProductId, length);
                    
                    *outputLength = length;
                    memcpy(output, response, length);
                }
                else
                    // Not enough buffer space for data
                    result = kIOReturnMessageTooLarge;
            }
            break;
        }
        case HCI_EVENT_NUM_COMPLETED_PACKETS:
            DebugLog("[%04x:%04x]: Number of completed packets.\n", mVendorId, mProductId);
            break;
        case HCI_EVENT_CONN_COMPLETE:
            DebugLog("[%04x:%04x]: Connection complete event.\n", mVendorId, mProductId);
            break;
        case HCI_EVENT_DISCONN_COMPLETE:
            DebugLog("[%04x:%04x]: Disconnection complete. event\n", mVendorId, mProductId);
            break;
        case HCI_EVENT_HARDWARE_ERROR:
            DebugLog("[%04x:%04x]: Hardware error\n", mVendorId, mProductId);
            break;
        case HCI_EVENT_MODE_CHANGE:
            DebugLog("[%04x:%04x]: Mode change event.\n", mVendorId, mProductId);
            break;
        case HCI_EVENT_LE_META:
            DebugLog("[%04x:%04x]: Low-Energy meta event.\n", mVendorId, mProductId);
            break;
        default:
            DebugLog("[%04x:%04x]: Unknown event code (0x%02x).\n", mVendorId, mProductId, header->eventCode);
            break;
    }
    
    return result;
}

IOReturn BrcmPatchRAM::bulkWrite(const void* data, UInt16 length)
{
    IOReturn result;
    
    if (IOMemoryDescriptor* buffer = IOMemoryDescriptor::withAddress((void*)data, length, kIODirectionOut))
    {
        if ((result = buffer->prepare(kIODirectionOut)) == kIOReturnSuccess)
        {
            if ((result = mBulkPipe.write(buffer, 0, 0, buffer->getLength(), NULL)) == kIOReturnSuccess)
            {
                //DEBUG_LOG("%s: Wrote %d bytes to bulk pipe.\n", getName(), length);
            }
            else
                AlwaysLog("[%04x:%04x]: Failed to write to bulk pipe (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        }
        else
            AlwaysLog("[%04x:%04x]: Failed to prepare bulk write memory buffer (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        
        if ((result = buffer->complete(kIODirectionOut)) != kIOReturnSuccess)
            AlwaysLog("[%04x:%04x]: Failed to complete bulk write memory buffer (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        
        buffer->release();
    }
    else
    {
        AlwaysLog("[%04x:%04x]: Unable to allocate bulk write buffer.\n", mVendorId, mProductId);
        result = kIOReturnNoMemory;
    }
    
    return result;
}

bool BrcmPatchRAM::performUpgrade()
{
    BrcmFirmwareStore* firmwareStore;
    OSArray* instructions = NULL;
    OSCollectionIterator* iterator = NULL;
    OSData* data;
#ifdef DEBUG
    DeviceState previousState = kUnknown;
#endif

    IOLockLock(mCompletionLock);
    mDeviceState = kInitialize;

    while (true)
    {
#ifdef DEBUG
        if (mDeviceState != kInstructionWrite && mDeviceState != kInstructionWritten)
            DebugLog("[%04x:%04x]: State \"%s\" --> \"%s\".\n", mVendorId, mProductId, getState(previousState), getState(mDeviceState));
        previousState = mDeviceState;
#endif

        // Break out when done
        if (mDeviceState == kUpdateAborted || mDeviceState == kUpdateComplete || mDeviceState == kUpdateNotNeeded)
            break;

        // Note on following switch/case:
        //   use 'break' when a response from io completion callback is expected
        //   use 'continue' when a change of state with no expected response (loop again)

        switch (mDeviceState)
        {
            case kInitialize:
                hciCommand(&HCI_VSC_READ_VERBOSE_CONFIG, sizeof(HCI_VSC_READ_VERBOSE_CONFIG));
                break;

            case kFirmwareVersion:
                // Unable to retrieve firmware store
                if (!(firmwareStore = getFirmwareStore()))
                {
                    mDeviceState = kUpdateAborted;
                    continue;
                }
                instructions = firmwareStore->getFirmware(mVendorId, mProductId, OSDynamicCast(OSString, getProperty(kFirmwareKey)));
                // Unable to retrieve firmware instructions
                if (!instructions)
                {
                    mDeviceState = kUpdateAborted;
                    continue;
                }

                // Initiate firmware upgrade
                hciCommand(&HCI_VSC_DOWNLOAD_MINIDRIVER, sizeof(HCI_VSC_DOWNLOAD_MINIDRIVER));
                break;

            case kMiniDriverComplete:
                // Write firmware data to bulk pipe
                iterator = OSCollectionIterator::withCollection(instructions);
                if (!iterator)
                {
                    mDeviceState = kUpdateAborted;
                    continue;
                }

                // If this IOSleep is not issued, the device is not ready to receive
                // the firmware instructions and we will deadlock due to lack of
                // responses.
                IOSleep(mInitialDelay);

                // Write first instruction to trigger response
                if ((data = OSDynamicCast(OSData, iterator->getNextObject())))
                    bulkWrite(data->getBytesNoCopy(), data->getLength());
                break;

            case kInstructionWrite:
                // should never happen, but would cause a crash
                if (!iterator)
                {
                    mDeviceState = kUpdateAborted;
                    continue;
                }

                if ((data = OSDynamicCast(OSData, iterator->getNextObject())))
                    bulkWrite(data->getBytesNoCopy(), data->getLength());
                else
                    // Firmware data fully written
                    hciCommand(&HCI_VSC_END_OF_RECORD, sizeof(HCI_VSC_END_OF_RECORD));
                break;

            case kInstructionWritten:
                mDeviceState = kInstructionWrite;
                continue;

            case kFirmwareWritten:
                IOSleep(mPreResetDelay);
				if (hciCommand(&HCI_RESET, sizeof(HCI_RESET)) != kIOReturnSuccess)
				{
					DebugLog("HCI_RESET failed, aborting.");
					mDeviceState = kUpdateAborted;
					continue;
				}
                break;

            case kResetComplete:
                resetDevice();
                getDeviceStatus();
                mDeviceState = kUpdateComplete;
                continue;

            case kUnknown:
            case kUpdateNotNeeded:
            case kUpdateComplete:
            case kUpdateAborted:
                DebugLog("Error: kUnkown/kUpdateComplete/kUpdateAborted cases should be unreachable.\n");
                break;
        }

        // queue async read
        if (!continuousRead())
        {
            mDeviceState = kUpdateAborted;
            continue;
        }
        // wait for completion of the async read
        IOLockSleep(mCompletionLock, this, 0);
    }

    IOLockUnlock(mCompletionLock);
    OSSafeReleaseNULL(iterator);

    return mDeviceState == kUpdateComplete || mDeviceState == kUpdateNotNeeded;
}

#ifdef DEBUG
const char* BrcmPatchRAM::getState(DeviceState deviceState)
{
    static const IONamedValue state_values[] = {
        {kUnknown,            "Unknown"              },
        {kInitialize,         "Initialize"           },
        {kFirmwareVersion,    "Firmware version"     },
        {kMiniDriverComplete, "Mini-driver complete" },
        {kInstructionWrite,   "Instruction write"    },
        {kInstructionWritten, "Instruction written"  },
        {kFirmwareWritten,    "Firmware written"     },
        {kResetComplete,      "Reset complete"       },
        {kUpdateComplete,     "Update complete"      },
        {kUpdateNotNeeded,    "Update not needed"    },
        {0,                   NULL                   }
    };
    
    return IOFindNameForValue(deviceState, state_values);
}
#endif //DEBUG

#ifndef kIOUSBClearPipeStallNotRecursive
// from 10.7 SDK
#define kIOUSBClearPipeStallNotRecursive iokit_usb_err(0x48)
#endif

const char* BrcmPatchRAM::stringFromReturn(IOReturn rtn)
{
    static const IONamedValue IOReturn_values[] = {
        {kIOReturnIsoTooOld,          "Isochronous I/O request for distant past"     },
        {kIOReturnIsoTooNew,          "Isochronous I/O request for distant future"   },
        {kIOReturnNotFound,           "Data was not found"                           },
//REVIEW: new error identifiers?
#ifndef TARGET_ELCAPITAN
        {kIOUSBUnknownPipeErr,        "Pipe ref not recognized"                      },
        {kIOUSBTooManyPipesErr,       "Too many pipes"                               },
        {kIOUSBNoAsyncPortErr,        "No async port"                                },
        {kIOUSBNotEnoughPowerErr,     "Not enough power for selected configuration"  },
        {kIOUSBEndpointNotFound,      "Endpoint not found"                           },
        {kIOUSBConfigNotFound,        "Configuration not found"                      },
        {kIOUSBTransactionTimeout,    "Transaction timed out"                        },
        {kIOUSBTransactionReturned,   "Transaction has been returned to the caller"  },
        {kIOUSBPipeStalled,           "Pipe has stalled, error needs to be cleared"  },
        {kIOUSBInterfaceNotFound,     "Interface reference not recognized"           },
        {kIOUSBLowLatencyBufferNotPreviouslyAllocated,
            "Attempted to user land low latency isoc calls w/out calling PrepareBuffer" },
        {kIOUSBLowLatencyFrameListNotPreviouslyAllocated,
            "Attempted to user land low latency isoc calls w/out calling PrepareBuffer" },
        {kIOUSBHighSpeedSplitError,   "Error on hi-speed bus doing split transaction"},
        {kIOUSBSyncRequestOnWLThread, "Synchronous USB request on workloop thread."  },
        {kIOUSBDeviceNotHighSpeed,    "The device is not a high speed device."       },
        {kIOUSBClearPipeStallNotRecursive,
            "IOUSBPipe::ClearPipeStall should not be called rescursively"               },
        {kIOUSBLinkErr,               "USB link error"                               },
        {kIOUSBNotSent2Err,           "Transaction not sent"                         },
        {kIOUSBNotSent1Err,           "Transaction not sent"                         },
        {kIOUSBNotEnoughPipesErr,     "Not enough pipes in interface"                },
        {kIOUSBBufferUnderrunErr,     "Buffer Underrun (Host hardware failure)"      },
        {kIOUSBBufferOverrunErr,      "Buffer Overrun (Host hardware failure"        },
        {kIOUSBReserved2Err,          "Reserved"                                     },
        {kIOUSBReserved1Err,          "Reserved"                                     },
        {kIOUSBWrongPIDErr,           "Pipe stall, Bad or wrong PID"                 },
        {kIOUSBPIDCheckErr,           "Pipe stall, PID CRC error"                    },
        {kIOUSBDataToggleErr,         "Pipe stall, Bad data toggle"                  },
        {kIOUSBBitstufErr,            "Pipe stall, bitstuffing"                      },
        {kIOUSBCRCErr,                "Pipe stall, bad CRC"                          },
#endif
        {0,                           NULL                                           }
    };
    
    const char* result = IOFindNameForValue(rtn, IOReturn_values);
    
    if (result)
        return result;
    
    return super::stringFromReturn(rtn);
}


#ifdef NON_RESIDENT

OSDefineMetaClassAndStructors(BrcmPatchRAMResidency, IOService)

bool BrcmPatchRAMResidency::start(IOService *provider)
{
    DebugLog("BrcmPatchRAMResidency start\n");

    if (!super::start(provider))
        return false;

    registerService();

    return true;
}

#endif
