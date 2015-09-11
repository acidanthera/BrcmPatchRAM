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

#ifndef TARGET_ELCAPITAN
#include <IOKit/usb/IOUSBInterface.h>
#else
#include <IOKit/usb/IOUSBHostFamily.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/USBSpec.h>
#include <IOKit/usb/USB.h>
#include <sys/utfconv.h>
#endif

USBDeviceShim::USBDeviceShim()
{
    m_pDevice = NULL;
}

void USBDeviceShim::setDevice(IOService* provider)
{
    OSObject* prev = m_pDevice;
    
#ifndef TARGET_ELCAPITAN
    m_pDevice = OSDynamicCast(IOUSBDevice, provider);
#else
    m_pDevice = OSDynamicCast(IOUSBHostDevice, provider);
#endif
    
    if (m_pDevice)
        m_pDevice->retain();
    
    if (prev)
        prev->release();
}

UInt16 USBDeviceShim::getVendorID()
{
#ifndef TARGET_ELCAPITAN
    return m_pDevice->GetVendorID();
#else
    return USBToHost16(m_pDevice->getDeviceDescriptor()->idVendor);
#endif
}

UInt16 USBDeviceShim::getProductID()
{
#ifndef TARGET_ELCAPITAN
    return m_pDevice->GetProductID();
#else
    return USBToHost16(m_pDevice->getDeviceDescriptor()->idProduct);
#endif
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
#ifndef TARGET_ELCAPITAN
    return m_pDevice->GetStringDescriptor(index, buf, maxLen, lang);
#else
    memset(buf, 0, maxLen);
    
    const StringDescriptor* desc = m_pDevice->getStringDescriptor(index);
    
    if (!desc)
        return kIOReturnBadArgument;
    
    if (desc->bLength <= StandardUSB::kDescriptorSize)
        return kIOReturnBadArgument;
    
    size_t utf8len = 0;
    utf8_encodestr(reinterpret_cast<const u_int16_t*>(desc->bString), desc->bLength - StandardUSB::kDescriptorSize, reinterpret_cast<u_int8_t*>(buf), &utf8len, maxLen, '/', UTF_LITTLE_ENDIAN);

    return kIOReturnSuccess;
#endif
}

UInt16 USBDeviceShim::getDeviceRelease()
{
#ifndef TARGET_ELCAPITAN
    return m_pDevice->GetDeviceRelease();
#else
    return USBToHost16(m_pDevice->getDeviceDescriptor()->bcdDevice);
#endif
}

IOReturn USBDeviceShim::getDeviceStatus(IOService* forClient, USBStatus *status)
{
#ifndef TARGET_ELCAPITAN
    return m_pDevice->GetDeviceStatus(status);
#else
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
#endif
}

IOReturn USBDeviceShim::resetDevice()
{
#ifndef TARGET_ELCAPITAN
    return m_pDevice->ResetDevice();
#else
    //REVIEW: no equivalent in 10.11.
    return kIOReturnSuccess;
#endif
}

UInt8 USBDeviceShim::getNumConfigurations()
{
#ifndef TARGET_ELCAPITAN
    return m_pDevice->GetNumConfigurations();
#else
    return m_pDevice->getDeviceDescriptor()->bNumConfigurations;
#endif
}

const USBCONFIGURATIONDESCRIPTOR* USBDeviceShim::getFullConfigurationDescriptor(UInt8 configIndex)
{
#ifndef TARGET_ELCAPITAN
    return m_pDevice->GetFullConfigurationDescriptor(configIndex);
#else
    return m_pDevice->getConfigurationDescriptor(configIndex);
#endif
}

IOReturn USBDeviceShim::getConfiguration(IOService* forClient, UInt8 *configNumber)
{
#ifndef TARGET_ELCAPITAN
    return m_pDevice->GetConfiguration(configNumber);
#else
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
#endif
}

