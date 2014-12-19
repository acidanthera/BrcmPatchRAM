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

/******************************************************************************
 * BrcmPatchRAM::probe - parse kernel extension Info.plist
 ******************************************************************************/
 IOService* BrcmPatchRAM::probe(IOService *provider, SInt32 *probeScore)
 {
     DEBUG_LOG("%s::probe\n", this->getName());
     return super::probe(provider, probeScore);
 }

/******************************************************************************
 * BrcmPatchRAM::init - parse kernel extension Info.plist
 ******************************************************************************/
bool BrcmPatchRAM::init(OSDictionary *dictionary)
{    
    DEBUG_LOG("BrcmPatchRAM::init\n"); // this->getName() is not available yet
    return super::init(dictionary);
}

/******************************************************************************
 * BrcmPatchRAM::start - start kernel extension
 ******************************************************************************/
bool BrcmPatchRAM::start(IOService *provider)
{
    BrcmFirmwareStore* firmwareStore;
    
    IOLog("%s: Version 0.6 starting.\n", this->getName());

    if (!super::start(provider))
        return false;
    
    // Cast to IOUSBDevice
    mDevice = OSDynamicCast(IOUSBDevice, provider);
    
    if (!mDevice)
    {
        IOLog("%s: Provider is not a USB device.\n", this->getName());
        return false;
    }
    
    // Print out additional device information
    printDeviceInfo();
    
    // Set device configuration to composite configuration index 0
    if (!setConfiguration(0))
        return false;

    // Obtain first interface
    getInterface();
    
    if (mInterface != NULL)
    {
        mInterface->retain();
        mInterface->open(this);
        
        getInterruptPipe();
        getBulkPipe();
        
        if (mInterruptPipe != NULL && mBulkPipe != NULL)
        {
            // getFirmwareVersion additionally re-synchronizes outstanding responses
            UInt16 firmwareVersion = getFirmwareVersion();
            
            //IOLog("BrcmPatchRAM: Current firmware version v%d.\n", firmwareVersion);
            
            if (firmwareVersion == 0 && (firmwareStore = getFirmwareStore()) != NULL)
            {
                OSArray* instructions = firmwareStore->getFirmware(OSDynamicCast(OSString, getProperty("FirmwareKey")));
                
                if (instructions != NULL)
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
                    
                    IOLog("%s: Firmware upgrade completed successfully.\n", this->getName());
                }
            }
        }
    }
    
    if (mInterruptPipe != NULL)
    {
        mInterruptPipe->Abort();
        mInterruptPipe->release();
    }
    
    if (mBulkPipe != NULL)
    {
        mBulkPipe->Abort();
        mBulkPipe->release();
    }
    
    if (mInterface != NULL)
    {
        mInterface->close(this);
        mInterface->release();
    }

    return false;
}

/******************************************************************************
 * BrcmPatchRAM::stop & free - stop and free kernel extension
 ******************************************************************************/
void BrcmPatchRAM::stop(IOService *provider)
{
    DEBUG_LOG("%s: Stopping...\n", this->getName());  
    super::stop(provider);
}

/******************************************************************************
 * BrcmPatchRAM::getFirmwareStore - Obtain referenced to the firmware data store
 ******************************************************************************/
BrcmFirmwareStore* BrcmPatchRAM::getFirmwareStore()
{
    BrcmFirmwareStore* firmwareStore = OSDynamicCast(BrcmFirmwareStore, this->getResourceService()->getProperty(kBrcmFirmwareStoreService));
    
    if (firmwareStore == NULL)
        IOLog("%s: BrcmFirmwareStore does not appear to be available.\n", this->getName());
    
    return firmwareStore;
}

/******************************************************************************
 * BrcmPatchRAM::printDeviceInfo - Print USB device information
 ******************************************************************************/
void BrcmPatchRAM::printDeviceInfo()
{
    char product[255];
    char manufacturer[255];
    char serial[255];
    
    // Retrieve device information
    mDevice->GetStringDescriptor(mDevice->GetProductStringIndex(), product, sizeof(product));
    mDevice->GetStringDescriptor(mDevice->GetManufacturerStringIndex(), manufacturer, sizeof(manufacturer));
    mDevice->GetStringDescriptor(mDevice->GetSerialNumberStringIndex(), serial, sizeof(serial));
    
    IOLog("%s: USB [%04x:%04x %s v%d] \"%s\" by \"%s\"\n",
          this->getName(),
          mDevice->GetVendorID(),
          mDevice->GetProductID(),
          serial,
          mDevice->GetDeviceRelease(),
          product,
          manufacturer);
}

