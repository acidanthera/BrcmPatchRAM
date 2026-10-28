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
#ifndef __BrcmPatchRAM__
#define __BrcmPatchRAM__

#if defined(TARGET_ELCAPITAN) || defined(TARGET_CATALINA)
// 10.11 works better if probe simply exits after updating firmware
#define NON_RESIDENT 1
#endif

#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>

#include "BrcmFirmwareStore.h"
#include "USBDeviceShim.h"

#define kDisplayName "DisplayName"
#define kBundleIdentifier "CFBundleIdentifier"
#define kIOUSBDeviceClassName "IOUSBDevice"
#ifndef kIOUSBHostDeviceClassName
#define kIOUSBHostDeviceClassName "IOUSBHostDevice"
#endif
#define kAppleBundlePrefix "com.apple."
#define kFirmwareKey "FirmwareKey"
#define kFirmwareLoaded "FirmwareLoaded"

enum DeviceState
{
    kUnknown,
    kPreInitialize,
    kInitialize,
    kFirmwareVersion,
    kMiniDriverComplete,
    kInstructionWrite,
    kInstructionWritten,
    kFirmwareWritten,
    kResetWrite,
    kResetComplete,
    kUpdateComplete,
    kUpdateNotNeeded,
    kUpdateAborted,
};

typedef struct DeviceHskSupport
{
    UInt16 vid;
    UInt16 did;
} DeviceHskSupport;

#if defined(TARGET_CATALINA)
#define BrcmPatchRAM BrcmPatchRAM3
#elif defined(TARGET_ELCAPITAN)
#define BrcmPatchRAM BrcmPatchRAM2
#endif

extern "C"
{
kern_return_t BrcmPatchRAM_Start(kmod_info_t*, void*);
kern_return_t BrcmPatchRAM_Stop(kmod_info_t*, void*);
}

class BrcmPatchRAM : public IOService
{
private:
    typedef IOService super;
#if defined(TARGET_CATALINA)
    OSDeclareDefaultStructors(BrcmPatchRAM3);
#elif defined(TARGET_ELCAPITAN)
    OSDeclareDefaultStructors(BrcmPatchRAM2);
#else
    OSDeclareDefaultStructors(BrcmPatchRAM);
#endif
    
    UInt16 mVendorId = 0xFFFF;
    UInt16 mProductId = 0xFFFF;
#ifndef TARGET_CATALINA
    UInt32 mProbeDelay = 0;
#endif
    UInt32 mPreResetDelay = 0;
    UInt32 mPostResetDelay = 0;
    UInt32 mInitialDelay = 0;

    USBDeviceShim mDevice;
    USBInterfaceShim mInterface;
    USBPipeShim mInterruptPipe;
    USBPipeShim mBulkPipe;
    BrcmFirmwareStore* mFirmwareStore = NULL;
#ifndef NON_RESIDENT
    bool mStopping = false;
#endif
    bool mSupportsHandshake = false;

    USBCOMPLETION mInterruptCompletion {};
    IOBufferMemoryDescriptor* mReadBuffer = NULL;
    
    volatile DeviceState mDeviceState = kInitialize;
    volatile uint16_t mFirmwareVersion = 0xFFFF;
    IOLock* mCompletionLock = NULL;

#ifdef TARGET_CATALINA
    /*
     * Robustness additions (BrcmPatchRAM3 only):
     *  - bounded waits for device replies (mStepTimeout)
     *  - several upload attempts per start (mMaxAttempts)
     *  - delayed background recovery if every attempt failed
     *  - optional USB re-enumeration as a last resort (mAllowPortReset)
     */
    UInt32 mMaxAttempts = 3;
    UInt32 mStepTimeout = 3000;          // ms to wait for one device reply
    bool mAllowPortReset = false;        // boot-arg bpr_portreset=1 (reset USB port if all upload attempts fail)
    bool mHideInterfaces = true;         // boot-arg bpr_hide=0 disables: keep interfaces unregistered during upload
    bool mWakeProbe = true;              // boot-arg bpr_wakeprobe=0 disables: check device health after wake
    bool mTestProbeFail = false;         // boot-arg bpr_testprobefail=1: pretend the wake probe failed (testing)
    volatile bool mReadDone = false;     // set by readCompletion, protected by mCompletionLock
    volatile bool mUploadOk = false;     // result of the most recent upload run
    volatile bool mShuttingDown = false;
    volatile bool mResetIssued = false;  // USB re-enumeration requested; device will go away
    volatile bool mInterfacesHidden = false; // interfaces exist but are not registered for matching yet
    UInt32 mRecoveryRound = 0;
    UInt32 mWakeCheckRetries = 0;
    IOWorkLoop* mRecoveryLoop = NULL;
    IOTimerEventSource* mRecoveryTimer = NULL;
    IOTimerEventSource* mWakeTimer = NULL;
    IOLock* mUploadLock = NULL;          // serialises start()/recovery/wake-check device access

