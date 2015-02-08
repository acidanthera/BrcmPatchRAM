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

#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>

#include <IOKit/usb/IOUSBInterface.h>
#include <IOKit/IOCatalogue.h>

#include <kern/clock.h>
#include <libkern/version.h>
#include <libkern/zlib.h>

#include "Common.h"
#include "hci.h"
#include "BrcmPatchRAM.h"

OSDefineMetaClassAndStructors(BrcmPatchRAM, IOService)

/******************************************************************************
 * BrcmPatchRAM::probe - parse kernel extension Info.plist
 ******************************************************************************/
IOService* BrcmPatchRAM::probe(IOService *provider, SInt32 *probeScore)
{
    extern kmod_info_t kmod_info;
    uint64_t start_time, end_time, nano_secs;
    
    clock_get_uptime(&start_time);
    
    DebugLog("probe\n");
    
    AlwaysLog("%s: Version %s starting on OS X Darwin %d.%d.\n", kmod_info.version, version_major, version_minor);
    
    mDevice = OSDynamicCast(IOUSBDevice, provider);
    
    if (mDevice)
    {
        OSString* displayName = OSDynamicCast(OSString, getProperty(kDisplayName));
        
        if (displayName)
            provider->setProperty(kUSBProductString, displayName);
        
        BrcmFirmwareStore* firmwareStore;
        
        mDevice->retain();
        
        if (mDevice->open(this))
        {
            mVendorId = mDevice->GetVendorID();
            mProductId = mDevice->GetProductID();
            
            // Print out additional device information
            printDeviceInfo();
            
            // Set device configuration to composite configuration index 0
            if (!setConfiguration(0))
                return false;
            
            // Obtain first interface
            mInterface = findInterface();
            
            if (mInterface)
            {
                mInterface->retain();
                mInterface->open(this);
                
                mInterruptPipe = findPipe(kUSBInterrupt, kUSBIn);
                mBulkPipe = findPipe(kUSBBulk, kUSBOut);
                
                if (mInterruptPipe && mBulkPipe)
                {
                    // getFirmwareVersion additionally re-synchronizes outstanding responses
                    UInt16 firmwareVersion = getFirmwareVersion();
                    
                    if (firmwareVersion > 0)
                        AlwaysLog("Current firmware version v%d, no upgrade required.\n", firmwareVersion);
                    
                    if (firmwareVersion == 0 && (firmwareStore = getFirmwareStore()) != NULL)
                    {
                        OSArray* instructions = firmwareStore->getFirmware(OSDynamicCast(OSString, getProperty("FirmwareKey")));
                        
                        if (instructions)
                        {
                            // Initiate firmware upgrade
                            hciCommand(&HCI_VSC_DOWNLOAD_MINIDRIVER, sizeof(HCI_VSC_DOWNLOAD_MINIDRIVER));
                            queueRead();
                            
                            // Wait for mini driver download
                            IOSleep(5);
                            
                            // Write firmware data to bulk pipe
                            OSCollectionIterator* iterator = OSCollectionIterator::withCollection(instructions);
                            
                            OSData* data;
                            while ((data = OSDynamicCast(OSData, iterator->getNextObject())))
                            {
                                bulkWrite((void *)data->getBytesNoCopy(), data->getLength());
                                queueRead();
                            }
                            
                            OSSafeRelease(iterator);
                            
                            hciCommand(&HCI_VSC_END_OF_RECORD, sizeof(HCI_VSC_END_OF_RECORD));
                            queueRead();
                            queueRead();
                            
                            IOSleep(100);
                            
                            //hciCommand(&HCI_VSC_WAKEUP, sizeof(HCI_VSC_WAKEUP));
                            //queueRead();
                            
                            hciCommandSync(&HCI_RESET, sizeof(HCI_RESET));
                            //queueRead();
                            
                            IOSleep(50);
                            
                            resetDevice();
                            
                            getDeviceStatus();
                            
                            AlwaysLog("[%04x:%04x]: Firmware upgrade completed successfully.\n", mVendorId, mProductId);
                        }
                    }
                }
            }
            
            if (mInterruptPipe)
            {
                mInterruptPipe->Abort();
                mInterruptPipe->release();
            }
            
            if (mBulkPipe)
            {
                mBulkPipe->Abort();
                mBulkPipe->release();
            }
            
            if (mInterface)
            {
                mInterface->close(this);
                mInterface->release();
            }
            
            mDevice->close(this);
        }
        
        mDevice->release();
    }
    else
        AlwaysLog("Provider is not a USB device.\n");
    
    clock_get_uptime(&end_time);
    absolutetime_to_nanoseconds(end_time - start_time, &nano_secs);
    uint64_t milli_secs = nano_secs / 1000000;
    
    AlwaysLog("Processing time %llu.%llu seconds.\n", milli_secs / 10000, milli_secs % 1000);
    
    publishPersonality();
    
    return NULL;
}

