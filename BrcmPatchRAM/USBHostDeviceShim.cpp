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

#include "USBDeviceShim.h"
#include "Common.h"

#include <IOKit/usb/IOUSBHostInterface.h>
#include <sys/utfconv.h>

USBDeviceShim::USBDeviceShim()
{
    m_pDevice = NULL;
}

void USBDeviceShim::setDevice(IOService* provider)
{
    OSObject* prev = m_pDevice;
    
    m_pDevice = OSDynamicCast(IOUSBHostDevice, provider);
    
    if (m_pDevice)
        m_pDevice->retain();
    
    if (prev)
        prev->release();
}

UInt16 USBDeviceShim::getVendorID()
{
    return USBToHost16(m_pDevice->getDeviceDescriptor()->idVendor);
}

UInt16 USBDeviceShim::getProductID()
{
    return USBToHost16(m_pDevice->getDeviceDescriptor()->idProduct);
}

OSObject* USBDeviceShim::getProperty(const char* name)
{
    return m_pDevice->getProperty(name);
}

void USBDeviceShim::setProperty(const char* name, bool value)
{
    m_pDevice->setProperty(name, value);
}

void USBDeviceShim::removeProperty(const char* name)
{
    m_pDevice->removeProperty(name);
}

static void hack_strlcpy(char* dest, const char* src, size_t max)
{
    if (!max) return;
    --max;
    while (max && *src)
    {
        *dest++ = *src++;
        --max;
    }
    *dest = 0;
}

IOReturn USBDeviceShim::getStringDescriptor(UInt8 index, char *buf, int maxLen, UInt16 lang)
{
    memset(buf, 0, maxLen);
    
    const StringDescriptor* desc = m_pDevice->getStringDescriptor(index);
    
    if (!desc)
        return kIOReturnBadArgument;
    
    if (desc->bLength <= StandardUSB::kDescriptorSize)
        return kIOReturnBadArgument;
    
    size_t utf8len = 0;
    utf8_encodestr(reinterpret_cast<const u_int16_t*>(desc->bString), desc->bLength - StandardUSB::kDescriptorSize, reinterpret_cast<u_int8_t*>(buf), &utf8len, maxLen, '/', UTF_LITTLE_ENDIAN);
    
    return kIOReturnSuccess;
}

UInt16 USBDeviceShim::getDeviceRelease()
{
    return USBToHost16(m_pDevice->getDeviceDescriptor()->bcdDevice);
}

IOReturn USBDeviceShim::getDeviceStatus(IOService* forClient, USBStatus *status)
{
    uint16_t stat       = 0;
    StandardUSB::DeviceRequest request;
    request.bmRequestType = makeDeviceRequestbmRequestType(kRequestDirectionIn, kRequestTypeStandard, kRequestRecipientDevice);
    request.bRequest      = kDeviceRequestGetStatus;
    request.wValue        = 0;
    request.wIndex        = 0;
    request.wLength       = sizeof(stat);
    uint32_t bytesTransferred = 0;
    IOReturn result = m_pDevice->deviceRequest(forClient, request, &stat, bytesTransferred, kUSBHostStandardRequestCompletionTimeout);
    *status = stat;
    return result;
}

IOReturn USBDeviceShim::resetDevice()
{
    // Setting configuration value 0 (unconfigured) releases all opened interfaces / pipes
    m_pDevice->setConfiguration(0);
    //m_pDevice->reset();
    return kIOReturnSuccess;
}

UInt8 USBDeviceShim::getNumConfigurations()
{
    return m_pDevice->getDeviceDescriptor()->bNumConfigurations;
}

const USBCONFIGURATIONDESCRIPTOR* USBDeviceShim::getFullConfigurationDescriptor(UInt8 configIndex)
{
    return m_pDevice->getConfigurationDescriptor(configIndex);
}

