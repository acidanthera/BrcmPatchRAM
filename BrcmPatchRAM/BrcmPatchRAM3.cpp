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
    
    if (!mCompletionLock)
        goto error1;

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

    uploadFirmware();
    
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
    IOLockFree(mCompletionLock);
    mCompletionLock = NULL;
    
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
    
    PMstop();

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
    DebugLog("setPowerState: which = 0x%lx\n", which);
    
    if (which == kMyOffPowerState) {
        
    } else if (which == kMyOnPowerState) {
        
    }
    return IOPMAckImplied;
}

void BrcmPatchRAM::uploadFirmware()
{
    // signal to timer that firmware already loaded
    mDevice.setProperty(kFirmwareLoaded, true);
    
    // don't bother with devices that have no firmware
    if (!getProperty(kFirmwareKey))
        return;
    
    if (!mDevice.open(this)) {
        AlwaysLog("uploadFirmware could not open the device!\n");
        return;
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
                if (mDeviceState == kUpdateComplete) {
                    AlwaysLog("[%04x:%04x]: Firmware upgrade completed successfully.\n", mVendorId, mProductId);
                } else {
                    AlwaysLog("[%04x:%04x]: Firmware upgrade not needed.\n", mVendorId, mProductId);
                }
            } else {
                AlwaysLog("[%04x:%04x]: Firmware upgrade failed.\n", mVendorId, mProductId);
            }
        }
        mInterface.close(this);
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
    
    // Set the configuration to the first configuration
    if ((result = mDevice.setConfiguration(this, configurationDescriptor->bConfigurationValue, true)) != kIOReturnSuccess) {
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
            me->hciParseResponse(me->mReadBuffer->getBytesNoCopy(), bytesTransferred, NULL, NULL);
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
    
    switch (header->eventCode) {
        case HCI_EVENT_COMMAND_COMPLETE:
        {
            HCI_COMMAND_COMPLETE* event = (HCI_COMMAND_COMPLETE*)response;
            
            switch (event->opcode) {
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
        if (!continuousRead()) {
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

