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

#include <libkern/zlib.h>

#include "Common.h"
#include "hci.h"
#include "BrcmPatchRAM.h"

OSDefineMetaClassAndStructors(BrcmPatchRAM, IOService)

bool BrcmPatchRAM::init(OSDictionary *dictionary)
{
    DEBUG_LOG("BrcmPatchRAM::init\n"); // getName() is not available yet
    return super::init(dictionary);
}

IOService* BrcmPatchRAM::probe(IOService *provider, SInt32 *probeScore)
{
    DEBUG_LOG("%s::probe\n", getName());
    
    mDevice = OSDynamicCast(IOUSBDevice, provider);
    
    if (mDevice != NULL)
    {
        mVendorId = mDevice->GetVendorID();
        mProductId = mDevice->GetProductID();
        
        return super::probe(provider, probeScore);
    }
    
    IOLog("%s: Provider is not a USB device.\n", getName());
    
    return NULL;
}

bool BrcmPatchRAM::start(IOService *provider)
{
    IOLog("%s [%04x:%04x]: Version 0.6a starting.\n", getName(), mVendorId, mProductId);

    if (!super::start(provider))
        return false;
    
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
            continousRead();
            
            if (performUpgrade())
                IOLog("%s [%04x:%04x]: Firmware upgrade completed successfully.\n", getName(), mVendorId, mProductId);
            else
                IOLog("%s [%04x:%04x]: Firmware upgrade failed.\n", getName(), mVendorId, mProductId);
        }
    }
    
    if (mReadBuffer)
        mReadBuffer->release();
    
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

    return false;
}

void BrcmPatchRAM::stop(IOService *provider)
{
    DEBUG_LOG("%s [%04x:%04x]: Stopping...\n", getName(), mVendorId, mProductId);
    super::stop(provider);
}

unsigned int BrcmPatchRAM::getDelayValue(const char* key)
{
    OSNumber* value = OSDynamicCast(OSNumber, getProperty(key));
    
    if (value)
        return value->unsigned32BitValue();
    else
        return DEFAULT_DELAY;        
}

BrcmFirmwareStore* BrcmPatchRAM::getFirmwareStore()
{
    BrcmFirmwareStore* firmwareStore = NULL;
    
    firmwareStore = OSDynamicCast(BrcmFirmwareStore, getResourceService()->getProperty(kBrcmFirmwareStoreService));
    
    if (!firmwareStore)
        IOLog("%s [%04x:%04x]: BrcmFirmwareStore does not appear to be available.\n", getName(), mVendorId, mProductId);
    
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
    
    IOLog("%s [%04x:%04x]: USB [%s v%d] \"%s\" by \"%s\"\n",
          getName(),
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
        IOLog("%s [%04x:%04x]: Unable to get device status (0x%08x).\n", getName(), mVendorId, mProductId, result);
        return 0;
    }
    else DEBUG_LOG("%s [%04x:%04x]: Device status 0x%08x.\n", getName(), mVendorId, mProductId, (int)status);
    
    return (int)status;
}