/******************************************************************************
 * BrcmPatchRAM::getDeviceStatus - Get USB device status
 ******************************************************************************/
int BrcmPatchRAM::getDeviceStatus()
{
    IOReturn result;
    USBStatus status;
    
    if ((result = mDevice->GetDeviceStatus(&status)) != kIOReturnSuccess)
    {
        IOLog("%s: Unable to get device status (0x%08x).\n", this->getName(), result);
        return 0;
    }
    else DEBUG_LOG("%s: Device status 0x%08x.\n", this->getName(), (int)status);
    
    return (int)status;
}

/******************************************************************************
 * BrcmPatchRAM::resetDevice - Reset USB device
 ******************************************************************************/
bool BrcmPatchRAM::resetDevice()
{
    IOReturn result;
    
    if ((result = mDevice->ResetDevice()) != kIOReturnSuccess)
    {
        IOLog("%s: Failed to reset the device (0x%08x).\n", this->getName(), result);
        return false;
    }
    else
        DEBUG_LOG("%s: Device reset.\n", this->getName());
    
    return true;
}

/******************************************************************************
 * BrcmPatchRAM::setConfiguration - Set USB device composite configuration
 ******************************************************************************/
bool BrcmPatchRAM::setConfiguration(int configurationIndex)
{
    IOReturn result;
    const IOUSBConfigurationDescriptor* configurationDescriptor;
    UInt8 currentConfiguration = 0xFF;
    
    // Find the first config/interface
    UInt8 numconf = 0;
    
    if ((numconf = mDevice->GetNumConfigurations()) < (configurationIndex + 1))
    {
        IOLog("%s: Composite configuration index %d is not available, %d total composite configurations.\n",
              this->getName(), configurationIndex, numconf);
        return false;
    }
    else
        DEBUG_LOG("%s: Available composite configurations: %d.\n", this->getName(), numconf);
    
    configurationDescriptor = mDevice->GetFullConfigurationDescriptor(configurationIndex);
    
    // Set the configuration to the requested configuration index
    if (!configurationDescriptor)
    {
        IOLog("%s: No configuration descriptor for configuration index: %d.\n", this->getName(), configurationIndex);
        return false;
    }
    
    if ((result = mDevice->GetConfiguration(&currentConfiguration)) != kIOReturnSuccess)
    {
        IOLog("%s: Unable to retrieve active configuration (0x%08x).\n", this->getName(), result);
        return false;
    }
    
    // Device is already configured
    if (currentConfiguration == configurationDescriptor->bConfigurationValue)
    {
        DEBUG_LOG("%s: Device configuration is already set to configuration index %d.\n", this->getName(), configurationIndex);
        return true;
    }
    
    if (!mDevice->open(this))
    {
        IOLog("%s: Unable to open device for (re-)configuration.\n", this->getName());
        return false;
    }
    
    // Set the configuration to the first configuration
    if ((result = mDevice->SetConfiguration(this, configurationDescriptor->bConfigurationValue, true)) != kIOReturnSuccess)
    {
        IOLog("%s: Unable to (re-)configure device (0x%08x).\n", this->getName(), result);
        mDevice->close(this);
        return false;
    }
    
    DEBUG_LOG("%s: Set device configuration to configuration index %d successfully.\n", this->getName(), configurationIndex);
    mDevice->close(this);
    
    return true;
}

/******************************************************************************
 * BrcmPatchRAM::getInterface
 ******************************************************************************/
void BrcmPatchRAM::getInterface()
{
    IOUSBFindInterfaceRequest request;
    IOUSBInterface* interface = NULL;
    
    // Find the interface for bulk endpoint transfers
    request.bAlternateSetting  = kIOUSBFindInterfaceDontCare;
    request.bInterfaceClass    = kIOUSBFindInterfaceDontCare;
    request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
    request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    
    if ((interface = mDevice->FindNextInterface(NULL, &request)) != NULL)
    {
        DEBUG_LOG("%s: Interface %d (class %02x, subclass %02x, protocol %02x) located.\n",
                  this->getName(),
                  interface->GetInterfaceNumber(),
                  interface->GetInterfaceClass(),
                  interface->GetInterfaceSubClass(),
                  interface->GetInterfaceProtocol());
        
        mInterface = interface;
        return;
    }
    
    IOLog("%s: No interface could be located.\n", this->getName());
}

