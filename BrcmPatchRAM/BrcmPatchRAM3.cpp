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
 *
 *  BrcmPatchRAM3.cpp
 *  BrcmPatchRAM3
 *
 *  Created by Laura Müller on 06.10.19.
 *
 */

#if defined(TARGET_CATALINA)

// Silence warning about deprecated USB header files.
#define __IOUSBFAMILY__

#include <IOKit/usb/USB.h>
#include <IOKit/IOCatalogue.h>

#include <libkern/version.h>
#include <libkern/OSKextLib.h>

#include "Common.h"
#include "hci.h"
#include "BrcmPatchRAM.h"

#define kReadBufferSize 0x200

#include <kern/clock.h>
#include <stdarg.h>

#ifndef THREAD_UNINT
#define THREAD_UNINT 0
#endif
#ifndef THREAD_TIMED_OUT
#define THREAD_TIMED_OUT 1
#endif
#ifndef kMillisecondScale
#define kMillisecondScale 1000000
#endif

// Background recovery: delays (ms) before each retry round after a failed upload.
static const UInt32 kRecoveryBackoffMs[] = { 5000, 15000, 45000 };
#define kMaxRecoveryRounds (sizeof(kRecoveryBackoffMs) / sizeof(kRecoveryBackoffMs[0]))
// Upper bound for consecutive forced USB re-enumerations without a healthy device / successful upload.
#define kMaxPortResets 3
static UInt32 sPortResets = 0;
static uint64_t sLastResetMs = 0;  // uptime of the last requested reset; old resets stop counting after kResetForgetMs
static const uint64_t kResetForgetMs = 10 * 60 * 1000;

// Wake check: wait this long after a power-on notification before probing the device (ms).
#define kWakeCheckDelayMs 2500
// Pause between the two probes that must both fail before the port is reset (ms).
#define kWakeProbeGapMs 1000
// How often the wake check is postponed because the device is busy before it gives up.
#define kMaxWakeCheckRetries 5
// Settle time between finishing the upload and letting the native Bluetooth stack see the interfaces (ms).
#define kRevealSettleMs 250
// Controller verification: how long to wait for one Read Local Version reply before sending the command again (ms).
#define kVerifyTryMs 750
// After the controller answered: how long it has to stay silent before it counts as settled (ms), and the
// maximum number of late events that are drained before giving up waiting for silence.
#define kVerifyQuietMs 0   /* a drain phase that aborts a queued read broke the native transport attach on boot */
#define kVerifyMaxDrain 8