bool BrcmPatchRAM::resetDevice()
{
    IOReturn result;
    
    if ((result = mDevice->ResetDevice()) != kIOReturnSuccess)
    {
        IOLog("%s [%04x:%04x]: Failed to reset the device (0x%08x).\n", getName(), mVendorId, mProductId, result);
        return false;
    }
    else
        DEBUG_LOG("%s [%04x:%04x]: Device reset.\n", getName(), mVendorId, mProductId);
    
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
        IOLog("%s [%04x:%04x]: Composite configuration index %d is not available, %d total composite configurations.\n",
              getName(), mVendorId, mProductId, configurationIndex, numconf);
        return false;
    }
    else
        DEBUG_LOG("%s [%04x:%04x]: Available composite configurations: %d.\n", getName(), mVendorId, mProductId, numconf);
    
    configurationDescriptor = mDevice->GetFullConfigurationDescriptor(configurationIndex);
    
    // Set the configuration to the requested configuration index
    if (!configurationDescriptor)
    {
        IOLog("%s [%04x:%04x]: No configuration descriptor for configuration index: %d.\n", getName(), mVendorId, mProductId, configurationIndex);
        return false;
    }
    
    if ((result = mDevice->GetConfiguration(&currentConfiguration)) != kIOReturnSuccess)
    {
        IOLog("%s [%04x:%04x]: Unable to retrieve active configuration (0x%08x).\n", getName(), mVendorId, mProductId, result);
        return false;
    }
    
    // Device is already configured
    if (currentConfiguration == configurationDescriptor->bConfigurationValue)
    {
        DEBUG_LOG("%s [%04x:%04x]: Device configuration is already set to configuration index %d.\n", getName(),
                  mVendorId, mProductId, configurationIndex);
        return true;
    }
    
    if (!mDevice->open(this))
    {
        IOLog("%s [%04x:%04x]: Unable to open device for (re-)configuration.\n", getName(), mVendorId, mProductId);
        return false;
    }
    
    // Set the configuration to the first configuration
    if ((result = mDevice->SetConfiguration(this, configurationDescriptor->bConfigurationValue, true)) != kIOReturnSuccess)
    {
        IOLog("%s [%04x:%04x]: Unable to (re-)configure device (0x%08x).\n", getName(), mVendorId, mProductId, result);
        mDevice->close(this);
        return false;
    }
    
    DEBUG_LOG("%s [%04x:%04x]: Set device configuration to configuration index %d successfully.\n", getName(),
              mVendorId, mProductId, configurationIndex);
    
    mDevice->close(this);
    
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
        DEBUG_LOG("%s [%04x:%04x]: Interface %d (class %02x, subclass %02x, protocol %02x) located.\n",
                  getName(),
                  mVendorId,
                  mProductId,
                  interface->GetInterfaceNumber(),
                  interface->GetInterfaceClass(),
                  interface->GetInterfaceSubClass(),
                  interface->GetInterfaceProtocol());
        
        return interface;
    }
    
    IOLog("%s [%04x:%04x]: No interface could be located.\n", getName(), mVendorId, mProductId);
    
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
        DEBUG_LOG("%s [%04x:%04x]: Located pipe at 0x%02x.\n", getName(), mVendorId, mProductId, pipe->GetEndpointDescriptor()->bEndpointAddress);
        return pipe;
    }
    else
        IOLog("%s [%04x:%04x]: Unable to locate pipe.\n", getName(), mVendorId, mProductId);
    
    return NULL;
}

void BrcmPatchRAM::continousRead()
{
    mReadBuffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, 0x200);
    mReadBuffer->prepare();
    
    mInterruptCompletion =
    {
        .target = this,
        .action = readCompletion,
        .parameter = NULL
    };
    
    IOReturn result;
    
    if ((result = mInterruptPipe->Read(mReadBuffer, 0, 0, mReadBuffer->getLength(), &mInterruptCompletion)) != kIOReturnSuccess)
    {
        if (result != kIOReturnSuccess)
        {
            IOLog("%s [%04x:%04x]: continuousRead - Failed to queue read (0x%08x)\n", getName(), mVendorId, mProductId, result);
            
            if (result == kIOUSBPipeStalled)
            {
                mInterruptPipe->Reset();
                result = mInterruptPipe->Read(mReadBuffer, 0, 0, mReadBuffer->getLength(), &mInterruptCompletion);
                
                if (result != kIOReturnSuccess)
                    IOLog("%s [%04x:%04x]: continuousRead - Failed, read dead (0x%08x)\n", getName(), mVendorId, mProductId, result);
            }
        }
        
    };
}

