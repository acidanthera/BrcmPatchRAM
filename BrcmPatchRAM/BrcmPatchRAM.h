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
    
    UInt16 mVendorId;
    UInt16 mProductId;
#ifndef TARGET_CATALINA
    UInt32 mProbeDelay;
#endif
    UInt32 mPreResetDelay;
    UInt32 mPostResetDelay;
    UInt32 mInitialDelay;

    USBDeviceShim mDevice;
    USBInterfaceShim mInterface;
    USBPipeShim mInterruptPipe;
    USBPipeShim mBulkPipe;
    BrcmFirmwareStore* mFirmwareStore = NULL;
#ifndef NON_RESIDENT
    bool mStopping = false;
#endif
    bool mSupportsHandshake;

    USBCOMPLETION mInterruptCompletion;
    IOBufferMemoryDescriptor* mReadBuffer;
    
    volatile DeviceState mDeviceState = kInitialize;
    volatile uint16_t mFirmwareVersion = 0xFFFF;
    IOLock* mCompletionLock = NULL;
    
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
    UInt32 mBlurpWait;
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
    
    bool resetDevice();
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
    virtual IOService* probe(IOService *provider, SInt32 *probeScore);
#if defined(TARGET_CATALINA) || (!defined(NON_RESIDENT))
    virtual bool start(IOService* provider);
    virtual void stop(IOService* provider);
    virtual IOReturn setPowerState(unsigned long which, IOService *whom);
#endif
    
#ifndef NON_RESIDENT
    virtual IOReturn setPowerState(unsigned long which, IOService *whom);
#endif
    
    virtual const char* stringFromReturn(IOReturn rtn);
    
#ifdef TARGET_CATALINA
    virtual bool init(OSDictionary *properties);
    virtual void free();
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
    virtual bool start(IOService *provider);
};

#endif //defined(NON_RESIDENT) && (!defined(TARGET_CATALINA))

#endif //__BrcmPatchRAM__