static uint64_t uptimeMs()
{
    uint64_t now, ns;
    clock_get_uptime(&now);
    absolutetime_to_nanoseconds(now, &ns);
    return ns / 1000000;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

enum { kMyOffPowerState = 0, kMyOnPowerState = 1 };

static IOPMPowerState myTwoStates[2] =
{
    { kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { kIOPMPowerStateVersion1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0 }
};

/*
 * Examining the log files I discovered that mPreResetDelay is obsolete
 * for the Dell DW1560 because the device implements some kind of
 * handshake mechanism to signal when it has finished processing
 * the downloaded firmware patches and is ready for the HCI reset
 * command by sending a Vendor Specific Event (event code 0xff).
 *
 * The corresponding log entry looks like this:
 *
 * BrcmPatchRAM2: [0a5c:216f]: Vendor specific event. Ready to reset device.
 *
 * hskSupport contains a list of all devices which have been verified to
 * support this mechanism. It must be terminated with a zero entry.
 *
 * I assume that more devices (all?) also work this way so that
 * they should be added to the list. It might be possible that this
 * is a common feature among Broadcom BT controllers making the list
 * obsolete, but for now, we still need it.
 */
static DeviceHskSupport hskSupport[] =
{
    { 0x0a5c, 0x216f },
    { 0x0a5c, 0x21ec },
    { 0x0a5c, 0x6412 },
    { 0x0a5c, 0x6414 },
    { 0x0489, 0xe07a },
    { 0x0,    0x0    }
};

OSDefineMetaClassAndStructors(BrcmPatchRAM3, IOService)

bool BrcmPatchRAM::init(OSDictionary *properties)
{
    bool result;
    
    DebugLog("init\n");

    result = super::init(properties);
    
    if (result) {
        UInt32 delay;
        
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
        
        mPreResetDelay = 250;
        
        if (OSNumber* preResetDelay = OSDynamicCast(OSNumber, getProperty("PreResetDelay")))
            mPreResetDelay = preResetDelay->unsigned32BitValue();
        
        if (PE_parse_boot_argn("bpr_preresetdelay", &delay, sizeof delay))
            mPreResetDelay = delay;

        // Number of upload attempts per start (1..10).
        mMaxAttempts = 3;
        if (PE_parse_boot_argn("bpr_attempts", &delay, sizeof delay))
            mMaxAttempts = delay < 1 ? 1 : (delay > 10 ? 10 : delay);

        // How long to wait for a single device reply before giving up on the attempt (ms).
        mStepTimeout = 3000;
        if (PE_parse_boot_argn("bpr_steptimeout", &delay, sizeof delay))
            mStepTimeout = delay < 500 ? 500 : (delay > 60000 ? 60000 : delay);

        // Last resort: reset the USB port if all attempts failed. Off by default.
        mAllowPortReset = false;
        if (PE_parse_boot_argn("bpr_portreset", &delay, sizeof delay))
            mAllowPortReset = delay != 0;

        // Keep the USB interfaces unregistered (invisible to bluetoothd) while firmware is uploaded.
        mHideInterfaces = true;
        if (PE_parse_boot_argn("bpr_hide", &delay, sizeof delay))
            mHideInterfaces = delay != 0;

        // Check that the device still answers after wake and re-enumerate it if not.
        mWakeProbe = true;
        if (PE_parse_boot_argn("bpr_wakeprobe", &delay, sizeof delay))
            mWakeProbe = delay != 0;

        // Testing only: make the wake probe always fail so the recovery path gets exercised.
        mTestProbeFail = false;
        if (PE_parse_boot_argn("bpr_testprobefail", &delay, sizeof delay))
            mTestProbeFail = delay != 0;

        // Ask the controller for its version (and discard stray events) before the native stack may see it.
        mVerifyHCI = true;
        if (PE_parse_boot_argn("bpr_verify", &delay, sizeof delay))
            mVerifyHCI = delay != 0;

        // Total time in ms the controller gets to answer that request (1000..30000).
        mVerifyTimeout = 6000;
        if (PE_parse_boot_argn("bpr_verifytimeout", &delay, sizeof delay))
            mVerifyTimeout = delay < 1000 ? 1000 : (delay > 30000 ? 30000 : delay);
        mVerifyQuiet = kVerifyQuietMs;
        if (PE_parse_boot_argn("bpr_verifyquiet", &delay, sizeof delay))
            mVerifyQuiet = delay > 2000 ? 2000 : delay;   // 0 = no drain phase, no read left to abort
    }
    return result;
}

void BrcmPatchRAM::free()
{
    DebugLog("free\n");
    
    super::free();
}

IOService* BrcmPatchRAM::probe(IOService *provider, SInt32 *probeScore)
{
    BrcmFirmwareStore *firmwareStore;
    OSString *firmwareKey;

    DebugLog("probe\n");
    
    AlwaysLog("Version %s starting on OS X Darwin %d.%d.\n", OSKextGetCurrentVersionString(), version_major, version_minor);
    
    /*
     * Preference towards starting BrcmPatchRAM3.kext when BrcmPatchRAM2.kext,
     * or BrcmPatchRAM.kext also exist.
     */
    *probeScore = 3000;
    
    // BrcmPatchRAM.kext, if installed on pre 10.11 fails immediately
    if (version_major < 15) {
        AlwaysLog("Aborting -- BrcmPatchRAM3.kext should not be installed pre 10.11.  Use BrcmPatchRAM.kext instead.\n");
        return NULL;
    }
    
    mDevice.setDevice(provider);
    
    if (!mDevice.getValidatedDevice()) {
        AlwaysLog("Provider type is incorrect (not IOUSBDevice or IOUSBHostDevice)\n");
        return NULL;
    }
    if (OSString* displayName = OSDynamicCast(OSString, getProperty(kDisplayName)))
        provider->setProperty(kUSBProductString, displayName);
    
    mVendorId = mDevice.getVendorID();
    mProductId = mDevice.getProductID();
    
    // Check if device supports handshake.
    int handshake;
    if (PE_parse_boot_argn("bpr_handshake", &handshake, sizeof handshake))
        mSupportsHandshake = handshake != 0;
    else
        mSupportsHandshake = supportsHandshake(mVendorId, mProductId);

    DebugLog("Device %s handshake.\n", mSupportsHandshake ? "supports" : "doesn't support");
    
    /* Get firmware for device. */
    firmwareKey = OSDynamicCast(OSString, getProperty(kFirmwareKey));
    
    if (firmwareKey) {
        firmwareStore = getFirmwareStore();
        
        if (firmwareStore)
            firmwareStore->getFirmware(mVendorId, mProductId, firmwareKey);
    }
    /* Release device again as probe() shouldn't alter it's state. */
    mDevice.setDevice(NULL);
    
    return super::probe(provider, probeScore);
}

bool BrcmPatchRAM::start(IOService *provider)
{
    uint64_t start_time, end_time, nano_secs;
    IOReturn result;
    bool success = false;
    
    DebugLog("start\n");
    
    clock_get_uptime(&start_time);

    if (!super::start(provider))
        goto done;

    /*
     * Register for power state notifications as it seems to cause
     * a re-probe in case start fails to upload firmware due to
     * unexpected power state transitions. Without it, the driver
     * sometimes fails to upload firmware on boot but will succeed
     * later on wakeup.
     */
    PMinit();
    registerPowerDriver(this, myTwoStates, 2);
    provider->joinPMtree(this);
    makeUsable();
    
    mCompletionLock = IOLockAlloc();
    mUploadLock = IOLockAlloc();

    if (!mCompletionLock || !mUploadLock)
        goto error2;

    /*
     * Setup and prepare read buffer now as it can be reused and
     * it would be inefficient to call prepare() over and over again.
     */
    mReadBuffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionIn, kReadBufferSize);
    
    if (!mReadBuffer) {
        AlwaysLog("[%04x:%04x]: Failed to allocate read buffer.\n", mVendorId, mProductId);
        goto error2;
    }
    if ((result = mReadBuffer->prepare(kIODirectionIn)) != kIOReturnSuccess) {
        AlwaysLog("[%04x:%04x]: Failed to prepare read buffer (0x%08x)\n", mVendorId, mProductId, result);
        goto error3;
    }
    mInterruptCompletion.owner = this;
    mInterruptCompletion.action = readCompletion;
    mInterruptCompletion.parameter = NULL;

    /* Reset the device to put it in a defined state. */
    mDevice.setDevice(provider);

    /*
     * Own work loop + timer used for background recovery after a failed
     * upload. Failing to create them is not fatal, recovery is just disabled.
     */
    mShuttingDown = false;
    mResetIssued = false;
    mRecoveryRound = 0;
    mRecoveryLoop = IOWorkLoop::workLoop();
    if (mRecoveryLoop) {
        mRecoveryTimer = IOTimerEventSource::timerEventSource(this,
            OSMemberFunctionCast(IOTimerEventSource::Action, this, &BrcmPatchRAM::recoveryTimerFired));
        if (!mRecoveryTimer || mRecoveryLoop->addEventSource(mRecoveryTimer) != kIOReturnSuccess) {
            AlwaysLog("[%04x:%04x]: Unable to set up recovery timer, background recovery disabled.\n", mVendorId, mProductId);
            OSSafeReleaseNULL(mRecoveryTimer);
        }

        mWakeTimer = IOTimerEventSource::timerEventSource(this,
            OSMemberFunctionCast(IOTimerEventSource::Action, this, &BrcmPatchRAM::wakeCheckFired));
        if (!mWakeTimer || mRecoveryLoop->addEventSource(mWakeTimer) != kIOReturnSuccess) {
            AlwaysLog("[%04x:%04x]: Unable to set up wake check timer, wake check disabled.\n", mVendorId, mProductId);
            OSSafeReleaseNULL(mWakeTimer);
        }

        if (!mRecoveryTimer && !mWakeTimer)
            OSSafeReleaseNULL(mRecoveryLoop);
    }

    publishStatus("BPR-Config", "attempts=%u steptimeout=%u hide=%d wakeprobe=%d portreset=%d testprobefail=%d verify=%d verifytimeout=%u verifyquiet=%u",
                  (unsigned)mMaxAttempts, (unsigned)mStepTimeout, mHideInterfaces, mWakeProbe, mAllowPortReset, mTestProbeFail,
                  mVerifyHCI, (unsigned)mVerifyTimeout, (unsigned)mVerifyQuiet);

    IOLockLock(mUploadLock);
    mUploadOk = uploadWithRetries();
    IOLockUnlock(mUploadLock);

    if (!mUploadOk) {
        if (mResetIssued) {
            AlwaysLog("[%04x:%04x]: Firmware upload failed, USB re-enumeration requested.\n", mVendorId, mProductId);
        } else {
            AlwaysLog("[%04x:%04x]: Firmware upload failed, will retry in background in %u ms.\n",
                      mVendorId, mProductId, kRecoveryBackoffMs[0]);
            scheduleRecovery(kRecoveryBackoffMs[0]);

            // Without a recovery timer nobody would ever reveal the interfaces again.
            if (!mRecoveryTimer)
                revealInterfaces();
        }
    }

    /*
     * Self test (bpr_testprobefail=1): a driver instance that was just started never
     * sees a power ON callback after its upload, so the wake probe would not run at all.
     * Schedule it once here so the whole probe -> reset path can be exercised on demand.
     * The static reset counter bounds this to kMaxPortResets restarts in a row.
     */
    if (mUploadOk && mTestProbeFail && mWakeTimer) {
        AlwaysLog("[%04x:%04x]: Self test: wake probe scheduled in %u ms.\n", mVendorId, mProductId, 8000);
        publishStatus("BPR-LastSelfTest", "probe scheduled at uptime %llu ms", uptimeMs());
        mWakeCheckRetries = 0;
        mWakeTimer->setTimeoutMS(8000);
    }

    success = true;
    goto done;
    
    /*
     * error handling
     *
     * In case start() fails after super::start() has already been
     * called, it's not enough to free allocated resources but we also
     * have to call PMstop() and super::stop() in order to avoid
     * memory leaks as they would never be called in such a situation
     * if we forget to do so.
     */
error3:
    OSSafeReleaseNULL(mReadBuffer);

error2:
    if (mCompletionLock) {
        IOLockFree(mCompletionLock);
        mCompletionLock = NULL;
    }
    if (mUploadLock) {
        IOLockFree(mUploadLock);
        mUploadLock = NULL;
    }
    
error1:
    PMstop();
    super::stop(provider);
    
done:
    clock_get_uptime(&end_time);
    absolutetime_to_nanoseconds(end_time - start_time, &nano_secs);
    uint64_t milli_secs = nano_secs / 1000000;
    AlwaysLog("Processing time %llu.%llu seconds.\n", milli_secs / 1000, milli_secs % 1000);

    return success;
}

void BrcmPatchRAM::stop(IOService* provider)
{
    DebugLog("stop\n");

    mShuttingDown = true;
    PMstop();

    /*
     * Tear down background recovery first. removeEventSource() waits for a
     * running timer action (possibly an upload in progress), which fails
     * quickly once the device is gone.
     */
    if (mRecoveryTimer) {
        mRecoveryTimer->cancelTimeout();
        if (mRecoveryLoop)
            mRecoveryLoop->removeEventSource(mRecoveryTimer);
        OSSafeReleaseNULL(mRecoveryTimer);
    }
    if (mWakeTimer) {
        mWakeTimer->cancelTimeout();
        if (mRecoveryLoop)
            mRecoveryLoop->removeEventSource(mWakeTimer);
        OSSafeReleaseNULL(mWakeTimer);
    }
    OSSafeReleaseNULL(mRecoveryLoop);

    // Never leave the Bluetooth interfaces hidden when going away.
    revealInterfaces();

    OSSafeReleaseNULL(mFirmwareStore);

    if (mReadBuffer) {
        mReadBuffer->complete(kIODirectionIn);

        mInterruptCompletion.owner = NULL;
        mInterruptCompletion.action = NULL;

        OSSafeReleaseNULL(mReadBuffer);
    }
    if (mCompletionLock) {
        IOLockFree(mCompletionLock);
        mCompletionLock = NULL;
    }
    if (mUploadLock) {
        IOLockFree(mUploadLock);
        mUploadLock = NULL;
    }

    /* Release device. */
    mDevice.setDevice(NULL);
    
    super::stop(provider);
}

/*
 * As we registered for power state notifications we have to supply a
 * handler, even though it's a dummy implementation.
 */
IOReturn BrcmPatchRAM::setPowerState(unsigned long which, IOService *whom)
{
    AlwaysLog("[%04x:%04x]: power state %s (uptime %llu ms, firmware upload %s)\n",
              mVendorId, mProductId,
              which == kMyOnPowerState ? "ON" : "OFF",
              uptimeMs(),
              mUploadOk ? "ok" : "not ok");

    publishStatus("BPR-LastPower", "%s at uptime %llu ms", which == kMyOnPowerState ? "ON" : "OFF", uptimeMs());

    if (which == kMyOnPowerState) {
        /*
         * Never touch the device here, this runs on the power management thread.
         * Only schedule work on our own work loop:
         *  - upload failed earlier: start a fresh round of background recovery
         *  - upload was fine: check a bit later that the device still answers
         *    (it is not re-enumerated on every wake, and sometimes it is dead
         *    after resume without the USB stack noticing)
         */
        if (!mResetIssued && !mShuttingDown) {
            if (!mUploadOk) {
                mRecoveryRound = 0;
                scheduleRecovery(3000);
            } else if (mWakeProbe && mWakeTimer) {
                mWakeCheckRetries = 0;
                mWakeTimer->setTimeoutMS(kWakeCheckDelayMs);
            }
        }
    }
    return IOPMAckImplied;
}

void BrcmPatchRAM::publishStatus(const char* key, const char* format, ...)
{
    // Readable without root: ioreg -l -w0 | grep BPR-
    char buffer[160];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    setProperty(key, buffer);
}

/*
 * Register the interfaces that were created hidden (see setConfiguration), so that
 * the native Bluetooth stack can see the device. Must happen on every exit path
 * of an upload, otherwise Bluetooth would never come up.
 */
void BrcmPatchRAM::revealInterfaces()
{
    if (!mInterfacesHidden)
        return;

    mInterfacesHidden = false;

    if (mDevice.getValidatedDevice()) {
        IOSleep(kRevealSettleMs);
        mDevice.registerInterfaces();
        publishStatus("BPR-LastReveal", "interfaces registered at uptime %llu ms", uptimeMs());
        AlwaysLog("[%04x:%04x]: Interfaces registered for matching.\n", mVendorId, mProductId);
    }
}

/*
 * Passive health check: a standard USB GET_STATUS on the default control endpoint.
 * It does not touch the Bluetooth (HCI) state, so it is safe while Apple's stack
 * is using the controller. Returns false only if the device is clearly dead;
 * anything inconclusive counts as alive.
 */
bool BrcmPatchRAM::probeDeviceAlive()
{
    if (mTestProbeFail) {
        AlwaysLog("[%04x:%04x]: Wake probe: simulated failure (bpr_testprobefail).\n", mVendorId, mProductId);
        return false;
    }

    IOService* device = mDevice.getValidatedDevice();

    // A device that is already being torn down (normal re-enumeration after wake) is not "dead".
    if (!device || device->isInactive())
        return true;

    if (!mDevice.open(this)) {
        publishStatus("BPR-LastProbe", "inconclusive (could not open device) at uptime %llu ms", uptimeMs());
        return true;
    }

    USBStatus status = 0;
    IOReturn result = mDevice.getDeviceStatus(this, &status);
    mDevice.close(this);

    switch (result) {
        case kIOReturnSuccess:
        // the device is going away or someone else is using it: nothing we can conclude
        case kIOReturnNoDevice:
        case kIOReturnNotAttached:
        case kIOReturnOffline:
        case kIOReturnAborted:
        case kIOReturnBusy:
        case kIOReturnExclusiveAccess:
        case kIOReturnNotPermitted:
            return true;

        default:
            // e.g. kIOReturnNotResponding / timeouts / transaction errors: the chip is not answering
            AlwaysLog("[%04x:%04x]: Wake probe: GET_STATUS failed (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
            return false;
    }
}

void BrcmPatchRAM::wakeCheckFired(IOTimerEventSource*)
{
    if (mShuttingDown || mResetIssued || !mUploadOk || !mWakeProbe || !mUploadLock)
        return;

    // an upload / recovery round is running, check again shortly
    if (!IOLockTryLock(mUploadLock)) {
        if (mWakeCheckRetries++ < kMaxWakeCheckRetries && mWakeTimer)
            mWakeTimer->setTimeoutMS(1000);
        return;
    }

    bool alive = probeDeviceAlive();

    if (!alive) {
        IOSleep(kWakeProbeGapMs);
        alive = probeDeviceAlive();
    }
    IOLockUnlock(mUploadLock);

    if (alive) {
        sPortResets = 0;
        publishStatus("BPR-LastProbe", "alive at uptime %llu ms", uptimeMs());
        DebugLog("wake probe ok\n");
        return;
    }

    publishStatus("BPR-LastProbe", "DEAD at uptime %llu ms", uptimeMs());
    AlwaysLog("[%04x:%04x]: Device does not answer after wake.\n", mVendorId, mProductId);
    requestReset("device dead after wake");
}

/*
 * Ask the USB stack to reset the port and re-enumerate the device. The driver is
 * restarted from scratch afterwards (including the firmware upload). Runs in a
 * separate thread, see USBDeviceShim::reEnumerateAsync.
 */
void BrcmPatchRAM::requestReset(const char* reason)
{
    IOService* device = mDevice.getValidatedDevice();

    // Never reset on behalf of a device object that is already gone: the port may hold a new device by now.
    if (mShuttingDown || mResetIssued || !device || device->isInactive())
        return;

    // A successful upload does not clear the counter (a reset loop uploads fine every time).
    // Only a passing wake probe or enough time since the last reset does.
    if (sPortResets && uptimeMs() - sLastResetMs > kResetForgetMs)
        sPortResets = 0;

    if (sPortResets >= kMaxPortResets) {
        AlwaysLog("[%04x:%04x]: Not resetting the USB port (%s): %u resets in a row without success.\n",
                  mVendorId, mProductId, reason, sPortResets);
        publishStatus("BPR-LastAction", "reset refused (%s), limit reached, uptime %llu ms", reason, uptimeMs());
        return;
    }

    sPortResets++;
    sLastResetMs = uptimeMs();
    mResetIssued = true;
    AlwaysLog("[%04x:%04x]: Resetting the USB port (%s), %u/%u.\n", mVendorId, mProductId, reason, sPortResets, kMaxPortResets);
    publishStatus("BPR-LastAction", "reset requested (%s) at uptime %llu ms", reason, uptimeMs());

    IOReturn result = mDevice.reEnumerateAsync();
    if (result != kIOReturnSuccess) {
        mResetIssued = false;
        AlwaysLog("[%04x:%04x]: Could not start USB re-enumeration (0x%08x).\n", mVendorId, mProductId, result);
    }
}

void BrcmPatchRAM::scheduleRecovery(UInt32 delayMs)
{
    if (mShuttingDown || !mRecoveryTimer)
        return;
    mRecoveryTimer->setTimeoutMS(delayMs);
}

void BrcmPatchRAM::recoveryTimerFired(IOTimerEventSource*)
{
    if (mShuttingDown || mUploadOk || mResetIssued || !mUploadLock)
        return;

    // start() (or an earlier round) may still be uploading, try again shortly.
    if (!IOLockTryLock(mUploadLock)) {
        scheduleRecovery(1000);
        return;
    }

    if (!mDevice.getValidatedDevice() || !mCompletionLock) {
        IOLockUnlock(mUploadLock);
        return;
    }

    mRecoveryRound++;
    AlwaysLog("[%04x:%04x]: Background recovery round %u/%u (uptime %llu ms).\n",
              mVendorId, mProductId, mRecoveryRound, (unsigned)kMaxRecoveryRounds, uptimeMs());

    bool ok = uploadWithRetries();
    mUploadOk = ok;
    IOLockUnlock(mUploadLock);

    if (ok) {
        AlwaysLog("[%04x:%04x]: Background recovery succeeded.\n", mVendorId, mProductId);
    } else if (!mResetIssued && !mShuttingDown) {
        if (mRecoveryRound < kMaxRecoveryRounds) {
            UInt32 next = kRecoveryBackoffMs[mRecoveryRound];
            AlwaysLog("[%04x:%04x]: Background recovery failed, next round in %u ms.\n", mVendorId, mProductId, next);
            scheduleRecovery(next);
        } else {
            AlwaysLog("[%04x:%04x]: Background recovery gave up; will try again on next wake.\n", mVendorId, mProductId);
            publishStatus("BPR-LastUpload", "recovery gave up at uptime %llu ms", uptimeMs());
            // Fail open: let the native stack see the device, it may still work (or at least report an error).
            revealInterfaces();
        }
    }
}

/*
 * Sleep on mCompletionLock (held by the caller) until *flag is set or ms have passed.
 * Returns whether the flag was set.
 */
static bool waitForFlag(IOLock* lock, volatile bool* flag, void* event, UInt32 ms)
{
    uint64_t deadline;
    clock_interval_to_deadline(ms, kMillisecondScale, &deadline);
    while (!*flag) {
        if (IOLockSleepDeadline(lock, event, deadline, THREAD_UNINT) == THREAD_TIMED_OUT && !*flag)
            return false;
    }
    return true;
}

/*
 * Called from readCompletion (mCompletionLock held) while the controller is being verified.
 * Only the Read Local Version reply matters, everything else is counted and discarded.
 */
void BrcmPatchRAM::verifyParse(void* response, UInt16 length)
{
    HCI_RESPONSE* header = (HCI_RESPONSE*)response;
    UInt8* bytes = (UInt8*)response;
    UInt16 opcode = 0;

    if (length >= sizeof(HCI_COMMAND_COMPLETE) && header->eventCode == HCI_EVENT_COMMAND_COMPLETE) {
        HCI_COMMAND_COMPLETE* event = (HCI_COMMAND_COMPLETE*)response;
        opcode = event->opcode;

        // A second reply (the request is repeated while the chip is silent) counts as stray.
        if (opcode == HCI_OPCODE_READ_LOCAL_VERSION && !mVerifyGotVersion) {
            mVerifyOpStatus = event->status;
            // status, HCI version, HCI revision (2), LMP version, manufacturer (2), LMP subversion (2)
            if (event->status == 0 && length >= 14) {
                mVerifyHciVersion = bytes[6];
                mVerifyHciRevision = (UInt16)(bytes[7] | (bytes[8] << 8));
                mVerifyLmpVersion = bytes[9];
                mVerifyManufacturer = (UInt16)(bytes[10] | (bytes[11] << 8));
                mVerifyLmpSubversion = (UInt16)(bytes[12] | (bytes[13] << 8));
            }
            mVerifyGotVersion = true;
            return;
        }
    }

    mVerifyLastStrayOpcode = opcode;
    mVerifyLastStrayEvent = length >= 1 ? header->eventCode : 0;
    mVerifyStray++;
    AlwaysLog("[%04x:%04x]: Verify: discarding stray event 0x%02x (opcode 0x%04x, %d bytes).\n",
              mVendorId, mProductId, mVerifyLastStrayEvent, opcode, length);
}

/*
 * Abort the interrupt read that is still queued and wait for its completion.
 * The caller holds mCompletionLock and must have a read pending (queued, not completed).
 */
void BrcmPatchRAM::abortPendingRead()
{
    // The completion handler takes the lock, so drop it meanwhile.
    IOLockUnlock(mCompletionLock);
    mInterruptPipe.abort();
    IOLockLock(mCompletionLock);

    if (!waitForFlag(mCompletionLock, &mReadDone, this, 2000))
        AlwaysLog("[%04x:%04x]: Verify: pending read did not complete after abort.\n", mVendorId, mProductId);
}

/*
 * Make sure the controller really runs its firmware and is quiet before the native Bluetooth
 * stack may see it. Runs after a successful upload, while our interface is open and the USB
 * interfaces are still hidden from bluetoothd.
 *
 *  1. Keep one interrupt read queued so that nothing the chip emits stays behind for bluetoothd.
 *  2. Send HCI Read Local Version and repeat it every kVerifyTryMs: a command that reaches the
 *     chip while it is still rebooting its firmware is simply lost.
 *  3. Discard every event that is not the reply (e.g. an unsolicited Reset completion).
 *  4. Optionally (bpr_verifyquiet > 0) require that much silence after the reply. Off by default:
 *     it leaves a read queued that must be aborted, and then the native transport did not attach.
 *
 * Returns false if the chip stays silent for mVerifyTimeout ms or the pipe breaks.
 */
bool BrcmPatchRAM::verifyControllerReady()
{
    // Without a read buffer there is nothing to verify with, do not hold the handover back for that.
    if (!mCompletionLock || !mReadBuffer)
        return true;

    const uint64_t start = uptimeMs();
    UInt32 sent = 0;
    UInt32 drained = 0;
    bool pending = false;   // a read is queued and has not completed yet
    bool ok = false;

    IOLockLock(mCompletionLock);
    mVerifyMode = true;
    mVerifyGotVersion = false;
    mVerifyStray = 0;
    mVerifyLastStrayOpcode = 0;
    mVerifyLastStrayEvent = 0;
    mVerifyReadStatus = kIOReturnSuccess;
    mVerifyOpStatus = 0;
    mReadDone = false;

    pending = continuousRead();

    while (pending && !mShuttingDown && !mVerifyGotVersion && uptimeMs() - start < mVerifyTimeout) {
        sent++;

        IOLockUnlock(mCompletionLock);
        IOReturn result = hciCommand(HCI_LOCAL_VERSION, sizeof(HCI_LOCAL_VERSION));
        IOLockLock(mCompletionLock);

        if (result != kIOReturnSuccess)
            break;

        // Collect events until the reply is in or this try has run out; the read stays queued across tries.
        const uint64_t tryEnd = uptimeMs() + kVerifyTryMs;
        while (pending && !mVerifyGotVersion) {
            const uint64_t now = uptimeMs();
            if (now >= tryEnd || !waitForFlag(mCompletionLock, &mReadDone, this, (UInt32)(tryEnd - now)))
                break;

            // A read completed: an event, or a failure.
            pending = false;
            IOReturn readStatus = mVerifyReadStatus;
            if (readStatus != kIOReturnSuccess) {
                // A stalled pipe or a timeout can be retried, anything else (device gone, aborted) cannot.
                if (readStatus != kIOReturnNotResponding && readStatus != kIOUSBTransactionTimeout)
                    break;
                mVerifyReadStatus = kIOReturnSuccess;
            }
            if (!mVerifyGotVersion) {
                mReadDone = false;
                pending = continuousRead();
            }
        }
    }

    if (mVerifyGotVersion && mVerifyReadStatus == kIOReturnSuccess && mVerifyQuiet == 0) {
        ok = true;      // reply seen, nothing queued: leave the pipe exactly as the upload left it
    } else if (mVerifyGotVersion && mVerifyReadStatus == kIOReturnSuccess) {
        // The chip answers. Wait until it has also stopped talking, any late event is eaten here.
        while (!mShuttingDown) {
            if (!pending) {
                mReadDone = false;
                if (!(pending = continuousRead()))
                    break;
            }
            if (!waitForFlag(mCompletionLock, &mReadDone, this, mVerifyQuiet))
                break;                      // quiet: settled (the read is still queued, aborted below)

            pending = false;
            IOReturn readStatus = mVerifyReadStatus;
            if (readStatus == kIOReturnNotResponding || readStatus == kIOUSBTransactionTimeout)
                mVerifyReadStatus = kIOReturnSuccess;
            else if (readStatus != kIOReturnSuccess)
                break;
            if (++drained >= kVerifyMaxDrain) {
                AlwaysLog("[%04x:%04x]: Verify: controller keeps sending events, giving up waiting for silence.\n", mVendorId, mProductId);
                break;
            }
        }
        ok = mVerifyReadStatus == kIOReturnSuccess;
    }

    const IOReturn finalStatus = mVerifyReadStatus;
    if (pending)
        abortPendingRead();
    mVerifyMode = false;
    IOLockUnlock(mCompletionLock);

    const uint64_t took = uptimeMs() - start;

    if (ok) {
        AlwaysLog("[%04x:%04x]: Controller verified in %llu ms (%u request(s), %u stray event(s)): HCI v%u rev 0x%04x, LMP v%u sub 0x%04x, manufacturer 0x%04x.\n",
                  mVendorId, mProductId, took, (unsigned)sent, (unsigned)mVerifyStray,
                  mVerifyHciVersion, mVerifyHciRevision, mVerifyLmpVersion, mVerifyLmpSubversion, mVerifyManufacturer);
        publishStatus("BPR-LastVerify", "ok in %llu ms, tries=%u stray=%u hci=%u/0x%04x lmpsub=0x%04x uptime %llu ms",
                      took, (unsigned)sent, (unsigned)mVerifyStray, mVerifyHciVersion, mVerifyHciRevision, mVerifyLmpSubversion, uptimeMs());
    } else {
        AlwaysLog("[%04x:%04x]: Controller did not answer Read Local Version (%llu ms, %u request(s), %u stray event(s), last read status 0x%08x).\n",
                  mVendorId, mProductId, took, (unsigned)sent, (unsigned)mVerifyStray, finalStatus);
        publishStatus("BPR-LastVerify", "FAILED after %llu ms, tries=%u stray=%u readstatus=0x%08x uptime %llu ms",
                      took, (unsigned)sent, (unsigned)mVerifyStray, finalStatus, uptimeMs());
    }
    return ok;
}

bool BrcmPatchRAM::uploadWithRetries()
{
    for (UInt32 attempt = 1; attempt <= mMaxAttempts; attempt++) {
        if (mShuttingDown)
            return false;

        uint64_t t0 = uptimeMs();

        if (uploadFirmwareOnce()) {
            if (attempt > 1)
                AlwaysLog("[%04x:%04x]: Upload succeeded on attempt %u/%u.\n", mVendorId, mProductId, attempt, mMaxAttempts);
            publishStatus("BPR-LastUpload", "ok, attempt %u/%u, firmware v%d, uptime %llu ms",
                          (unsigned)attempt, (unsigned)mMaxAttempts, (int)mFirmwareVersion + 0x1000, uptimeMs());
            // Now that the chip is ready, let the native Bluetooth stack take over.
            revealInterfaces();
            return true;
        }

        AlwaysLog("[%04x:%04x]: Upload attempt %u/%u failed after %llu ms (state %d).\n",
                  mVendorId, mProductId, attempt, mMaxAttempts, uptimeMs() - t0, (int)mDeviceState);

        if (attempt < mMaxAttempts)
            IOSleep(500 * attempt + mPostResetDelay);
    }

    publishStatus("BPR-LastUpload", "FAILED after %u attempts, uptime %llu ms", (unsigned)mMaxAttempts, uptimeMs());

    /*
     * Last resort. The reset runs in its own thread, so it is safe to request it from here.
     * A controller that took the firmware but then stays silent is reset even without
     * bpr_portreset: handing it to bluetoothd would only start a crash loop. Both are capped
     * by the shared reset limit.
     */
    if (!mShuttingDown) {
        if (mVerifyFailedLast)
            requestReset("controller not answering after upload");
        else if (mAllowPortReset)
            requestReset("all upload attempts failed");
    }
    return false;
}

bool BrcmPatchRAM::uploadFirmwareOnce()
{
    bool ok = false;

    mVerifyFailedLast = false;

    // signal to timer that firmware already loaded
    mDevice.setProperty(kFirmwareLoaded, true);

    // don't bother with devices that have no firmware
    if (!getProperty(kFirmwareKey))
        return true;

    if (!mDevice.open(this)) {
        AlwaysLog("[%04x:%04x]: uploadFirmware could not open the device!\n", mVendorId, mProductId);
        return false;
    }
    
    // Print out additional device information
    printDeviceInfo();
    
    // Set device configuration to composite configuration index 0
    // Obtain first interface
    if (setConfiguration(0) && findInterface(&mInterface) && mInterface.open(this)) {
        DebugLog("set configuration and interface opened\n");
        mInterface.findPipe(&mInterruptPipe, kUSBInterrupt, kUSBIn);
        mInterface.findPipe(&mBulkPipe, kUSBBulk, kUSBOut);
        
        if (mInterruptPipe.getValidatedPipe() && mBulkPipe.getValidatedPipe()) {
            DebugLog("got pipes\n");
            
            if (performUpgrade()) {
                ok = true;
                if (mDeviceState == kUpdateComplete) {
                    AlwaysLog("[%04x:%04x]: Firmware upgrade completed successfully.\n", mVendorId, mProductId);
                } else {
                    AlwaysLog("[%04x:%04x]: Firmware upgrade not needed.\n", mVendorId, mProductId);
                }

                // The upload is done, but the chip may still be rebooting into the new firmware.
                // Check that it answers and is quiet while bluetoothd cannot see it yet.
                if (mVerifyHCI && !mShuttingDown && !verifyControllerReady()) {
                    ok = false;
                    mVerifyFailedLast = true;
                    AlwaysLog("[%04x:%04x]: Controller not ready after the upload, treating this attempt as failed.\n", mVendorId, mProductId);
                }
            } else {
                AlwaysLog("[%04x:%04x]: Firmware upgrade failed.\n", mVendorId, mProductId);
            }
        } else {
            AlwaysLog("[%04x:%04x]: Interrupt/bulk pipes not available.\n", mVendorId, mProductId);
        }
        mInterface.close(this);
    } else {
        AlwaysLog("[%04x:%04x]: Could not configure device or open its interface.\n", mVendorId, mProductId);

        /*
         * If this happened while the interfaces are hidden, hiding may not work on this
         * system. Fall back to the classic behaviour (interfaces visible during the
         * upload) for all further attempts.
         */
        if (mInterfacesHidden) {
            AlwaysLog("[%04x:%04x]: Hidden interfaces are not usable, falling back to visible interfaces.\n", mVendorId, mProductId);
            publishStatus("BPR-Hide", "unusable, fell back to visible interfaces at uptime %llu ms", uptimeMs());
            mHideInterfaces = false;
            mInterface.setInterface(NULL);
            mDevice.close(this);
            revealInterfaces();
            if (!mDevice.open(this))
                return false;
        }
    }

    // cleanup
    if (mInterruptPipe.getValidatedPipe()) {
        mInterruptPipe.abort();
        mInterruptPipe.setPipe(NULL);
    }
    if (mBulkPipe.getValidatedPipe()) {
        mBulkPipe.abort();
        mBulkPipe.setPipe(NULL);
    }
    mInterface.setInterface(NULL);
    mDevice.close(this);

    return ok;
}

BrcmFirmwareStore* BrcmPatchRAM::getFirmwareStore()
{
    if (!mFirmwareStore) {
        // check to see if it already loaded
        IOService* tmpStore = waitForMatchingService(serviceMatching(kBrcmFirmwareStoreService), 0);
        mFirmwareStore = OSDynamicCast(BrcmFirmwareStore, tmpStore);
        if (!mFirmwareStore)
        {
            // not loaded, so wait...
            if (tmpStore)
                tmpStore->release();
            tmpStore = waitForMatchingService(serviceMatching(kBrcmFirmwareStoreService), 2000UL*1000UL*1000UL);
            mFirmwareStore = OSDynamicCast(BrcmFirmwareStore, tmpStore);
            if (!mFirmwareStore && tmpStore)
                tmpStore->release();
        }
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
    
    if ((result = mDevice.getDeviceStatus(this, &status)) != kIOReturnSuccess) {
        AlwaysLog("[%04x:%04x]: Unable to get device status (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        return 0;
    } else {
        DebugLog("[%04x:%04x]: Device status 0x%08x.\n", mVendorId, mProductId, (int)status);
    }
    return (int)status;
}

bool BrcmPatchRAM::setConfiguration(int configurationIndex)
{
    IOReturn result;
    const USBCONFIGURATIONDESCRIPTOR* configurationDescriptor;
    UInt8 currentConfiguration = 0xFF;
    
    // Find the first config/interface
    UInt8 numconf = 0;
    
    if ((numconf = mDevice.getNumConfigurations()) < (configurationIndex + 1)) {
        AlwaysLog("[%04x:%04x]: Composite configuration index %d is not available, %d total composite configurations.\n",
                  mVendorId, mProductId, configurationIndex, numconf);
        return false;
    } else {
        DebugLog("[%04x:%04x]: Available composite configurations: %d.\n", mVendorId, mProductId, numconf);
    }
    configurationDescriptor = mDevice.getFullConfigurationDescriptor(configurationIndex);
    
    // Set the configuration to the requested configuration index
    if (!configurationDescriptor) {
        AlwaysLog("[%04x:%04x]: No configuration descriptor for configuration index: %d.\n", mVendorId, mProductId, configurationIndex);
        return false;
    }
    
    if ((result = mDevice.getConfiguration(this, &currentConfiguration)) != kIOReturnSuccess) {
        AlwaysLog("[%04x:%04x]: Unable to retrieve active configuration (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        return false;
    }
    
    // Device is already configured
    if (currentConfiguration != 0) {
        DebugLog("[%04x:%04x]: Device configuration is already set to configuration index %d.\n",
                 mVendorId, mProductId, configurationIndex);
        return true;
    }
    
    /*
     * Set the configuration to the first configuration.
     *
     * With interface matching disabled the interfaces are created but not registered,
     * so the native Bluetooth stack (bluetoothd) cannot see the controller while the
     * firmware is being uploaded. Otherwise it starts talking to the chip in the middle
     * of the upload when it is already running, e.g. after wake. They are registered
     * by revealInterfaces() once the upload is done.
     */
    if ((result = mDevice.setConfiguration(this, configurationDescriptor->bConfigurationValue, !mHideInterfaces)) != kIOReturnSuccess) {
        AlwaysLog("[%04x:%04x]: Unable to (re-)configure device (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        return false;
    }
    if (mHideInterfaces)
        mInterfacesHidden = true;
    
    DebugLog("[%04x:%04x]: Set device configuration to configuration index %d successfully.\n",
             mVendorId, mProductId, configurationIndex);
    
    return true;
}

bool BrcmPatchRAM::findInterface(USBInterfaceShim* shim)
{
    mDevice.findFirstInterface(shim);
    
    if (IOService* interface = shim->getValidatedInterface()) {
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
    if (!mInterface.findPipe(shim, type, direction)) {
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
    IOReturn result;
    
    if ((result = mInterruptPipe.read(mReadBuffer, 0, 0, mReadBuffer->getLength(), &mInterruptCompletion)) != kIOReturnSuccess) {
        AlwaysLog("[%04x:%04x]: continuousRead - Failed to queue read (0x%08x)\n", mVendorId, mProductId, result);
        /*
         * As a retry of the read operation has never been successful
         * in case of an error during my tests, it's better to give up
         * immediately, so that the next attempt can start all over.
         */
        if (result == kIOUSBPipeStalled)
            mInterruptPipe.clearStall();
        
        return false;
    }
    return true;
}

void BrcmPatchRAM::readCompletion(void* target, void* parameter, IOReturn status, uint32_t bytesTransferred)
{
    BrcmPatchRAM *me = (BrcmPatchRAM*)target;
    
    IOLockLock(me->mCompletionLock);
    
    switch (status)
    {
        case kIOReturnSuccess:
            if (me->mVerifyMode)
                me->verifyParse(me->mReadBuffer->getBytesNoCopy(), bytesTransferred);
            else
                me->hciParseResponse(me->mReadBuffer->getBytesNoCopy(), bytesTransferred, NULL, NULL);
            break;
            
        case kIOReturnAborted:
            // Verification aborts its own pending read on purpose, that is not worth a log line or a state change.
            if (!me->mVerifyMode) {
                AlwaysLog("[%04x:%04x]: readCompletion - Return aborted (0x%08x)\n", me->mVendorId, me->mProductId, status);
                me->mDeviceState = kUpdateAborted;
            }
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

    // Verification needs to tell a failed read from an event.
    if (me->mVerifyMode && status != kIOReturnSuccess)
        me->mVerifyReadStatus = status;

    me->mReadDone = true;
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
    
    switch (header->eventCode) {
        case HCI_EVENT_COMMAND_COMPLETE:
        {
            HCI_COMMAND_COMPLETE* event = (HCI_COMMAND_COMPLETE*)response;
            
            switch (event->opcode) {
                case HCI_OPCODE_READ_VERBOSE_CONFIG:
                    DebugLog("[%04x:%04x]: READ VERBOSE CONFIG complete (status: 0x%02x, length: %d bytes).\n",
                             mVendorId, mProductId, event->status, header->length);
                    
                    mFirmwareVersion = *(UInt16*)(((char*)response) + 10);
                    
                    AlwaysLog("[%04x:%04x]: Firmware version: v%d (%s).\n",
                              mVendorId, mProductId, mFirmwareVersion + 0x1000,
                              mFirmwareVersion > 0 ? "already loaded" : "not loaded");
                    
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

                    mDeviceState = mDeviceState == kPreInitialize ? kInitialize : kResetComplete;
                    break;
                    
                default:
                    DebugLog("[%04x:%04x]: Event COMMAND COMPLETE (opcode 0x%04x, status: 0x%02x, length: %d bytes).\n",
                             mVendorId, mProductId, event->opcode, event->status, header->length);
                    break;
            }
            
            if (output && outputLength) {
                bzero(output, *outputLength);
                
                // Return the received data
                if (*outputLength >= length) {
                    DebugLog("[%04x:%04x]: Returning output data %d bytes.\n", mVendorId, mProductId, length);
                    
                    *outputLength = length;
                    memcpy(output, response, length);
                } else {
                    // Not enough buffer space for data
                    result = kIOReturnMessageTooLarge;
                }
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
            
        case HCI_EVENT_VENDOR:
            DebugLog("[%04x:%04x]: Vendor specific event. Ready to reset device.\n", mVendorId, mProductId);
            
            if (mSupportsHandshake) {
                // Device is ready for reset.
                mDeviceState = kResetWrite;
            }
            break;
            
        default:
            DebugLog("[%04x:%04x]: Unknown event code (0x%02x).\n", mVendorId, mProductId, header->eventCode);
            break;
    }
    
    return result;
}

IOReturn BrcmPatchRAM::bulkWrite(const void* data, UInt16 length)
{
    IOMemoryDescriptor* buffer;
    IOReturn result = kIOReturnNoMemory;
    
    buffer = IOMemoryDescriptor::withAddress((void*)data, length, kIODirectionOut);
    
    if (!buffer) {
        AlwaysLog("[%04x:%04x]: Unable to allocate bulk write buffer.\n", mVendorId, mProductId);
        goto done;
    }
    if ((result = buffer->prepare(kIODirectionOut)) != kIOReturnSuccess) {
        AlwaysLog("[%04x:%04x]: Failed to prepare bulk write memory buffer (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        goto cleanup;
    }
    if ((result = mBulkPipe.write(buffer, 0, 0, buffer->getLength(), NULL)) != kIOReturnSuccess) {
        AlwaysLog("[%04x:%04x]: Failed to write to bulk pipe (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
    }
    if ((result = buffer->complete(kIODirectionOut)) != kIOReturnSuccess) {
        AlwaysLog("[%04x:%04x]: Failed to complete bulk write memory buffer (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
    }
    
cleanup:
    buffer->release();
    
done:
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
    mDeviceState = kPreInitialize;
    
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
            case kPreInitialize:
                /* Reset the device to put it in a defined state. */
                if (hciCommand(&HCI_RESET, sizeof(HCI_RESET)) != kIOReturnSuccess) {
                    DebugLog("HCI_RESET failed, aborting.");
                    mDeviceState = kUpdateAborted;
                    continue;
                }
                break;

            case kInitialize:
                /* Wait for device to become ready after reset. */
                IOSleep(mPostResetDelay);

                if (hciCommand(&HCI_VSC_READ_VERBOSE_CONFIG, sizeof(HCI_VSC_READ_VERBOSE_CONFIG)) != kIOReturnSuccess) {
                    DebugLog("HCI_VSC_READ_VERBOSE_CONFIG failed, aborting.");
                    mDeviceState = kUpdateAborted;
                    continue;
                }
                break;
                
            case kFirmwareVersion:
                // Unable to retrieve firmware store
                if (!(firmwareStore = getFirmwareStore())) {
                    mDeviceState = kUpdateAborted;
                    continue;
                }
                instructions = firmwareStore->getFirmware(mVendorId, mProductId, OSDynamicCast(OSString, getProperty(kFirmwareKey)));
                
                // Unable to retrieve firmware instructions
                if (!instructions) {
                    mDeviceState = kUpdateAborted;
                    continue;
                }
                
                // Initiate firmware upgrade
                if (hciCommand(&HCI_VSC_DOWNLOAD_MINIDRIVER, sizeof(HCI_VSC_DOWNLOAD_MINIDRIVER)) != kIOReturnSuccess) {
                    DebugLog("HCI_VSC_DOWNLOAD_MINIDRIVER failed, aborting.");
                    mDeviceState = kUpdateAborted;
                    continue;
                }
                break;
                
            case kMiniDriverComplete:
                // Should never happen, but semantically causes a leak.
                OSSafeReleaseNULL(iterator);
                // Write firmware data to bulk pipe
                iterator = OSCollectionIterator::withCollection(instructions);
                
                if (!iterator) {
                    mDeviceState = kUpdateAborted;
                    continue;
                }
                // If this IOSleep is not issued, the device is not ready to receive
                // the firmware instructions and we will deadlock due to lack of
                // responses.
                IOSleep(mInitialDelay);
                
                // Write first instruction to trigger response
                if ((data = OSDynamicCast(OSData, iterator->getNextObject())))
                //changed from bulkWrite for BigSur support
                hciCommand((void*)(data->getBytesNoCopy()), data->getLength());
                break;
                
            case kInstructionWrite:
                // should never happen, but would cause a crash
                if (!iterator) {
                    mDeviceState = kUpdateAborted;
                    continue;
                }
                
                if ((data = OSDynamicCast(OSData, iterator->getNextObject()))) {
                    //changed from bulkWrite for BigSur support
                    hciCommand((void*)(data->getBytesNoCopy()), data->getLength());                    
                } else {
                    // Firmware data fully written
                    if (hciCommand(&HCI_VSC_END_OF_RECORD, sizeof(HCI_VSC_END_OF_RECORD)) != kIOReturnSuccess) {
                        DebugLog("HCI_VSC_END_OF_RECORD failed, aborting.");
                        mDeviceState = kUpdateAborted;
                        continue;
                    }
                }
                break;
                
            case kInstructionWritten:
                mDeviceState = kInstructionWrite;
                continue;
                
            case kFirmwareWritten:
                if (!mSupportsHandshake) {
                    IOSleep(mPreResetDelay);

                    if (hciCommand(&HCI_RESET, sizeof(HCI_RESET)) != kIOReturnSuccess) {
                        DebugLog("HCI_RESET failed, aborting.");
                        mDeviceState = kUpdateAborted;
                        continue;
                    }
                }
                break;

            case kResetWrite:
                if (hciCommand(&HCI_RESET, sizeof(HCI_RESET)) != kIOReturnSuccess) {
                    DebugLog("HCI_RESET failed, aborting.");
                    mDeviceState = kUpdateAborted;
                    continue;
                }
                break;
                
            case kResetComplete:
                IOSleep(mPostResetDelay);

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
        mReadDone = false;
        if (!continuousRead()) {
            mDeviceState = kUpdateAborted;
            continue;
        }

        // wait for completion of the async read, but not forever
        uint64_t deadline;
        clock_interval_to_deadline(mStepTimeout, kMillisecondScale, &deadline);
        while (!mReadDone) {
            if (IOLockSleepDeadline(mCompletionLock, this, deadline, THREAD_UNINT) == THREAD_TIMED_OUT && !mReadDone)
                break;
        }

        if (!mReadDone) {
            AlwaysLog("[%04x:%04x]: No reply from device within %u ms (state %d), aborting attempt.\n",
                      mVendorId, mProductId, mStepTimeout, (int)mDeviceState);
            mDeviceState = kUpdateAborted;

            // Abort the stuck read. The completion handler takes the lock, so drop it meanwhile.
            IOLockUnlock(mCompletionLock);
            mInterruptPipe.abort();
            IOLockLock(mCompletionLock);

            // Let the (aborted) completion run before anything is torn down.
            clock_interval_to_deadline(2000, kMillisecondScale, &deadline);
            while (!mReadDone) {
                if (IOLockSleepDeadline(mCompletionLock, this, deadline, THREAD_UNINT) == THREAD_TIMED_OUT && !mReadDone) {
                    AlwaysLog("[%04x:%04x]: Pending read did not complete after abort.\n", mVendorId, mProductId);
                    break;
                }
            }
            mDeviceState = kUpdateAborted;
        }
    }
    
    IOLockUnlock(mCompletionLock);
    OSSafeReleaseNULL(iterator);
    
    return mDeviceState == kUpdateComplete || mDeviceState == kUpdateNotNeeded;
}

bool BrcmPatchRAM::supportsHandshake(UInt16 vid, UInt16 did)
{
    UInt32 i;
    
    for (i = 0; hskSupport[i].vid != 0; i++) {
        if ((hskSupport[i].vid == vid) && (hskSupport[i].did == did))
            return true;
    }
    return false;
}

#ifdef DEBUG
const char* BrcmPatchRAM::getState(DeviceState deviceState)
{
    static const IONamedValue state_values[] = {
        {kUnknown,            "Unknown"              },
        {kPreInitialize,      "PreInitialize"        },
        {kInitialize,         "Initialize"           },
        {kFirmwareVersion,    "Firmware version"     },
        {kMiniDriverComplete, "Mini-driver complete" },
        {kInstructionWrite,   "Instruction write"    },
        {kInstructionWritten, "Instruction written"  },
        {kFirmwareWritten,    "Firmware written"     },
        {kResetWrite,         "Reset write"          },
        {kResetComplete,      "Reset complete"       },
        {kUpdateComplete,     "Update complete"      },
        {kUpdateNotNeeded,    "Update not needed"    },
        {kUpdateAborted,      "Update aborted"       },
        {0,                   NULL                   }
    };
    
    return IOFindNameForValue(deviceState, state_values);
}
#endif //DEBUG

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

#endif /* TARGET_CATALINA */