void BrcmPatchRAM::readCompletion(void* target, void* parameter, IOReturn status, UInt32 bufferSizeRemaining)
{
    BrcmPatchRAM *me = (BrcmPatchRAM*)target;
    
    switch (status)
    {
        case kIOReturnSuccess:
            me->hciParseResponse(me->mReadBuffer->getBytesNoCopy(), me->mReadBuffer->getLength() - bufferSizeRemaining, NULL, NULL);
            break;
        case kIOReturnAborted:
            // Read loop is done, exit silently
            return;
        case kIOReturnNotResponding:
            IOLog("%s [%04x:%04x]: Not responding - Delaying next read.\n", me->getName(), me->mVendorId, me->mProductId);
            me->mInterruptPipe->ClearStall();
            IOSleep(100);
            break;
        default:
            break;
    }
    
    // Queue the next read, only if not aborted
    IOReturn result;
    
    result = me->mInterruptPipe->Read(me->mReadBuffer, 0, 0, me->mReadBuffer->getLength(), &me->mInterruptCompletion);
    
    if (result != kIOReturnSuccess)
    {
        IOLog("%s [%04x:%04x]: readCompletion - Failed to queue next read (0x%08x)\n", me->getName(), me->mVendorId, me->mProductId, result);
        
        if (result == kIOUSBPipeStalled)
        {
            me->mInterruptPipe->ClearStall();
            
            result = me->mInterruptPipe->Read(me->mReadBuffer, 0, 0, me->mReadBuffer->getLength(), &me->mInterruptCompletion);
            
            if (result != kIOReturnSuccess)
                IOLog("%s [%04x:%04x]: readCompletion - Failed, read dead (0x%08x)\n", me->getName(), me->mVendorId, me->mProductId, result);
        }
    }
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
        IOLog("%s [%04x:%04x]: device request failed (0x%08x).\n", getName(), mVendorId, mProductId, result);
   
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
                    DEBUG_LOG("%s [%04x:%04x]: READ VERBOSE CONFIG complete (status: 0x%02x, length: %d bytes).\n",
                              getName(), mVendorId, mProductId, event->status, header->length);
                    
                    mFirmareVersion = *(UInt16*)(((char*)response) + 10);
                    
                    DEBUG_LOG("%s [%04x:%04x]: Firmware version: v%d.\n",
                              getName(), mVendorId, mProductId, mFirmareVersion + 0x1000);
                    
                    mDeviceState = kFirmwareVersion;
                    break;
                case HCI_OPCODE_DOWNLOAD_MINIDRIVER:
                    DEBUG_LOG("%s [%04x:%04x]: DOWNLOAD MINIDRIVER complete (status: 0x%02x, length: %d bytes).\n",
                              getName(), mVendorId, mProductId, event->status, header->length);
                    
                    mDeviceState = kMiniDriverComplete;
                    break;
                case HCI_OPCODE_LAUNCH_RAM:
                    //DEBUG_LOG("%s [%04x:%04x]: LAUNCH RAM complete (status: 0x%02x, length: %d bytes).\n",
                    //          getName(), mVendorId, mProductId, event->status, header->length);
                    
                    mDeviceState = kInstructionWritten;
                    break;
                case HCI_OPCODE_END_OF_RECORD:
                    DEBUG_LOG("%s [%04x:%04x]: END OF RECORD complete (status: 0x%02x, length: %d bytes).\n",
                              getName(), mVendorId, mProductId, event->status, header->length);
                    
                    mDeviceState = kFirmwareWritten;
                    break;
                case HCI_OPCODE_RESET:
                    DEBUG_LOG("%s [%04x:%04x]: RESET complete (status: 0x%02x, length: %d bytes).\n",
                              getName(), mVendorId, mProductId, event->status, header->length);
                    
                    mDeviceState = kResetComplete;
                    break;
                default:
                    DEBUG_LOG("%s [%04x:%04x]: Event COMMAND COMPLETE (opcode 0x%04x, status: 0x%02x, length: %d bytes).\n",
                              getName(), mVendorId, mProductId, event->opcode, event->status, header->length);
                    break;                    
            }
            
            if (output && outputLength)
            {
                bzero(output, *outputLength);
                
                // Return the received data
                if (*outputLength >= length)
                {
                    DEBUG_LOG("%s [%04x:%04x]: Returning output data %d bytes.\n", getName(), mVendorId, mProductId, length);
                    
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
            DEBUG_LOG("%s [%04x:%04x]: Number of completed packets.\n", getName(), mVendorId, mProductId);
            break;
        case HCI_EVENT_CONN_COMPLETE:
            DEBUG_LOG("%s [%04x:%04x]: Connection complete event.\n", getName(), mVendorId, mProductId);
            break;
        case HCI_EVENT_LE_META:
            DEBUG_LOG("%s [%04x:%04x]: Low-Energy meta event.\n", getName(), mVendorId, mProductId);
            break;
        default:
            DEBUG_LOG("%s [%04x:%04x]: Unknown event code (0x%02x).\n", getName(), mVendorId, mProductId, header->eventCode);
            break;
    }
    
    return kIOReturnSuccess;
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
                IOLog("%s [%04x:%04x]: Failed to write to bulk pipe (0x%08x).\n", getName(), mVendorId, mProductId, result);
        }
        else
           IOLog("%s [%04x:%04x]: Failed to prepare bulk write memory buffer (0x%08x).\n", getName(), mVendorId, mProductId, result);
        
        if ((result = buffer->complete()) != kIOReturnSuccess)
            IOLog("%s [%04x:%04x]: Failed to complete bulk write memory buffer (0x%08x).\n", getName(), mVendorId, mProductId, result);
        
        buffer->release();
    }
    else
    {
        IOLog("%s [%04x:%04x]: Unable to allocate bulk write buffer.\n", getName(), mVendorId, mProductId);
        result = kIOReturnNoMemory;
    }

    return result;
}