IOReturn USBDeviceShim::getConfiguration(IOService* forClient, UInt8 *configNumber)
{
    uint8_t config  = 0;
    StandardUSB::DeviceRequest request;
    request.bmRequestType = makeDeviceRequestbmRequestType(kRequestDirectionIn, kRequestTypeStandard, kRequestRecipientDevice);
    request.bRequest      = kDeviceRequestGetConfiguration;
    request.wValue        = 0;
    request.wIndex        = 0;
    request.wLength       = sizeof(config);
    uint32_t bytesTransferred = 0;
    IOReturn result = m_pDevice->deviceRequest(forClient, request, &config, bytesTransferred, kUSBHostStandardRequestCompletionTimeout);
    *configNumber = config;
    return result;
}

IOReturn USBDeviceShim::setConfiguration(IOService *forClient, UInt8 configValue, bool startInterfaceMatching)
{
    return m_pDevice->setConfiguration(configValue, startInterfaceMatching);
}

bool USBDeviceShim::findFirstInterface(USBInterfaceShim* shim)
{
    DebugLog("USBDeviceShim::findFirstInterface\n");
    
    OSIterator* iterator = m_pDevice->getChildIterator(gIOServicePlane);
    
    if (!iterator)
        return false;
    
    while (OSObject* candidate = iterator->getNextObject())
    {
        if (IOUSBHostInterface* interface = OSDynamicCast(IOUSBHostInterface, candidate))
        {
            //if (interface->getInterfaceDescriptor()->bInterfaceClass != kUSBHubClass)
            {
                shim->setInterface(interface);
                break;
            }
        }
    }
    
    iterator->release();

    DebugLog("getValidatedInterface returns %p\n", shim->getValidatedInterface());
    return shim->getValidatedInterface() != NULL;
}

bool USBDeviceShim::open(IOService *forClient, IOOptionBits options, void *arg)
{
    return m_pDevice->open(forClient, options, arg);
}

void USBDeviceShim::close(IOService *forClient, IOOptionBits options)
{
    return m_pDevice->close(forClient, options);
}

UInt8 USBDeviceShim::getManufacturerStringIndex()
{
    return m_pDevice->getDeviceDescriptor()->iManufacturer;
}

UInt8 USBDeviceShim::getProductStringIndex()
{
    return m_pDevice->getDeviceDescriptor()->iProduct;
}

UInt8 USBDeviceShim::getSerialNumberStringIndex()
{
    return m_pDevice->getDeviceDescriptor()->iSerialNumber;
}

USBInterfaceShim::USBInterfaceShim()
{
    m_pInterface = NULL;
}

void USBInterfaceShim::setInterface(IOService* interface)
{
    OSObject* prev = m_pInterface;

    m_pInterface = OSDynamicCast(IOUSBHostInterface, interface);

    if (m_pInterface)
        m_pInterface->retain();
    if (prev)
        prev->release();
}

bool USBInterfaceShim::open(IOService *forClient, IOOptionBits options, void *arg)
{
    bool result = m_pInterface->open(forClient, options, arg);
    if (!result)
        AlwaysLog("USBInterfaceShim:open failed\n");
    return result;
}

void USBInterfaceShim::close(IOService *forClient, IOOptionBits options)
{
    m_pInterface->close(forClient, options);
}

#ifdef DEBUG
UInt8 USBInterfaceShim::getInterfaceNumber()
{
    return m_pInterface->getInterfaceDescriptor()->bInterfaceNumber;
}

UInt8 USBInterfaceShim::getInterfaceClass()
{
    return m_pInterface->getInterfaceDescriptor()->bInterfaceClass;
}

UInt8 USBInterfaceShim::getInterfaceSubClass()
{
    return m_pInterface->getInterfaceDescriptor()->bInterfaceSubClass;
}

UInt8 USBInterfaceShim::getInterfaceProtocol()
{
    return m_pInterface->getInterfaceDescriptor()->bInterfaceProtocol;
}
#endif