IOReturn USBDeviceShim::setConfiguration(IOService *forClient, UInt8 configValue, bool startInterfaceMatching)
{
#ifndef TARGET_ELCAPITAN
    return m_pDevice->SetConfiguration(forClient, configValue, startInterfaceMatching);
#else
    return m_pDevice->setConfiguration(configValue, startInterfaceMatching);
#endif
}

bool USBDeviceShim::findFirstInterface(USBInterfaceShim* shim)
{
    DebugLog("USBDeviceShim::findFirstInterface\n");
#ifndef TARGET_ELCAPITAN
    IOUSBFindInterfaceRequest request;
    request.bAlternateSetting  = kIOUSBFindInterfaceDontCare;
    request.bInterfaceClass    = kIOUSBFindInterfaceDontCare;
    request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
    request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    IOUSBInterface* interface = m_pDevice->FindNextInterface(NULL, &request);
    DebugLog("FindNextInterface returns %p\n", interface);
    shim->setInterface(interface);
#else
    OSIterator* iterator = m_pDevice->getChildIterator(gIOServicePlane);
    if (!iterator)
        return false;
    while (OSObject* candidate = iterator->getNextObject())
    {
        if (IOUSBHostInterface* interface = OSDynamicCast(IOUSBHostInterface, candidate))
        {
            //REVIEW: kUSBHubClass not found. docs wrong.
            //if (interface->getInterfaceDescriptor()->bInterfaceClass == kUSBHubClass)
            {
                shim->setInterface(interface);
                break;
            }
        }
    }
    iterator->release();
#endif
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
#ifndef TARGET_ELCAPITAN
    return m_pDevice->GetManufacturerStringIndex();
#else
    return m_pDevice->getDeviceDescriptor()->iManufacturer;
#endif
}

UInt8 USBDeviceShim::getProductStringIndex()
{
#ifndef TARGET_ELCAPITAN
    return m_pDevice->GetProductStringIndex();
#else
    return m_pDevice->getDeviceDescriptor()->iProduct;
#endif
}
UInt8 USBDeviceShim::getSerialNumberStringIndex()
{
#ifndef TARGET_ELCAPITAN
    return m_pDevice->GetSerialNumberStringIndex();
#else
    return m_pDevice->getDeviceDescriptor()->iSerialNumber;
#endif
}

USBInterfaceShim::USBInterfaceShim()
{
    m_pInterface = NULL;
}

void USBInterfaceShim::setInterface(IOService* interface)
{
    OSObject* prev = m_pInterface;
#ifndef TARGET_ELCAPITAN
    m_pInterface = OSDynamicCast(IOUSBInterface, interface);
#else
    m_pInterface = OSDynamicCast(IOUSBHostInterface, interface);
#endif
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
#ifndef TARGET_ELCAPITAN
    return m_pInterface->GetInterfaceNumber();
#else
    return m_pInterface->getInterfaceDescriptor()->bInterfaceNumber;
#endif
}

UInt8 USBInterfaceShim::getInterfaceClass()
{
#ifndef TARGET_ELCAPITAN
    return m_pInterface->GetInterfaceClass();
#else
    return m_pInterface->getInterfaceDescriptor()->bInterfaceClass;
#endif
}

UInt8 USBInterfaceShim::getInterfaceSubClass()
{
#ifndef TARGET_ELCAPITAN
    return m_pInterface->GetInterfaceSubClass();
#else
    return m_pInterface->getInterfaceDescriptor()->bInterfaceSubClass;
#endif
}

UInt8 USBInterfaceShim::getInterfaceProtocol()
{
#ifndef TARGET_ELCAPITAN
    return m_pInterface->GetInterfaceProtocol();
#else
    return m_pInterface->getInterfaceDescriptor()->bInterfaceProtocol;
#endif
}

#endif

