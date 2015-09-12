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

#include <IOKit/usb/IOUSBInterface.h>

USBDeviceShim::USBDeviceShim()
{
    m_pDevice = NULL;
}

void USBDeviceShim::setDevice(IOService* provider)
{
    OSObject* prev = m_pDevice;
    
    m_pDevice = OSDynamicCast(IOUSBDevice, provider);
    
    if (m_pDevice)
        m_pDevice->retain();
    
    if (prev)
        prev->release();
}

UInt16 USBDeviceShim::getVendorID()
{
    return m_pDevice->GetVendorID();
}

UInt16 USBDeviceShim::getProductID()
{
    return m_pDevice->GetProductID();
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

IOReturn USBDeviceShim::getStringDescriptor(UInt8 index, char *buf, int maxLen, UInt16 lang)
{
    return m_pDevice->GetStringDescriptor(index, buf, maxLen, lang);
}

UInt16 USBDeviceShim::getDeviceRelease()
{
    return m_pDevice->GetDeviceRelease();
}

IOReturn USBDeviceShim::getDeviceStatus(IOService* forClient, USBStatus *status)
{
    return m_pDevice->GetDeviceStatus(status);
}

IOReturn USBDeviceShim::resetDevice()
{
    return m_pDevice->ResetDevice();
}

UInt8 USBDeviceShim::getNumConfigurations()
{
    return m_pDevice->GetNumConfigurations();
}

const USBCONFIGURATIONDESCRIPTOR* USBDeviceShim::getFullConfigurationDescriptor(UInt8 configIndex)
{
    return m_pDevice->GetFullConfigurationDescriptor(configIndex);
}

IOReturn USBDeviceShim::getConfiguration(IOService* forClient, UInt8 *configNumber)
{
    return m_pDevice->GetConfiguration(configNumber);
}

IOReturn USBDeviceShim::setConfiguration(IOService *forClient, UInt8 configValue, bool startInterfaceMatching)
{
    return m_pDevice->SetConfiguration(forClient, configValue, startInterfaceMatching);
}

bool USBDeviceShim::findFirstInterface(USBInterfaceShim* shim)
{
    DebugLog("USBDeviceShim::findFirstInterface\n");

    IOUSBFindInterfaceRequest request;
    request.bAlternateSetting  = kIOUSBFindInterfaceDontCare;
    request.bInterfaceClass    = kIOUSBFindInterfaceDontCare;
    request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
    request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    IOUSBInterface* interface = m_pDevice->FindNextInterface(NULL, &request);
    DebugLog("FindNextInterface returns %p\n", interface);
    shim->setInterface(interface);

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
    return m_pDevice->GetManufacturerStringIndex();
}

UInt8 USBDeviceShim::getProductStringIndex()
{
    return m_pDevice->GetProductStringIndex();
}
UInt8 USBDeviceShim::getSerialNumberStringIndex()
{
    return m_pDevice->GetSerialNumberStringIndex();
}

USBInterfaceShim::USBInterfaceShim()
{
    m_pInterface = NULL;
}

void USBInterfaceShim::setInterface(IOService* interface)
{
    OSObject* prev = m_pInterface;

    m_pInterface = OSDynamicCast(IOUSBInterface, interface);
    
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
    return m_pInterface->GetInterfaceNumber();
}

UInt8 USBInterfaceShim::getInterfaceClass()
{
    return m_pInterface->GetInterfaceClass();
}

UInt8 USBInterfaceShim::getInterfaceSubClass()
{
    return m_pInterface->GetInterfaceSubClass();
}

UInt8 USBInterfaceShim::getInterfaceProtocol()
{
    return m_pInterface->GetInterfaceProtocol();
}
#endif

bool USBInterfaceShim::findPipe(USBPipeShim* shim, UInt8 type, UInt8 direction)
{
    IOUSBFindEndpointRequest findEndpointRequest;
    findEndpointRequest.type = type;
    findEndpointRequest.direction = direction;
    if (IOUSBPipe* pipe = m_pInterface->FindNextPipe(NULL, &findEndpointRequest))
    {
        shim->setPipe(pipe);
        return true;
    }
    return false;
}

IOReturn USBInterfaceShim::hciCommand(void* command, UInt16 length)
{
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
}

USBPipeShim::USBPipeShim()
{
    m_pPipe = NULL;
}

void USBPipeShim::setPipe(OSObject* pipe)
{
    OSObject* prev = m_pPipe;
    m_pPipe = OSDynamicCast(IOUSBPipe, pipe);

    if (m_pPipe)
        m_pPipe->retain();
    if (prev)
        prev->release();
}

IOReturn USBPipeShim::abort(void)
{
    return m_pPipe->Abort();
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
    return m_pPipe->Read(buffer, noDataTimeout, completionTimeout, reqCount, completion, bytesRead);
}

IOReturn USBPipeShim::write(IOMemoryDescriptor *	buffer,
                            UInt32		noDataTimeout,
                            UInt32		completionTimeout,
                            IOByteCount		reqCount,
                            USBCOMPLETION *	completion)
{
    return m_pPipe->Write(buffer, noDataTimeout, completionTimeout, reqCount, completion);
}

const USBENDPOINTDESCRIPTOR* USBPipeShim::getEndpointDescriptor()
{
    return m_pPipe->GetEndpointDescriptor();
}

IOReturn USBPipeShim::clearStall()
{
    return m_pPipe->Reset();
}