bool USBInterfaceShim::findPipe(USBPipeShim* shim, UInt8 type, UInt8 direction)
{
    // virtual IOUSBHostPipe* FindNextPipe(IOUSBHostPipe* current, IOUSBFindEndpointRequest* request) __attribute__((deprecated));
    // virtual IOUSBHostPipe* FindNextPipe(IOUSBHostPipe* current, IOUSBFindEndpointRequest* request, bool withRetain) __attribute__((deprecated));
    // Replacement: getInterfaceDescriptor and StandardUSB::getNextAssociatedDescriptorWithType to find an endpoint descriptor,
    // then use copyPipe to retrieve the pipe object
    //    virtual const StandardUSB::InterfaceDescriptor* getInterfaceDescriptor();
    //    const Descriptor* getNextAssociatedDescriptorWithType(const ConfigurationDescriptor* configurationDescriptor, const Descriptor* parentDescriptor, const Descriptor* currentDescriptor, const uint8_t type);
    
    //
    /*!
     * @brief Return the configuration descriptor in which this interface is defined
     *
     * @return Pointer to the configuration descriptor
     */
    //virtual const StandardUSB::ConfigurationDescriptor* getConfigurationDescriptor();
    
    /*!
     * @brief Return the pipe whose <code>bEndpointAddress</code> matches <code>address</code>
     *
     * @discussion This method will return the pipe whose <code>bEndpointAddress</code> matches <code>address</code>.  If
     * the pipe doesn't exist yet, but is part of the interface, it will first be created.  This method returns a
     * <code>retain()</code>ed object that must be <code>release()</code>ed by the caller.
     *
     * @param address Address of the pipe
     *
     * @return Pointer to a retain()ed IOUSBHostPipe object or NULL
     */
    //virtual IOUSBHostPipe* copyPipe(uint8_t address);
    
    // USB 2.0 9.5: Descriptors
    //struct Descriptor
    //{
    //    uint8_t bLength;
    //    uint8_t bDescriptorType;
    //} __attribute__((packed));
    //
    //typedef struct Descriptor Descriptor;
    
    // USB 2.0 9.6.5: Interface
    //struct InterfaceDescriptor : public Descriptor
    //{
    //    uint8_t     bInterfaceNumber;
    //    uint8_t     bAlternateSetting;
    //    uint8_t     bNumEndpoints;
    //    uint8_t     bInterfaceClass;
    //    uint8_t     bInterfaceSubClass;
    //    uint8_t     bInterfaceProtocol;
    //    uint8_t     iInterface;
    //} __attribute__((packed));
    //typedef struct InterfaceDescriptor InterfaceDescriptor;
    
    // USB 2.0 9.6.6: Endpoint
    //struct EndpointDescriptor : public Descriptor
    //{
    //    uint8_t     bEndpointAddress;
    //    uint8_t     bmAttributes;
    //    uint16_t    wMaxPacketSize;
    //    uint8_t     bInterval;
    //} __attribute__((packed));
    //typedef struct EndpointDescriptor EndpointDescriptor;
    
    //const EndpointDescriptor* getNextEndpointDescriptor(const ConfigurationDescriptor* configurationDescriptor, const InterfaceDescriptor* interfaceDescriptor, const Descriptor* currentDescriptor);

    DebugLog("findPipe: direction = %d, type = %d\n", direction, type);
    const StandardUSB::ConfigurationDescriptor* configDesc = m_pInterface->getConfigurationDescriptor();
    const StandardUSB::InterfaceDescriptor* ifaceDesc = m_pInterface->getInterfaceDescriptor();
    if (!configDesc || !ifaceDesc)
    {
        DebugLog("configDesc = %p, ifaceDesc = %p\n", configDesc, ifaceDesc);
        return false;
    }
    const EndpointDescriptor* ep = NULL;
    while ((ep = StandardUSB::getNextEndpointDescriptor(configDesc, ifaceDesc, ep)))
    {
        // check if endpoint matches type and direction
        uint8_t epDirection = StandardUSB::getEndpointDirection(ep);
        uint8_t epType = StandardUSB::getEndpointType(ep);
        DebugLog("endpoint found: epDirection = %d, epType = %d\n", epDirection, epType);
        if (direction == epDirection && type == epType)
        {
            DebugLog("found matching endpoint\n");
            
            // matches... try to make a pipe from the endpoint address
            IOUSBHostPipe* pipe = m_pInterface->copyPipe(StandardUSB::getEndpointAddress(ep));
            if (pipe == NULL)
            {
                DebugLog("copyPipe failed\n");
                return false;
            }
            
            // set it in the shim
            shim->setPipe(pipe);
            pipe->release();
            return true;
        }
    }
    DebugLog("findPipe: no matching endpoint found");
    return false;
}