/******************************************************************************
 * BrcmPatchRAM::getInterruptPipe
 ******************************************************************************/
void BrcmPatchRAM::getInterruptPipe()
{
    IOUSBFindEndpointRequest findEndpointRequest;
    
    findEndpointRequest.direction = kUSBIn;
    findEndpointRequest.type = kUSBInterrupt;
    
    IOUSBPipe* pipe = mInterface->FindNextPipe(NULL, &findEndpointRequest, true);
    
    if (pipe != NULL)
    {
        const IOUSBEndpointDescriptor* endpointDescriptor = pipe->GetEndpointDescriptor();
        DEBUG_LOG("%s: Located interrupt pipe at 0x%02x.\n", this->getName(), endpointDescriptor->bEndpointAddress);
        
        mInterruptPipe = pipe;
    }
    else
        IOLog("%s: Unable to locate interrupt pipe.\n", this->getName());
}

/******************************************************************************
 * BrcmPatchRAM::getBulkPipe
 ******************************************************************************/
void BrcmPatchRAM::getBulkPipe()
{
    IOUSBFindEndpointRequest findEndpointRequest;
    
    findEndpointRequest.direction = kUSBOut;
    findEndpointRequest.type = kUSBBulk;
    
    IOUSBPipe* pipe = mInterface->FindNextPipe(NULL, &findEndpointRequest, true);
    
    if (pipe != NULL)
    {
        const IOUSBEndpointDescriptor* endpointDescriptor = pipe->GetEndpointDescriptor();
        DEBUG_LOG("%s: Located bulk pipe at 0x%02x.\n", this->getName(), endpointDescriptor->bEndpointAddress);
        
        mBulkPipe = pipe;
    }
    else
        IOLog("%s: Unable to locate bulk pipe.\n", this->getName());
}

IOReturn BrcmPatchRAM::queueRead()
{
    IOReturn result;
    
    IOBufferMemoryDescriptor* buffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, 0x200);
    
    if (buffer != NULL)
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
                IOLog("%s: Error initiating read (0x%08x).\n", this->getName(), result);
            }
            else
            {
                // Wait until read is complete
                while (mReadQueued)
                {
                    IOSleep(1);
                }
            }
            
            if ((result = buffer->complete()) != kIOReturnSuccess)
             IOLog("%s: Failed to complete queued read memory buffer (0x%08x).\n", this->getName(), result);
        }
        else
            IOLog("%s: Failed to prepare queued read memory buffer (0x%08x).\n", this->getName(), result);
        
        buffer->release();
    }
    else
    {
        IOLog("BrcmPatchRAM: Unable to allocate read buffer.\n");
        result = kIOReturnNoMemory;
    }

    return result;
}

void BrcmPatchRAM::interruptReadEntry(void* target, void* parameter, IOReturn status, UInt32 bufferSizeRemaining)
{
    if (target != NULL)
        ((BrcmPatchRAM*)target)->interruptReadHandler(parameter, status, bufferSizeRemaining);
}

