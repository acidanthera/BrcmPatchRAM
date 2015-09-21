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

#ifndef __USBDeviceShim__
#define __USBDeviceShim__

#ifndef TARGET_ELCAPITAN
#include <IOKit/usb/IOUSBDevice.h>
#else
#include <IOKit/usb/IOUSBHostDevice.h>
#endif

#ifndef TARGET_ELCAPITAN
#define USBCOMPLETION IOUSBCompletion
#define USBCONFIGURATIONDESCRIPTOR IOUSBConfigurationDescriptor
#define USBENDPOINTDESCRIPTOR IOUSBEndpointDescriptor
#else
#define USBCOMPLETION IOUSBHostCompletion
#define USBCONFIGURATIONDESCRIPTOR StandardUSB::ConfigurationDescriptor
#define USBENDPOINTDESCRIPTOR StandardUSB::EndpointDescriptor
#define USBStatus uint16_t
#endif

class USBPipeShim;
class USBInterfaceShim;

class USBDeviceShim
{
private:
#ifndef TARGET_ELCAPITAN
    IOUSBDevice* m_pDevice;
#else
    IOUSBHostDevice* m_pDevice;  // 10.11+
#endif
    
public:
    USBDeviceShim();
    void setDevice(IOService* provider);
    inline IOService* getValidatedDevice() { return m_pDevice; }
    
    UInt16 getVendorID();
    UInt16 getProductID();
    OSObject* getProperty(const char* name);
    void setProperty(const char* name, bool value);
    void removeProperty(const char* name);
    IOReturn getStringDescriptor(UInt8 index, char *buf, int maxLen, UInt16 lang=0x409);
    UInt16 getDeviceRelease();
    IOReturn getDeviceStatus(IOService* forClient, USBStatus *status);
    IOReturn resetDevice();
    UInt8 getNumConfigurations();
    const USBCONFIGURATIONDESCRIPTOR* getFullConfigurationDescriptor(UInt8 configIndex);
    IOReturn getConfiguration(IOService* forClient, UInt8 *configNumber);
    IOReturn setConfiguration(IOService *forClient, UInt8 configValue, bool startInterfaceMatching=true);
    bool findFirstInterface(USBInterfaceShim* shim);
    bool open(IOService *forClient, IOOptionBits options = 0, void *arg = 0 );
    void close(IOService *forClient, IOOptionBits options = 0);
    UInt8 getManufacturerStringIndex();
    UInt8 getProductStringIndex();
    UInt8 getSerialNumberStringIndex();
};

class USBInterfaceShim
{
private:
#ifndef TARGET_ELCAPITAN
    IOUSBInterface* m_pInterface;
#else
    IOUSBHostInterface* m_pInterface;  // 10.11+
#endif
    
public:
    USBInterfaceShim();
    void setInterface(IOService* interface);
    inline IOService* getValidatedInterface() { return (IOService*)m_pInterface; }
    
    bool open(IOService *forClient, IOOptionBits options = 0, void *arg = 0 );
    void close(IOService *forClient, IOOptionBits options = 0);
    
#ifdef DEBUG
    UInt8 getInterfaceNumber();
    UInt8 getInterfaceClass();
    UInt8 getInterfaceSubClass();
    UInt8 getInterfaceProtocol();
#endif
    
    bool findPipe(USBPipeShim* shim, uint8_t type, uint8_t direction);
    
    IOReturn hciCommand(void * command, UInt16 length);
};

class USBPipeShim
{
private:
#ifndef TARGET_ELCAPITAN
    IOUSBPipe* m_pPipe;
#else
    IOUSBHostPipe* m_pPipe;  // 10.11+
#endif
    
public:
    USBPipeShim();
    void setPipe(OSObject* pipe);
    inline OSObject* getValidatedPipe() { return m_pPipe; }
    
    IOReturn abort(void);
    
    IOReturn read(IOMemoryDescriptor *	buffer,
                  UInt32		noDataTimeout,
                  UInt32		completionTimeout,
                  IOByteCount		reqCount,
                  USBCOMPLETION *	completion = 0,
                  IOByteCount *		bytesRead = 0);
    IOReturn write(IOMemoryDescriptor *	buffer,
                   UInt32		noDataTimeout,
                   UInt32		completionTimeout,
                   IOByteCount		reqCount,
                   USBCOMPLETION *	completion = 0);
    const USBENDPOINTDESCRIPTOR* getEndpointDescriptor();
    IOReturn clearStall(void);
};

#endif /* __USBDeviceShim__ */