bool BrcmPatchRAM::performUpgrade()
{
    BrcmFirmwareStore* firmwareStore;
    OSArray* instructions;
    OSCollectionIterator* iterator;
    OSData* data;
    
    mDeviceState = kUnknown;
    
    while (true)
    {
        switch (mDeviceState)
        {
            case kUnknown:
                hciCommand(&HCI_VSC_READ_VERBOSE_CONFIG, sizeof(HCI_VSC_READ_VERBOSE_CONFIG));
                break;
            case kFirmwareVersion:
                // Device does not require a firmware patch at this time
                if (mFirmareVersion > 0)
                    return true;
                
                // Unable to retrieve firmware store
                if (!(firmwareStore = getFirmwareStore()))
                    return false;
                
                instructions = firmwareStore->getFirmware(OSDynamicCast(OSString, getProperty("FirmwareKey")));
                
                // Unable to retrieve firmware instructions
                if (!instructions)
                    return false;
                
                // Initiate firmware upgrade
                hciCommand(&HCI_VSC_DOWNLOAD_MINIDRIVER, sizeof(HCI_VSC_DOWNLOAD_MINIDRIVER));
                
                break;
            case kMiniDriverComplete:
                // Write firmware data to bulk pipe
                iterator = OSCollectionIterator::withCollection(instructions);
                
                if (!iterator)
                    return false;
                
                if ((data = OSDynamicCast(OSData, iterator->getNextObject())))
                    bulkWrite((void *)data->getBytesNoCopy(), data->getLength());
                else
                    return false;
                break;
            case kInstructionWritten:
                if ((data = OSDynamicCast(OSData, iterator->getNextObject())))
                    bulkWrite((void *)data->getBytesNoCopy(), data->getLength());
                else
                    // Firmware data fully written
                    hciCommand(&HCI_VSC_END_OF_RECORD, sizeof(HCI_VSC_END_OF_RECORD));
                break;
            case kFirmwareWritten:
                hciCommand(&HCI_RESET, sizeof(HCI_RESET));
                break;
            case kResetComplete:
                resetDevice();
                getDeviceStatus();
                
                return true;
                break;
        }
        
        IOSleep(1);
    }
}