void BrcmPatchRAM::interruptReadHandler(void* parameter, IOReturn status, UInt32 bufferSizeRemaining)
{
    IOBufferMemoryDescriptor* buffer = (IOBufferMemoryDescriptor*)parameter;
    
    if (buffer == NULL)
    {
        IOLog("%s: Queued read, buffer is NULL.\n", this->getName());
        return;
    }
    
    switch (status)
    {
        case kIOReturnOverrun:
            DEBUG_LOG("%s: read - kIOReturnOverrun\n", this->getName());
            mInterruptPipe->ClearStall();
        case kIOReturnSuccess:
        {
            hciParseResponse(buffer->getBytesNoCopy(), buffer->getLength() - bufferSizeRemaining, NULL, NULL);
            break;
        }
        case kIOReturnNotResponding:
            DEBUG_LOG("%s: read - kIOReturnNotResponding\n", this->getName());
            break;
        default:
            DEBUG_LOG("%s: read - Other (0x%08x)\n", this->getName(), status);
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
        IOLog("%s: device request failed (0x%08x).\n", this->getName(), result);
   
    return result;
}

IOReturn BrcmPatchRAM::hciCommandSync(void* command, UInt16 length)
{
    return this->hciCommandSync(command, length, NULL, NULL);
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
                    DEBUG_LOG("%s: READ VERBOSE CONFIG complete (status: 0x%02x, length: %d bytes).\n",
                              this->getName(), event->status, header->length);
                    
                    DEBUG_LOG("%s: Firmware version: v%d.\n",
                              this->getName(), (*(UInt16*)(((char*)response) + 10)) + 4096);
                    break;
                case HCI_OPCODE_DOWNLOAD_MINIDRIVER:
                    DEBUG_LOG("%s: DOWNLOAD MINIDRIVER complete (status: 0x%02x, length: %d bytes).\n",
                              this->getName(), event->status, header->length);
                    break;
                case HCI_OPCODE_LAUNCH_RAM:
                    //DEBUG_LOG("%s: LAUNCH RAM complete (status: 0x%02x, length: %d bytes).\n",
                    //          this->getName(), event->status, header->length);
                    break;
                case HCI_OPCODE_END_OF_RECORD:
                    DEBUG_LOG("%s: END OF RECORD complete (status: 0x%02x, length: %d bytes).\n",
                              this->getName(), event->status, header->length);
                    break;
                case HCI_OPCODE_RESET:
                    DEBUG_LOG("%s: RESET complete (status: 0x%02x, length: %d bytes).\n",
                              this->getName(), event->status, header->length);
                    break;
                default:
                    DEBUG_LOG("%s: Event COMMAND COMPLETE (opcode 0x%04x, status: 0x%02x, length: %d bytes).\n",
                              this->getName(), event->opcode, event->status, header->length);
                    break;                    
            }
            
            if (output != NULL && outputLength != NULL)
            {
                bzero(output, *outputLength);
                
                // Return the received data
                if (*outputLength >= length)
                {
                    DEBUG_LOG("%s: Returning output data %d bytes.\n", this->getName(), length);
                    
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
            DEBUG_LOG("%s: Number of completed packets.\n", this->getName());
            break;
        case HCI_EVENT_CONN_COMPLETE:
            DEBUG_LOG("%s: Connection complete event.\n", this->getName());
            break;
        case HCI_EVENT_LE_META:
            DEBUG_LOG("%s: Low-Energy meta event.\n", this->getName());
            break;
        default:
            DEBUG_LOG("%s: Unknown event code (0x%02x).\n", this->getName(), header->eventCode);
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
    
    if (buffer != NULL)
    {
        if ((result = buffer->prepare()) == kIOReturnSuccess)
        {
            IOByteCount reqCount = buffer->getLength();
            IOByteCount readCount;
            
            if ((result = mInterruptPipe->Read(buffer, 0, 0, reqCount, (IOUSBCompletion*)NULL, &readCount)) == kIOReturnSuccess)
                result = hciParseResponse(buffer->getBytesNoCopy(), readCount, output, length);
            else
                IOLog("%s: Failed to read from interrupt pipe sychronously (0x%08x).\n", this->getName(), result);
        }
        else
            IOLog("%s: Failed to prepare interrupt read memory buffer (0x%08x).\n", this->getName(), result);
        
        if ((result = buffer->complete()) != kIOReturnSuccess)
            IOLog("%s: Failed to complete interrupt read memory buffer (0x%08x).\n", this->getName(), result);
        
        buffer->release();
    }
    else
        result = kIOReturnNoMemory;
  
    return result;
}

IOReturn BrcmPatchRAM::bulkWrite(void* data, UInt16 length)
{
    IOReturn result;
    IOMemoryDescriptor* buffer = IOMemoryDescriptor::withAddress(data, length, kIODirectionIn);
    
    if (buffer != NULL)
    {
        if ((result = buffer->prepare()) == kIOReturnSuccess)
        {
            if ((result = mBulkPipe->Write(buffer, 0, 0, buffer->getLength(), (IOUSBCompletion*)NULL)) == kIOReturnSuccess)
            {
                //DEBUG_LOG("%s: Wrote %d bytes to bulk pipe.\n", this->getName(), length);
            }
            else
                IOLog("%s: Failed to write to bulk pipe (0x%08x).\n", this->getName(), result);
        }
        else
           IOLog("%s: Failed to prepare bulk write memory buffer (0x%08x).\n", this->getName(), result);
        
        if ((result = buffer->complete()) != kIOReturnSuccess)
            IOLog("%s: Failed to complete bulk write memory buffer (0x%08x).\n", this->getName(), result);
        
        buffer->release();
    }
    else
        result = kIOReturnNoMemory;
    
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