void BrcmPatchRAM::publishPersonality()
{
    SInt32 generationCount;
    OSOrderedSet* set = NULL;
    OSDictionary* dict = OSDictionary::withCapacity(5);
    
    // Matching dictionary for the current device
    dict->setObject(kIOProviderClassKey, OSString::withCString(kIOUSBDeviceClassName));
    dict->setObject(kUSBProductID, OSNumber::withNumber(mProductId, 16));
    dict->setObject(kUSBVendorID, OSNumber::withNumber(mVendorId, 16));
    
    set = gIOCatalogue->findDrivers(dict, &generationCount);
    
    // Retrieve currently matching IOKit driver personalities
    if (set && set->getCount() > 0)
    {
        DebugLog("[%04x:%04x]: %d matching driver personalities.\n", mVendorId, mProductId, set->getCount());
        
        OSCollectionIterator* iterator = OSCollectionIterator::withCollection(set);
        
        OSDictionary* personality;
        
        while ((personality = OSDynamicCast(OSDictionary, iterator->getNextObject())))
        {
            OSString* bundleId = OSDynamicCast(OSString, personality->getObject(kBundleIdentifier));
            
            if (bundleId)
                if (strncmp(bundleId->getCStringNoCopy(), kAppleBundlePrefix, strlen(kAppleBundlePrefix)) == 0)
                {
                    AlwaysLog("[%04x:%04x]: Found existing IOKit personality \"%s\".\n", mVendorId, mProductId, bundleId->getCStringNoCopy());
                    return;
                }
        }
        
        OSSafeRelease(iterator);
    }
    
    // OS X does not have a driver personality for this device yet, publish one
    // OS X - Snow Leopard
    // OS X - Lion
    if (version_major == 10 || version_major == 11)
    {
        dict->setObject(kBundleIdentifier, OSString::withCString("com.apple.driver.BroadcomUSBBluetoothHCIController"));
        dict->setObject(kIOClassKey, OSString::withCString("BroadcomUSBBluetoothHCIController"));
    }
    // OS X - Mountain Lion (12.0 until 12.4)
    else if (version_major == 12 && version_minor <= 4)
    {
        dict->setObject(kBundleIdentifier, OSString::withCString("com.apple.iokit.BroadcomBluetoothHCIControllerUSBTransport"));
        dict->setObject(kIOClassKey, OSString::withCString("BroadcomBluetoothHCIControllerUSBTransport"));
    }
    // OS X - Mountain Lion (12.5.0)
    // OS X - Mavericks
    // OS X - Yosemite
    else if (version_major == 12 || version_major == 13 || version_major == 14)
    {
        dict->setObject(kBundleIdentifier, OSString::withCString("com.apple.iokit.BroadcomBluetoothHostControllerUSBTransport"));
        dict->setObject(kIOClassKey, OSString::withCString("BroadcomBluetoothHostControllerUSBTransport"));
    }
    // OS X - Future releases....
    else if (version_major > 14)
    {
        AlwaysLog("[%04x:%04x]: Unknown new Darwin version %d.%d, publishing possible compatible personality.\n",
              mVendorId, mProductId, version_major, version_minor);
        
        dict->setObject(kBundleIdentifier, OSString::withCString("com.apple.iokit.BroadcomBluetoothHostControllerUSBTransport"));
        dict->setObject(kIOClassKey, OSString::withCString("BroadcomBluetoothHostControllerUSBTransport"));
    }
    else
    {
        AlwaysLog("[%04x:%04x]: Unknown Darwin version %d.%d, no compatible personality known.\n",
              mVendorId, mProductId, version_major, version_minor);
        return;
    }
    
    OSArray* array = OSArray::withCapacity(1);
    array->setObject(dict);
    
    // Add new personality into the kernel
    gIOCatalogue->addDrivers(array);
    
    OSSafeRelease(array);
    
    AlwaysLog("[%04x:%04x]: Published new IOKit personality.\n", mVendorId, mProductId);
}