    /*
     * Controller verification before the interfaces are handed to the native stack.
     * After the firmware upload the chip may still be rebooting its new firmware and can emit
     * its own HCI Reset completion. If bluetoothd starts its HCI init in the middle of that, its
     * first commands are lost, the stack sees an orphan event and times out on ReadLocalVersion
     * ("HCI initialization failed", crash loop). So, while the interfaces are still hidden,
     * ask the chip for its local version ourselves, discard any stray events and only continue
     * once it answers.
     */
    bool mVerifyHCI = true;              // boot-arg bpr_verify=0 disables
    UInt32 mVerifyQuiet = 0;             // boot-arg bpr_verifyquiet: ms of silence required after the reply (0 = skip)
    UInt32 mVerifyTimeout = 6000;        // boot-arg bpr_verifytimeout: total ms to wait for the controller to answer
    volatile bool mVerifyMode = false;   // readCompletion routes events to verifyParse (protected by mCompletionLock)
    volatile bool mVerifyGotVersion = false; // a Command Complete for Read Local Version arrived
    volatile bool mVerifyFailedLast = false; // the most recent upload attempt failed in verification, not in the upload
    volatile IOReturn mVerifyReadStatus = kIOReturnSuccess; // last non-success status of a read in verify mode
    volatile UInt32 mVerifyStray = 0;    // events other than the Read Local Version reply (discarded)
    volatile UInt16 mVerifyLastStrayOpcode = 0;
    volatile UInt8 mVerifyLastStrayEvent = 0;
    UInt8 mVerifyOpStatus = 0;           // HCI status of the Read Local Version reply
    UInt8 mVerifyHciVersion = 0;
    UInt16 mVerifyHciRevision = 0;
    UInt8 mVerifyLmpVersion = 0;
    UInt16 mVerifyManufacturer = 0;
    UInt16 mVerifyLmpSubversion = 0;

    bool verifyControllerReady();
    void verifyParse(void* response, UInt16 length);
    void abortPendingRead();

    bool uploadFirmwareOnce();
    bool uploadWithRetries();
    void scheduleRecovery(UInt32 delayMs);
    void recoveryTimerFired(IOTimerEventSource* sender);
    void wakeCheckFired(IOTimerEventSource* sender);
    bool probeDeviceAlive();
    void requestReset(const char* reason);
    void revealInterfaces();
    void publishStatus(const char* key, const char* format, ...) __printflike(3, 4);
#endif

#ifdef DEBUG
    static const char* getState(DeviceState deviceState);
#endif

#ifndef TARGET_CATALINA
    static OSString* brcmBundleIdentifier;
    static OSString* brcmIOClass;
    static OSString* brcmProviderClass;
    static void initBrcmStrings();
#endif
    
#ifdef DEBUG
    void printPersonalities();
#endif

#ifndef NON_RESIDENT
    UInt32 mBlurpWait = 0;
    IOTimerEventSource* mTimer = NULL;
    IOReturn onTimerEvent(void);

    static void uploadFirmwareThread(void* arg, wait_result_t wait);
    thread_t mWorker = 0;

    IOInterruptEventSource* mWorkSource = NULL;
    IOLock* mWorkLock = NULL;
    static IOLock* mLoadFirmwareLock;
    friend kern_return_t BrcmPatchRAM_Start(kmod_info_t*, void*);
    friend kern_return_t BrcmPatchRAM_Stop(kmod_info_t*, void*);

    enum WorkPending
    {
        kWorkLoadFirmware = 0x01,
        kWorkFinished = 0x02,
    };
    unsigned mWorkPending = 0;
    void scheduleWork(unsigned newWork);
    void processWorkQueue(IOInterruptEventSource*, int);
#endif // #ifndef NON_RESIDENT

#ifndef TARGET_CATALINA
    void publishPersonality();
#endif

#ifndef NON_RESIDENT
#if (!defined(TARGET_ELCAPITAN)) && (!defined(TARGET_CATALINA))
    void removePersonality();
#endif
#endif
#ifndef TARGET_CATALINA
    bool publishResourcePersonality(const char* classname);
#endif
    BrcmFirmwareStore* getFirmwareStore();
    void uploadFirmware();
    
    void printDeviceInfo();
    int getDeviceStatus();
    
    bool setConfiguration(int configurationIndex);
    
    bool findInterface(USBInterfaceShim* interface);
    bool findPipe(USBPipeShim* pipe, uint8_t type, uint8_t direction);
    
    bool continuousRead();
#if defined(TARGET_ELCAPITAN) || defined(TARGET_CATALINA)
    static void readCompletion(void* target, void* parameter, IOReturn status, uint32_t bytesTransferred);
#else
    static void readCompletion(void* target, void* parameter, IOReturn status, UInt32 bufferSizeRemaining);
#endif
    
    IOReturn hciCommand(void * command, uint16_t length);
    IOReturn hciParseResponse(void* response, uint16_t length, void* output, uint8_t* outputLength);
    
    IOReturn bulkWrite(const void* data, uint16_t length);
    
    uint16_t getFirmwareVersion();
    
    bool performUpgrade();
    bool supportsHandshake(UInt16 vid, UInt16 did);
public:
    IOService* probe(IOService *provider, SInt32 *probeScore) override;
#if defined(TARGET_CATALINA) || (!defined(NON_RESIDENT))
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    IOReturn setPowerState(unsigned long which, IOService *whom) override;
#endif
    
    const char* stringFromReturn(IOReturn rtn) override;
    
#ifdef TARGET_CATALINA
    bool init(OSDictionary *properties) override;
    void free() override;
#endif
};

#if defined(NON_RESIDENT) && (!defined(TARGET_CATALINA))

#define kBrcmPatchRAMResidency "BrcmPatchRAMResidency"
class BrcmPatchRAMResidency : public IOService
{
private:
    typedef IOService super;
    OSDeclareDefaultStructors(BrcmPatchRAMResidency);

public:
    bool start(IOService *provider) override;
};

#endif //defined(NON_RESIDENT) && (!defined(TARGET_CATALINA))

#endif //__BrcmPatchRAM__