bool USBInterfaceShim::findPipe(USBPipeShim* shim, UInt8 type, UInt8 direction)
{
#ifndef TARGET_ELCAPITAN
    IOUSBFindEndpointRequest findEndpointRequest;
    findEndpointRequest.type = type;
    findEndpointRequest.direction = direction;
    if (IOUSBPipe* pipe = m_pInterface->FindNextPipe(NULL, &findEndpointRequest))
    {
        shim->setPipe(pipe);
        return true;
    }
    return false;
#else
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
    
    //TODO:
    
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
#endif
}

IOReturn USBInterfaceShim::hciCommand(void* command, UInt16 length)
{
#ifndef TARGET_ELCAPITAN
    IOUSBDevRequest request =
    {
        .bmRequestType = USBmakebmRequestType(kUSBOut, kUSBClass, kUSBDevice),
        .bRequest = 0,
        .wValue = 0,
        .wIndex = 0,
        .wLength = length,
        .pData = command
    };
    return m_pInterface->DeviceRequest(&request);
#else
    StandardUSB::DeviceRequest request =
    {
        //.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBClass, kUSBDevice),
        .bmRequestType = makeDeviceRequestbmRequestType(kRequestDirectionOut, kRequestTypeStandard, kRequestRecipientInterface),
        .bRequest = 0,
        .wValue = 0,
        .wIndex = 0,
        .wLength = length
    };
    uint32_t bytesTransfered;
    return m_pInterface->deviceRequest(request, command, bytesTransfered, 0);
#endif
}

USBPipeShim::USBPipeShim()
{
    m_pPipe = NULL;
}

void USBPipeShim::setPipe(OSObject* pipe)
{
    OSObject* prev = m_pPipe;
#ifndef TARGET_ELCAPITAN
    m_pPipe = OSDynamicCast(IOUSBPipe, pipe);
#else
    m_pPipe = OSDynamicCast(IOUSBHostPipe, pipe);
#endif
    if (m_pPipe)
        m_pPipe->retain();
    if (prev)
        prev->release();
}

IOReturn USBPipeShim::abort(void)
{
#ifndef TARGET_ELCAPITAN
    return m_pPipe->Abort();
#else
    return m_pPipe->abort();
#endif
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
#ifndef TARGET_ELCAPITAN
    return m_pPipe->Read(buffer, noDataTimeout, completionTimeout, reqCount, completion, bytesRead);
#else
    IOReturn result;
    if (completion)
        result = m_pPipe->io(buffer, reqCount, completion, completionTimeout);
    else
    {
        uint32_t bytesTransfered;
        IOReturn result = m_pPipe->io(buffer, reqCount, bytesTransfered, completionTimeout);
        if (bytesRead) *bytesRead = bytesTransfered;
    }
    return result;
#endif
}

IOReturn USBPipeShim::write(IOMemoryDescriptor *	buffer,
                            UInt32		noDataTimeout,
                            UInt32		completionTimeout,
                            IOByteCount		reqCount,
                            USBCOMPLETION *	completion)
{
#ifndef TARGET_ELCAPITAN
    return m_pPipe->Write(buffer, noDataTimeout, completionTimeout, reqCount, completion);
#else
    IOReturn result;
    if (completion)
        result = m_pPipe->io(buffer, reqCount, completion, completionTimeout);
    else
    {
        uint32_t bytesTransfered;
        result = m_pPipe->io(buffer, reqCount, bytesTransfered, completionTimeout);
    }
    return result;
#endif
}

const USBENDPOINTDESCRIPTOR* USBPipeShim::getEndpointDescriptor()
{
#ifndef TARGET_ELCAPITAN
    return m_pPipe->GetEndpointDescriptor();
#else
    return m_pPipe->getEndpointDescriptor();
#endif
}

IOReturn USBPipeShim::clearStall()
{
#ifndef TARGET_ELCAPITAN
    return m_pPipe->Reset();
#else
    return m_pPipe->clearStall(false);
#endif
}