BrcmFirmwareStore* BrcmPatchRAM::getFirmwareStore()
{
    BrcmFirmwareStore* firmwareStore = OSDynamicCast(BrcmFirmwareStore, getResourceService()->getProperty(kBrcmFirmwareStoreService));
    
    if (!firmwareStore)
        AlwaysLog("[%04x:%04x]: BrcmFirmwareStore does not appear to be available.\n", mVendorId, mProductId);
    
    return firmwareStore;
}

void BrcmPatchRAM::printDeviceInfo()
{
    char product[255];
    char manufacturer[255];
    char serial[255];
    
    // Retrieve device information
    mDevice->GetStringDescriptor(mDevice->GetProductStringIndex(), product, sizeof(product));
    mDevice->GetStringDescriptor(mDevice->GetManufacturerStringIndex(), manufacturer, sizeof(manufacturer));
    mDevice->GetStringDescriptor(mDevice->GetSerialNumberStringIndex(), serial, sizeof(serial));
    
    AlwaysLog("[%04x:%04x]: USB [%s v%d] \"%s\" by \"%s\"\n",
          mVendorId,
          mProductId,
          serial,
          mDevice->GetDeviceRelease(),
          product,
          manufacturer);
}

int BrcmPatchRAM::getDeviceStatus()
{
    IOReturn result;
    USBStatus status;
    
    if ((result = mDevice->GetDeviceStatus(&status)) != kIOReturnSuccess)
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
    
    if ((result = mDevice->ResetDevice()) != kIOReturnSuccess)
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
    const IOUSBConfigurationDescriptor* configurationDescriptor;
    UInt8 currentConfiguration = 0xFF;
    
    // Find the first config/interface
    UInt8 numconf = 0;
    
    if ((numconf = mDevice->GetNumConfigurations()) < (configurationIndex + 1))
    {
        AlwaysLog("[%04x:%04x]: Composite configuration index %d is not available, %d total composite configurations.\n",
              mVendorId, mProductId, configurationIndex, numconf);
        return false;
    }
    else
        DebugLog("[%04x:%04x]: Available composite configurations: %d.\n", mVendorId, mProductId, numconf);
    
    configurationDescriptor = mDevice->GetFullConfigurationDescriptor(configurationIndex);
    
    // Set the configuration to the requested configuration index
    if (!configurationDescriptor)
    {
        AlwaysLog("[%04x:%04x]: No configuration descriptor for configuration index: %d.\n", mVendorId, mProductId, configurationIndex);
        return false;
    }
    
    if ((result = mDevice->GetConfiguration(&currentConfiguration)) != kIOReturnSuccess)
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
    if ((result = mDevice->SetConfiguration(this, configurationDescriptor->bConfigurationValue, true)) != kIOReturnSuccess)
    {
        AlwaysLog("[%04x:%04x]: Unable to (re-)configure device (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        mDevice->close(this);
        return false;
    }
    
    DebugLog("[%04x:%04x]: Set device configuration to configuration index %d successfully.\n",
              mVendorId, mProductId, configurationIndex);
    
    return true;
}

IOUSBInterface* BrcmPatchRAM::findInterface()
{
    IOUSBFindInterfaceRequest request;
    IOUSBInterface* interface = NULL;
    
    // Find the interface for bulk endpoint transfers
    request.bAlternateSetting  = kIOUSBFindInterfaceDontCare;
    request.bInterfaceClass    = kIOUSBFindInterfaceDontCare;
    request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
    request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    
    if ((interface = mDevice->FindNextInterface(NULL, &request)))
    {
        DebugLog("[%04x:%04x]: Interface %d (class %02x, subclass %02x, protocol %02x) located.\n",
                  mVendorId,
                  mProductId,
                  interface->GetInterfaceNumber(),
                  interface->GetInterfaceClass(),
                  interface->GetInterfaceSubClass(),
                  interface->GetInterfaceProtocol());
        
        return interface;
    }
    
    AlwaysLog("%s [%04x:%04x]: No interface could be located.\n", mVendorId, mProductId);
    
    return NULL;
}

IOUSBPipe* BrcmPatchRAM::findPipe(UInt8 type, UInt8 direction)
{
    IOUSBFindEndpointRequest findEndpointRequest;
    
    findEndpointRequest.type = type;
    findEndpointRequest.direction = direction;
    
    IOUSBPipe* pipe = mInterface->FindNextPipe(NULL, &findEndpointRequest, true);
    
    if (pipe)
    {
        DebugLog("[%04x:%04x]: Located pipe at 0x%02x.\n", mVendorId, mProductId, pipe->GetEndpointDescriptor()->bEndpointAddress);
        return pipe;
    }
    else
        AlwaysLog("[%04x:%04x]: Unable to locate pipe.\n", mVendorId, mProductId);
    
    return NULL;
}

IOReturn BrcmPatchRAM::queueRead()
{
    IOReturn result;
    
    IOBufferMemoryDescriptor* buffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, 0x200);
    
    if (buffer)
    {
        if ((result = buffer->prepare()) == kIOReturnSuccess)
        {
            IOUSBCompletion completion =
            {
                .target = this,
                .action = interruptReadEntry,
                .parameter = buffer
            };
            
            mReadQueued = true;
            
            if ((result = mInterruptPipe->Read(buffer, 0, 0, buffer->getLength(), &completion)) != kIOReturnSuccess)
            {
                mReadQueued = false;
                AlwaysLog("[%04x:%04x]: Error initiating read (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
            }
            else
            {
                // Wait until read is complete
                while (mReadQueued)
                    IOSleep(1);
            }
            
            if ((result = buffer->complete()) != kIOReturnSuccess)
                AlwaysLog("[%04x:%04x]: Failed to complete queued read memory buffer (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        }
        else
            AlwaysLog("[%04x:%04x]: Failed to prepare queued read memory buffer (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        
        buffer->release();
    }
    else
    {
        AlwaysLog("[%04x:%04x]: Unable to allocate read buffer.\n", mVendorId, mProductId);
        result = kIOReturnNoMemory;
    }
    
    return result;
}

void BrcmPatchRAM::interruptReadEntry(void* target, void* parameter, IOReturn status, UInt32 bufferSizeRemaining)
{
    if (target)
        ((BrcmPatchRAM*)target)->interruptReadHandler(parameter, status, bufferSizeRemaining);
}

void BrcmPatchRAM::interruptReadHandler(void* parameter, IOReturn status, UInt32 bufferSizeRemaining)
{
    IOBufferMemoryDescriptor* buffer = (IOBufferMemoryDescriptor*)parameter;
    
    if (!buffer)
    {
        AlwaysLog("[%04x:%04x]: Queued read, buffer is NULL.\n", mVendorId, mProductId);
        return;
    }
    
    switch (status)
    {
        case kIOReturnOverrun:
            DebugLog("[%04x:%04x]: read - kIOReturnOverrun\n", mVendorId, mProductId);
            mInterruptPipe->ClearStall();
        case kIOReturnSuccess:
        {
            hciParseResponse(buffer->getBytesNoCopy(), buffer->getLength() - bufferSizeRemaining, NULL, NULL);
            break;
        }
        case kIOReturnNotResponding:
            DebugLog("[%04x:%04x]: read - kIOReturnNotResponding\n", mVendorId, mProductId);
            break;
        default:
            DebugLog("[%04x:%04x]: read - Other (\"%s\" 0x%08x)\n", mVendorId, mProductId, stringFromReturn(status), status);
            break;
    }
    
    mReadQueued = false;
}

IOReturn BrcmPatchRAM::hciCommand(void * command, UInt16 length)
{
    IOReturn result;
    
    IOUSBDevRequest request =
    {
        .bmRequestType = USBmakebmRequestType(kUSBOut, kUSBClass, kUSBDevice),
        .bRequest = 0,
        .wValue = 0,
        .wIndex = 0,
        .wLength = length,
        .pData = command
    };
    
    if ((result = mInterface->DeviceRequest(&request)) != kIOReturnSuccess)
        AlwaysLog("[%04x:%04x]: device request failed (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
    
    return result;
}

IOReturn BrcmPatchRAM::hciCommandSync(void* command, UInt16 length)
{
    return hciCommandSync(command, length, NULL, NULL);
}

IOReturn BrcmPatchRAM::hciCommandSync(void* command, UInt16 length, void* output, UInt8* outputLength)
{
    IOReturn result;
    
    if ((result = hciCommand(command, length)) == kIOReturnSuccess)
        result = interruptRead(output, outputLength);
    
    return result;
}

IOReturn BrcmPatchRAM::hciParseResponse(void* response, UInt16 length, void* output, UInt8* outputLength)
{
    HCI_RESPONSE* header = (HCI_RESPONSE*)response;
    
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
                    
                    DebugLog("[%04x:%04x]: Firmware version: v%d.\n",
                              mVendorId, mProductId, (*(UInt16*)(((char*)response) + 10)) + 4096);
                    break;
                case HCI_OPCODE_DOWNLOAD_MINIDRIVER:
                    DebugLog("[%04x:%04x]: DOWNLOAD MINIDRIVER complete (status: 0x%02x, length: %d bytes).\n",
                              mVendorId, mProductId, event->status, header->length);
                    break;
                case HCI_OPCODE_LAUNCH_RAM:
                    //DEBUG_LOG("%s [%04x:%04x]: LAUNCH RAM complete (status: 0x%02x, length: %d bytes).\n",
                    //          getName(), mVendorId, mProductId, event->status, header->length);
                    break;
                case HCI_OPCODE_END_OF_RECORD:
                    DebugLog("[%04x:%04x]: END OF RECORD complete (status: 0x%02x, length: %d bytes).\n",
                              mVendorId, mProductId, event->status, header->length);
                    break;
                case HCI_OPCODE_RESET:
                    DebugLog("[%04x:%04x]: RESET complete (status: 0x%02x, length: %d bytes).\n",
                              mVendorId, mProductId, event->status, header->length);
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
                    return kIOReturnMessageTooLarge;
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
            DebugLog("%s [%04x:%04x]: Unknown event code (0x%02x).\n", mVendorId, mProductId, header->eventCode);
            break;
    }
    
    return kIOReturnSuccess;
}

IOReturn BrcmPatchRAM::interruptRead()
{
    return interruptRead(NULL, NULL);
}

IOReturn BrcmPatchRAM::interruptRead(void* output, UInt8* length)
{
    IOReturn result;
    IOBufferMemoryDescriptor* buffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, 0x200);
    
    IOSleep(10);
    
    if (buffer)
    {
        if ((result = buffer->prepare()) == kIOReturnSuccess)
        {
            IOByteCount reqCount = buffer->getLength();
            IOByteCount readCount;
            
            if ((result = mInterruptPipe->Read(buffer, 0, 0, reqCount, (IOUSBCompletion*)NULL, &readCount)) == kIOReturnSuccess)
                result = hciParseResponse(buffer->getBytesNoCopy(), readCount, output, length);
            else
                AlwaysLog("[%04x:%04x]: Failed to read from interrupt pipe sychronously (\"%s\" 0x%08x).\n",
                      mVendorId, mProductId, stringFromReturn(result), result);
        }
        else
            AlwaysLog("[%04x:%04x]: Failed to prepare interrupt read memory buffer (\"%s\" 0x%08x).\n",
                  mVendorId, mProductId, stringFromReturn(result), result);
        
        if ((result = buffer->complete()) != kIOReturnSuccess)
            AlwaysLog("[%04x:%04x]: Failed to complete interrupt read memory buffer (\"%s\" 0x%08x).\n",
                  mVendorId, mProductId, stringFromReturn(result), result);
        
        buffer->release();
    }
    else
    {
        AlwaysLog("[%04x:%04x]: Unable to allocate interrupt read buffer.\n", mVendorId, mProductId);
        result = kIOReturnNoMemory;
    }
    
    return result;
}

IOReturn BrcmPatchRAM::bulkWrite(void* data, UInt16 length)
{
    IOReturn result;
    IOMemoryDescriptor* buffer = IOMemoryDescriptor::withAddress(data, length, kIODirectionIn);
    
    if (buffer)
    {
        if ((result = buffer->prepare()) == kIOReturnSuccess)
        {
            if ((result = mBulkPipe->Write(buffer, 0, 0, buffer->getLength(), (IOUSBCompletion*)NULL)) == kIOReturnSuccess)
            {
                //DEBUG_LOG("%s: Wrote %d bytes to bulk pipe.\n", getName(), length);
            }
            else
                AlwaysLog("[%04x:%04x]: Failed to write to bulk pipe (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        }
        else
            AlwaysLog("[%04x:%04x]: Failed to prepare bulk write memory buffer (\"%s\" 0x%08x).\n", mVendorId, mProductId, stringFromReturn(result), result);
        
        if ((result = buffer->complete()) != kIOReturnSuccess)
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

UInt16 BrcmPatchRAM::getFirmwareVersion()
{
    char response[0x20];
    UInt8 length = sizeof(response);
    
    if (hciCommand(&HCI_VSC_READ_VERBOSE_CONFIG, sizeof(HCI_VSC_READ_VERBOSE_CONFIG)) == kIOReturnSuccess)
    {
        // There might be other outstanding events pending,
        // keep reading data until we find our matching response
        for (int i = 0; i < 100; i++)
        {
            if (interruptRead(response, &length) == kIOReturnSuccess)
            {
                HCI_RESPONSE* header = (HCI_RESPONSE*)response;
                
                if (header->eventCode == HCI_EVENT_COMMAND_COMPLETE)
                {
                    HCI_COMMAND_COMPLETE* event = (HCI_COMMAND_COMPLETE*)response;
                    
                    if (event->opcode == HCI_OPCODE_READ_VERBOSE_CONFIG)
                        return *(UInt16*)(((char*)response) + 10);
                }
            }
        }
    }
    
    return 0xFFFF;
}

const char* BrcmPatchRAM::stringFromReturn(IOReturn rtn)
{
    static const IONamedValue IOReturn_values[] = {
        {kIOReturnIsoTooOld,          "Isochronous I/O request for distant past"     },
        {kIOReturnIsoTooNew,          "Isochronous I/O request for distant future"   },
        {kIOReturnNotFound,           "Data was not found"                           },
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
        {0,                           NULL                                           }
    };
    
    const char* result = IOFindNameForValue(rtn, IOReturn_values);
    
    if (result)
        return result;
    
    return super::stringFromReturn(rtn);
}