IOReturn USBInterfaceShim::hciCommand(void* command, UInt16 length)
{
    StandardUSB::DeviceRequest request =
    {
        .bmRequestType = makeDeviceRequestbmRequestType(kRequestDirectionOut, kRequestTypeClass, kRequestRecipientDevice),
        .bRequest = 0,
        .wValue = 0,
        .wIndex = 0,
        .wLength = length
    };
    
    uint32_t bytesTransfered;
    return m_pInterface->deviceRequest(request, command, bytesTransfered, 0);
}

USBPipeShim::USBPipeShim()
{
    m_pPipe = NULL;
}

void USBPipeShim::setPipe(OSObject* pipe)
{
    OSObject* prev = m_pPipe;
    m_pPipe = OSDynamicCast(IOUSBHostPipe, pipe);

    if (m_pPipe)
        m_pPipe->retain();
    if (prev)
        prev->release();
}

IOReturn USBPipeShim::abort(void)
{
    return m_pPipe->abort();
}

/*!
 * @brief Issue an asynchronous I/O request
 *
 * @discussion See IOUSBHostIOSource::io for documentation
 *
 * @param completionTimeoutMs Must be 0 for interrupt endpoints.
 */
//virtual IOReturn io(IOMemoryDescriptor* dataBuffer, uint32_t dataBufferLength, IOUSBHostCompletion* completion, uint32_t completionTimeoutMs = 0);

/*!
 * @brief Issue a synchronous I/O request
 *
 * @discussion See IOUSBHostIOSource::io for documentation
 *
 * @param completionTimeoutMs Must be 0 for interrupt endpoints.
 */
//virtual IOReturn io(IOMemoryDescriptor* dataBuffer, uint32_t dataBufferLength, uint32_t& bytesTransferred, uint32_t completionTimeoutMs = 0);

IOReturn USBPipeShim::read(IOMemoryDescriptor *	buffer,
                           UInt32		noDataTimeout,
                           UInt32		completionTimeout,
                           IOByteCount		reqCount,
                           USBCOMPLETION *	completion,
                           IOByteCount *		bytesRead)
{
    IOReturn result;
    if (completion)
        result = m_pPipe->io(buffer, (uint32_t)reqCount, completion, completionTimeout);
    else
    {
        uint32_t bytesTransfered;
        result = m_pPipe->io(buffer, (uint32_t)reqCount, bytesTransfered, completionTimeout);
        if (bytesRead) *bytesRead = bytesTransfered;
    }
    return result;
}

IOReturn USBPipeShim::write(IOMemoryDescriptor *	buffer,
                            UInt32		noDataTimeout,
                            UInt32		completionTimeout,
                            IOByteCount		reqCount,
                            USBCOMPLETION *	completion)
{
    IOReturn result;
    if (completion)
        result = m_pPipe->io(buffer, (uint32_t)reqCount, completion, completionTimeout);
    else
    {
        uint32_t bytesTransfered;
        result = m_pPipe->io(buffer, (uint32_t)reqCount, bytesTransfered, completionTimeout);
    }
    return result;
}

const USBENDPOINTDESCRIPTOR* USBPipeShim::getEndpointDescriptor()
{
    return m_pPipe->getEndpointDescriptor();
}

IOReturn USBPipeShim::clearStall()
{
    return m_pPipe->clearStall(false);
}
