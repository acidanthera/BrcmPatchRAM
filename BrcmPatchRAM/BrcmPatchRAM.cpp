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

#ifndef TARGET_ELCAPITAN
#include <IOKit/usb/IOUSBInterface.h>
#else
#include <IOKit/usb/IOUSBHostFamily.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/USBSpec.h>
#include <IOKit/usb/USB.h>
#include <sys/utfconv.h>
#endif
#include <IOKit/IOCatalogue.h>

#include <kern/clock.h>
#include <libkern/version.h>
#include <libkern/zlib.h>
#include <string.h>

#include "Common.h"
#include "hci.h"
#include "BrcmPatchRAM.h"

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
    const StandardUSB::StringDescriptor* desc = m_pDevice->getStringDescriptor(index);
    if (!desc)
        return kIOReturnBadArgument;
    if (desc->bLength <= StandardUSB::kDescriptorSize)
        return kIOReturnBadArgument;
    //REVIEW: utf8_encodestr not found by kext linker (OSBundleLibraries issue)
    // for now use strncpy
    hack_strlcpy(buf, (const char*)desc->bString, maxLen-1);
    ////size_t utf8len = 0;
    ////utf8_encodestr(reinterpret_cast<const u_int16_t*>(desc->bString), desc->bLength - StandardUSB::kDescriptorSize, reinterpret_cast<u_int8_t*>(buf), &utf8len, maxLen, '/', UTF_LITTLE_ENDIAN);
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

//////////////////////////////////////////////////////////////////////////////////////////////////

enum { kMyOffPowerState = 0, kMyOnPowerState = 1 };

static IOPMPowerState myTwoStates[2] =
{
    { kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { kIOPMPowerStateVersion1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0 }
};

OSDefineMetaClassAndStructors(BrcmPatchRAM, IOService)

OSString* BrcmPatchRAM::brcmBundleIdentifier = NULL;
OSString* BrcmPatchRAM::brcmIOClass = NULL;
OSString* BrcmPatchRAM::brcmProviderClass = NULL;

void BrcmPatchRAM::initBrcmStrings()
{
    if (!brcmBundleIdentifier)
    {
        const char* bundle = NULL;
        const char* ioclass = NULL;
        const char* providerclass = kIOUSBDeviceClassName;
        
        // OS X - Snow Leopard
        // OS X - Lion
        if (version_major == 10 || version_major == 11)
        {
            bundle = "com.apple.driver.BroadcomUSBBluetoothHCIController";
            ioclass = "BroadcomUSBBluetoothHCIController";
        }
        // OS X - Mountain Lion (12.0 until 12.4)
        else if (version_major == 12 && version_minor <= 4)
        {
            bundle = "com.apple.iokit.BroadcomBluetoothHCIControllerUSBTransport";
            ioclass = "BroadcomBluetoothHCIControllerUSBTransport";
        }
        // OS X - Mountain Lion (12.5.0)
        // OS X - Mavericks
        // OS X - Yosemite
        else if (version_major == 12 || version_major == 13 || version_major == 14)
        {
            bundle = "com.apple.iokit.BroadcomBluetoothHostControllerUSBTransport";
            ioclass = "BroadcomBluetoothHostControllerUSBTransport";
        }
        // OS X - El Capitan
        else if (version_major == 15)
        {
            bundle = "com.apple.iokit.BroadcomBluetoothHostControllerUSBTransport";
            ioclass = "BroadcomBluetoothHostControllerUSBTransport";
            providerclass = kIOUSBHostDeviceClassName;
        }
        // OS X - Future releases....
        else if (version_major > 15)
        {
            AlwaysLog("Unknown new Darwin version %d.%d, using possible compatible personality.\n", version_major, version_minor);
            bundle = "com.apple.iokit.BroadcomBluetoothHostControllerUSBTransport";
            ioclass = "BroadcomBluetoothHostControllerUSBTransport";
            providerclass = kIOUSBHostDeviceClassName;
        }
        else
        {
            AlwaysLog("Unknown Darwin version %d.%d, no compatible personality known.\n", version_major, version_minor);
        }
        brcmBundleIdentifier = OSString::withCStringNoCopy(bundle);
        brcmIOClass = OSString::withCStringNoCopy(ioclass);
        brcmProviderClass = OSString::withCStringNoCopy(providerclass);
    }
}

IOService* BrcmPatchRAM::probe(IOService *provider, SInt32 *probeScore)
{
    extern kmod_info_t kmod_info;
    uint64_t start_time, end_time, nano_secs;
    
    DebugLog("probe\n");
    
    AlwaysLog("Version %s starting on OS X Darwin %d.%d.\n", kmod_info.version, version_major, version_minor);

    clock_get_uptime(&start_time);

    mWorkLock = IOLockAlloc();
    if (!mWorkLock)
        return NULL;

    mCompletionLock = IOLockAlloc();
    if (!mCompletionLock)
        return NULL;

    mDevice.setDevice(provider);
    if (!mDevice.getValidatedDevice())
    {
        AlwaysLog("Provider type is incorrect (not IOUSBDevice or IOUSBHostDevice)\n");
        return NULL;
    }

    // personality strings depend on version
    initBrcmStrings();

    // longest time seen in normal re-probe was ~200ms (400+ms on 10.11)
    if (version_major >= 15)
        mBlurpWait = 800;
    else
        mBlurpWait = 400;

    OSString* displayName = OSDynamicCast(OSString, getProperty(kDisplayName));
    if (displayName)
        provider->setProperty(kUSBProductString, displayName);
    
    mVendorId = mDevice.getVendorID();
    mProductId = mDevice.getProductID();

    // get firmware here to pre-cache for eventual use on wakeup or now
    if (OSString* firmwareKey = OSDynamicCast(OSString, getProperty(kFirmwareKey)))
    {
        if (BrcmFirmwareStore* firmwareStore = getFirmwareStore())
            firmwareStore->getFirmware(firmwareKey);
    }

    uploadFirmware();
    //IOSleep(500); //REVIEW
    publishPersonality();

    clock_get_uptime(&end_time);
    absolutetime_to_nanoseconds(end_time - start_time, &nano_secs);
    uint64_t milli_secs = nano_secs / 1000000;
    AlwaysLog("Processing time %llu.%llu seconds.\n", milli_secs / 1000, milli_secs % 1000);

#if 0
//#ifdef TARGET_ELCAPITAN
// maybe residency is not required for 10.11?
    if (15 == version_major)
    {
        mDevice.setDevice(NULL);
        return NULL;
    }
//#endif
#endif

    return this;
}

bool BrcmPatchRAM::start(IOService *provider)
{
    DebugLog("start\n");

    if (!super::start(provider))
        return false;
    
    // add interrupt source for delayed actions...
    IOWorkLoop* workLoop = getWorkLoop();
    if (!workLoop)
        return false;
    mWorkSource = IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &BrcmPatchRAM::processWorkQueue));
    if (!mWorkSource)
        return false;
    workLoop->addEventSource(mWorkSource);
    mWorkPending = 0;

    // add timer for firmware load in the case no re-probe after wake
    mTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &BrcmPatchRAM::onTimerEvent));
    if (!mTimer)
    {
        workLoop->removeEventSource(mWorkSource);
        mWorkSource->release();
        mWorkSource = NULL;
        return false;
    }
    workLoop->addEventSource(mTimer);

    // register for power state notifications
    PMinit();
    registerPowerDriver(this, myTwoStates, 2);
    provider->joinPMtree(this);
    
    //REVIEW: do firmware upload later
    ///mTimer->setTimeoutMS(mBlurpWait);
    
    return true;
}

static uint64_t wake_time;

void BrcmPatchRAM::stop(IOService* provider)
{
    uint64_t stop_time, nano_secs;
    clock_get_uptime(&stop_time);
    absolutetime_to_nanoseconds(stop_time - wake_time, &nano_secs);
    uint64_t milli_secs = nano_secs / 1000000;
    AlwaysLog("Time since wake %llu.%llu seconds.\n", milli_secs / 1000, milli_secs % 1000);

    DebugLog("stop\n");

//REVIEW: so kext can be unloaded with kextunload -p
    // unload native bluetooth driver
    IOReturn result = gIOCatalogue->terminateDriversForModule(brcmBundleIdentifier, false);
    if (result != kIOReturnSuccess)
        AlwaysLog("[%04x:%04x]: failure terminating native Broadcom bluetooth (%08x)\n", mVendorId, mProductId, result);
    else
        DebugLog("[%04x:%04x]: success terminating native Broadcom bluetooth\n", mVendorId, mProductId);

    // unpublish native bluetooth personality
    removePersonality();

    mStopping = true;
    OSSafeReleaseNULL(mFirmwareStore);

    IOWorkLoop* workLoop = getWorkLoop();
    if (workLoop)
    {
        if (mTimer)
        {
            mTimer->cancelTimeout();
            workLoop->removeEventSource(mTimer);
            mTimer->release();
            mTimer = NULL;
        }
        if (mWorkSource)
        {
            workLoop->removeEventSource(mWorkSource);
            mWorkSource->release();
            mWorkSource = NULL;
            mWorkPending = 0;
        }
    }

    PMstop();

    if (mCompletionLock)
    {
        IOLockFree(mCompletionLock);
        mCompletionLock = NULL;
    }
    if (mWorkLock)
    {
        IOLockFree(mWorkLock);
        mWorkLock = NULL;
    }

    mDevice.setDevice(NULL);

    mStopping = false;

    super::stop(provider);
}

IOReturn BrcmPatchRAM::onTimerEvent()
{
    DebugLog("onTimerEvent\n");

    if (!mDevice.getProperty(kFirmwareLoaded))
    {
        AlwaysLog("BLURP!! no firmware loaded and timer expiried (no re-probe)\n");
        scheduleWork(kWorkLoadFirmware);
    }

    return kIOReturnSuccess;
}

void BrcmPatchRAM::scheduleWork(unsigned int newWork)
{
    IOLockLock(mWorkLock);
    mWorkPending |= newWork;
    mWorkSource->interruptOccurred(0, 0, 0);
    IOLockUnlock(mWorkLock);
}

void BrcmPatchRAM::processWorkQueue(IOInterruptEventSource*, int)
{
    IOLockLock(mWorkLock);

    // start firmware loading process in a non-workloop thread
    if (mWorkPending & kWorkLoadFirmware)
    {
        DebugLog("_workPending kWorkLoadFirmare\n");
        mWorkPending &= ~kWorkLoadFirmware;
        retain();
        kern_return_t result = kernel_thread_start(&BrcmPatchRAM::uploadFirmwareThread, this, &mWorker);
        if (KERN_SUCCESS == result)
            DebugLog("Success creating firmware uploader thread\n");
        else
        {
            AlwaysLog("ERROR creating firmware uploader thread.\n");
            release();
        }
    }

    // firmware loading thread is finished
    if (mWorkPending & kWorkFinished)
    {
        DebugLog("_workPending kWorkFinished\n");
        mWorkPending &= ~kWorkFinished;
        thread_deallocate(mWorker);
        mWorker = 0;
        release();  // matching retain when thread created successfully
    }

    IOLockUnlock(mWorkLock);
}

void BrcmPatchRAM::uploadFirmwareThread(void *arg, wait_result_t wait)
{
    DebugLog("sendFirmwareThread enter\n");

    BrcmPatchRAM* me = static_cast<BrcmPatchRAM*>(arg);
    me->resetDevice();
    IOSleep(20);
    me->uploadFirmware();
    me->publishPersonality();
    me->scheduleWork(kWorkFinished);

    DebugLog("sendFirmwareThread termination\n");
    thread_terminate(current_thread());
    DebugLog("!!! sendFirmwareThread post-terminate !!! should not be here\n");
}

void BrcmPatchRAM::uploadFirmware()
{
    // signal to timer that firmware already loaded
    mDevice.setProperty(kFirmwareLoaded, true);

    // don't bother with devices that have no firmware
    if (!getProperty(kFirmwareKey))
        return;

    if (mDevice.open(this))
    {
        // Print out additional device information
        printDeviceInfo();
        
        // Set device configuration to composite configuration index 0
        // Obtain first interface
        if (setConfiguration(0) && findInterface(&mInterface) && mInterface.open(this))
        {
            DebugLog("set configuration and interface opened\n");
            mInterface.findPipe(&mInterruptPipe, kUSBInterrupt, kUSBIn);
            mInterface.findPipe(&mBulkPipe, kUSBBulk, kUSBOut);
            if (mInterruptPipe.getValidatedPipe() && mBulkPipe.getValidatedPipe())
            {
                DebugLog("got pipes\n");
                if (performUpgrade())
                    if (mDeviceState == kUpdateComplete)
                        AlwaysLog("[%04x:%04x]: Firmware upgrade completed successfully.\n", mVendorId, mProductId);
                    else
                        AlwaysLog("[%04x:%04x]: Firmware upgrade not needed.\n", mVendorId, mProductId);
                else
                    AlwaysLog("[%04x:%04x]: Firmware upgrade failed.\n", mVendorId, mProductId);
                OSSafeReleaseNULL(mReadBuffer); // mReadBuffer is allocated by performUpgrade but not released
            }
            mInterface.close(this);
        }
        
        // cleanup
        if (mInterruptPipe.getValidatedPipe())
        {
            mInterruptPipe.abort();
            mInterruptPipe.setPipe(NULL);
        }
        if (mBulkPipe.getValidatedPipe())
        {
            mBulkPipe.abort();
            mBulkPipe.setPipe(NULL);
        }
        mInterface.setInterface(NULL);
        mDevice.close(this);
    }
}

IOReturn BrcmPatchRAM::setPowerState(unsigned long which, IOService *whom)
{
    DebugLog("setPowerState: which = 0x%lx\n", which);
    
    if (which == kMyOffPowerState)
    {
        // consider firmware no longer loaded
        mDevice.removeProperty(kFirmwareLoaded);

        // in the case the instance is shutting down, don't do anything
        if (!mStopping)
        {
            // unload native bluetooth driver
            IOReturn result = gIOCatalogue->terminateDriversForModule(brcmBundleIdentifier, false);
            if (result != kIOReturnSuccess)
                AlwaysLog("[%04x:%04x]: failure terminating native Broadcom bluetooth (%08x)\n", mVendorId, mProductId, result);
            else
                DebugLog("[%04x:%04x]: success terminating native Broadcom bluetooth\n", mVendorId, mProductId);

            // unpublish native bluetooth personality
            removePersonality();
        }
    }
    else if (which == kMyOnPowerState)
    {
        clock_get_uptime(&wake_time);
        // start loading firmware for case probe is never called after wake
        if (!mDevice.getProperty(kFirmwareLoaded))
            mTimer->setTimeoutMS(mBlurpWait);
    }

    return IOPMAckImplied;
}

static void setStringInDict(OSDictionary* dict, const char* key, const char* value)
{
    OSString* str = OSString::withCStringNoCopy(value);
    if (str)
    {
        dict->setObject(key, str);
        str->release();
    }
}

static void setNumberInDict(OSDictionary* dict, const char* key, UInt16 value)
{
    OSNumber* num = OSNumber::withNumber(value, 16);
    if (num)
    {
        dict->setObject(key, num);
        num->release();
    }
}

#ifdef DEBUG
void BrcmPatchRAM::printPersonalities()
{
    // Matching dictionary for the current device
    OSDictionary* dict = OSDictionary::withCapacity(5);
    if (!dict) return;
    dict->setObject(kIOProviderClassKey, brcmProviderClass);
    setNumberInDict(dict, kUSBProductID, mProductId);
    setNumberInDict(dict, kUSBVendorID, mVendorId);
    
    SInt32 generatonCount;
    if (OSOrderedSet* set = gIOCatalogue->findDrivers(dict, &generatonCount))
    {
        AlwaysLog("[%04x:%04x]: %d matching driver personalities.\n", mVendorId, mProductId, set->getCount());
        if (OSCollectionIterator* iterator = OSCollectionIterator::withCollection(set))
        {
            while (OSDictionary* personality = static_cast<OSDictionary*>(iterator->getNextObject()))
            {
                OSString* bundleId = OSDynamicCast(OSString, personality->getObject(kBundleIdentifier));
                AlwaysLog("[%04x:%04x]: existing IOKit personality \"%s\".\n", mVendorId, mProductId, bundleId->getCStringNoCopy());
            }
            iterator->release();
        }
        set->release();
    }
    dict->release();
}
#endif //DEBUG

void BrcmPatchRAM::removePersonality()
{
    return;  //TODO: remove this when ready...

    DebugLog("removePersonality\n");
    
#ifdef DEBUG
    printPersonalities();
#endif
    
    // remove Broadcom matching personality
    OSDictionary* dict = OSDictionary::withCapacity(4);
    if (!dict) return;
    dict->setObject(kIOProviderClassKey, brcmProviderClass);
    setNumberInDict(dict, kUSBProductID, mProductId);
    setNumberInDict(dict, kUSBVendorID, mVendorId);
    dict->setObject(kBundleIdentifier, brcmBundleIdentifier);
    gIOCatalogue->removeDrivers(dict, false);  //REVIEW: not doing nub matching here
#if 1
    // remove generic matching personality
    dict->removeObject(kUSBProductID);
    dict->removeObject(kUSBVendorID);
    setStringInDict(dict, kBundleIdentifier, "com.apple.iokit.IOBluetoothHostControllerUSBTransport");
    setNumberInDict(dict, "bDeviceClass", 224);
    setNumberInDict(dict, "bDeviceProtocol", 1);
    setNumberInDict(dict, "bDeviceSubClass", 1);
    gIOCatalogue->removeDrivers(dict, false);  //REVIEW: not doing nub matching here
#endif
    dict->release();
    
#ifdef DEBUG
    printPersonalities();
#endif
}

void BrcmPatchRAM::publishPersonality()
{
    return;  //TODO: remove me when ready...

    // Matching dictionary for the current device
    OSDictionary* dict = OSDictionary::withCapacity(5);
    if (!dict) return;
    dict->setObject(kIOProviderClassKey, brcmProviderClass);
    setNumberInDict(dict, kUSBProductID, mProductId);
    setNumberInDict(dict, kUSBVendorID, mVendorId);
    
    // Retrieve currently matching IOKit driver personalities
    OSDictionary* personality = NULL;
    SInt32 generationCount;
    if (OSOrderedSet* set = gIOCatalogue->findDrivers(dict, &generationCount))
    {
        if (set->getCount())
            DebugLog("[%04x:%04x]: %d matching driver personalities.\n", mVendorId, mProductId, set->getCount());
        
        if (OSCollectionIterator* iterator = OSCollectionIterator::withCollection(set))
        {
            while ((personality = OSDynamicCast(OSDictionary, iterator->getNextObject())))
            {
                if (OSString* bundleId = OSDynamicCast(OSString, personality->getObject(kBundleIdentifier)))
                    if (strncmp(bundleId->getCStringNoCopy(), kAppleBundlePrefix, strlen(kAppleBundlePrefix)) == 0)
                    {
                        AlwaysLog("[%04x:%04x]: Found existing IOKit personality \"%s\".\n", mVendorId, mProductId, bundleId->getCStringNoCopy());
                        break;
                    }
            }
            iterator->release();
        }
        set->release();
    }
    
    if (!personality && brcmBundleIdentifier)
    {
        // OS X does not have a driver personality for this device yet, publish one
        DebugLog("brcmBundIdentifier: \"%s\"\n", brcmBundleIdentifier->getCStringNoCopy());
        DebugLog("brcmIOClass: \"%s\"\n", brcmIOClass->getCStringNoCopy());
        DebugLog("brcmProviderClass: \"%s\"\n", brcmProviderClass->getCStringNoCopy());
        dict->setObject(kBundleIdentifier, brcmBundleIdentifier);
        dict->setObject(kIOClassKey, brcmIOClass);
        
        // Add new personality into the kernel
        if (OSArray* array = OSArray::withCapacity(1))
        {
            array->setObject(dict);
            if (gIOCatalogue->addDrivers(array, false))
            {
                AlwaysLog("[%04x:%04x]: Published new IOKit personality.\n", mVendorId, mProductId);
                if (OSDictionary* dict1 = OSDynamicCast(OSDictionary, dict->copyCollection()))
                {
                    //dict1->removeObject(kIOClassKey);
                    //dict1->removeObject(kIOProviderClassKey);
                    dict1->removeObject(kUSBProductID);
                    dict1->removeObject(kUSBVendorID);
                    dict1->removeObject(kBundleIdentifier);
                    if (!gIOCatalogue->startMatching(dict1))
                        AlwaysLog("[%04x:%04x]: startMatching failed.\n", mVendorId, mProductId);
                    dict1->release();
                }
            }
            else
                AlwaysLog("[%04x:%04x]: ERROR in addDrivers for new IOKit personality.\n", mVendorId, mProductId);
            array->release();
        }
    }
    dict->release();

#ifdef DEBUG
    printPersonalities();
#endif
}

bool BrcmPatchRAM::publishFirmwareStorePersonality()
{
    // matching dictionary for disabled BrcmFirmwareStore
    OSDictionary* dict = OSDictionary::withCapacity(3);
    if (!dict) return false;
    setStringInDict(dict, kIOProviderClassKey, "disabled_IOResources");
    setStringInDict(dict, kIOClassKey, "BrcmFirmwareStore");
    setStringInDict(dict, kIOMatchCategoryKey, "BrcmFirmwareStore");

    // retrieve currently matching IOKit driver personalities
    OSDictionary* personality = NULL;
    SInt32 generationCount;
    if (OSOrderedSet* set = gIOCatalogue->findDrivers(dict, &generationCount))
    {
        if (set->getCount())
            DebugLog("%d matching driver personalities for BrcmFirmwareStore.\n", set->getCount());

        // should be only one, so we can grab just the first
        if (OSCollectionIterator* iterator = OSCollectionIterator::withCollection(set))
        {
            personality = OSDynamicCast(OSDictionary, iterator->getNextObject());
            iterator->release();
        }
        set->release();
    }
    // if we don't find it, then something is really wrong...
    if (!personality)
    {
        AlwaysLog("unable to find disabled BrcmFirmwareStore personality.\n");
        dict->release();
        return false;
    }
    // make copy of personality *before* removing from IOcatalog
    personality = OSDynamicCast(OSDictionary, personality->copyCollection());
    if (!personality)
    {
        AlwaysLog("copyCollection failed.");
        return false;
    }

    // unpublish disabled personality
    gIOCatalogue->removeDrivers(dict);
    dict->release();

    // Add new personality into the kernel
    if (OSArray* array = OSArray::withCapacity(1))
    {
        // change from disabled_IOResources to IOResources
        setStringInDict(personality, kIOProviderClassKey, "IOResources");
        array->setObject(personality);
        if (gIOCatalogue->addDrivers(array, true))
            AlwaysLog("Published new IOKit personality for BrcmFirmwareStore.\n");
        else
            AlwaysLog("ERROR in addDrivers for new BrcmFirmwareStore personality.\n");
        array->release();
    }
    personality->release();

    return true;
}

BrcmFirmwareStore* BrcmPatchRAM::getFirmwareStore()
{
    if (!mFirmwareStore)
    {
        // check to see if it already loaded
        mFirmwareStore = OSDynamicCast(BrcmFirmwareStore, waitForMatchingService(serviceMatching(kBrcmFirmwareStoreService), 0));
        if (!mFirmwareStore)
        {
            // not loaded, so publish personality...
            publishFirmwareStorePersonality();
            // and wait...
            mFirmwareStore = OSDynamicCast(BrcmFirmwareStore, waitForMatchingService(serviceMatching(kBrcmFirmwareStoreService), 2000UL*1000UL*1000UL));
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
    
    if ((result = mDevice.getDeviceStatus(this, &status)) != kIOReturnSuccess)
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
    
    if ((result = mDevice.resetDevice()) != kIOReturnSuccess)
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
    const USBCONFIGURATIONDESCRIPTOR* configurationDescriptor;
    UInt8 currentConfiguration = 0xFF;
    
    // Find the first config/interface
    UInt8 numconf = 0;
    
    if ((numconf = mDevice.getNumConfigurations()) < (configurationIndex + 1))
    {
        AlwaysLog("[%04x:%04x]: Composite configuration index %d is not available, %d total composite configurations.\n",
                  mVendorId, mProductId, configurationIndex, numconf);
        return false;
    }
    else
        DebugLog("[%04x:%04x]: Available composite configurations: %d.\n", mVendorId, mProductId, numconf);
    
    configurationDescriptor = mDevice.getFullConfigurationDescriptor(configurationIndex);
    
    // Set the configuration to the requested configuration index
    if (!configurationDescriptor)
    {
        AlwaysLog("[%04x:%04x]: No configuration descriptor for configuration index: %d.\n", mVendorId, mProductId, configurationIndex);
        return false;
    }
    
    if ((result = mDevice.getConfiguration(this, &currentConfiguration)) != kIOReturnSuccess)
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
    if ((result = mDevice.setConfiguration(this, configurationDescriptor->bConfigurationValue, true)) != kIOReturnSuccess)
    {
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
    if (IOService* interface = shim->getValidatedInterface())
    {
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
    if (!mInterface.findPipe(shim, type, direction))
    {
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
    if (!mReadBuffer)
    {
        mReadBuffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, 0x200);
        if (!mReadBuffer)
        {
            AlwaysLog("[%04x:%04x]: continuousRead - failed to allocate read buffer.\n", mVendorId, mProductId);
            return false;
        }
#ifndef TARGET_ELCAPITAN
        mInterruptCompletion.target = this;
#else
        mInterruptCompletion.owner = this;
#endif
        mInterruptCompletion.action = readCompletion;
        mInterruptCompletion.parameter = NULL;
    }

    IOReturn result = mReadBuffer->prepare();
    if (result != kIOReturnSuccess)
    {
        AlwaysLog("[%04x:%04x]: continuousRead - failed to prepare buffer (0x%08x)\n", mVendorId, mProductId, result);
        return false;
    }

    if ((result = mInterruptPipe.read(mReadBuffer, 0, 0, mReadBuffer->getLength(), &mInterruptCompletion)) != kIOReturnSuccess)
    {
        AlwaysLog("[%04x:%04x]: continuousRead - Failed to queue read (0x%08x)\n", mVendorId, mProductId, result);

        if (result == kIOUSBPipeStalled)
        {
            mInterruptPipe.clearStall();
            result = mInterruptPipe.read(mReadBuffer, 0, 0, mReadBuffer->getLength(), &mInterruptCompletion);
            
            if (result != kIOReturnSuccess)
            {
                AlwaysLog("[%04x:%04x]: continuousRead - Failed, read dead (0x%08x)\n", mVendorId, mProductId, result);
                return false;
            }
        }
    }

    return true;
}

#ifndef TARGET_ELCAPITAN
void BrcmPatchRAM::readCompletion(void* target, void* parameter, IOReturn status, UInt32 bufferSizeRemaining)
#else
void BrcmPatchRAM::readCompletion(void* target, void* parameter, IOReturn status, uint32_t bytesTransferred)
#endif
{
    BrcmPatchRAM *me = (BrcmPatchRAM*)target;

    IOLockLock(me->mCompletionLock);

    IOReturn result = me->mReadBuffer->complete();
    if (result != kIOReturnSuccess)
        DebugLog("[%04x:%04x]: ReadCompletion failed to complete read buffer (\"%s\" 0x%08x).\n", me->mVendorId, me->mProductId, me->stringFromReturn(result), result);

    switch (status)
    {
        case kIOReturnSuccess:
#ifndef TARGET_ELCAPITAN
            me->hciParseResponse(me->mReadBuffer->getBytesNoCopy(), me->mReadBuffer->getLength() - bufferSizeRemaining, NULL, NULL);
#else
            me->hciParseResponse(me->mReadBuffer->getBytesNoCopy(), bytesTransferred, NULL, NULL);
#endif
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
                    
                    mFirmareVersion = *(UInt16*)(((char*)response) + 10);
                    
                    DebugLog("[%04x:%04x]: Firmware version: v%d.\n",
                             mVendorId, mProductId, mFirmareVersion + 0x1000);
                    
                    // Device does not require a firmware patch at this time
                    if (mFirmareVersion > 0)
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
                    
                    mDeviceState = kResetComplete;
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
                    result = kIOReturnMessageTooLarge;
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
            DebugLog("[%04x:%04x]: Unknown event code (0x%02x).\n", mVendorId, mProductId, header->eventCode);
            break;
    }
    
    return result;
}

IOReturn BrcmPatchRAM::bulkWrite(const void* data, UInt16 length)
{
    IOReturn result;
    
    if (IOMemoryDescriptor* buffer = IOMemoryDescriptor::withAddress((void*)data, length, kIODirectionIn))
    {
        if ((result = buffer->prepare()) == kIOReturnSuccess)
        {
            if ((result = mBulkPipe.write(buffer, 0, 0, buffer->getLength(), NULL)) == kIOReturnSuccess)
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
    mDeviceState = kInitialize;

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
            case kInitialize:
                hciCommand(&HCI_VSC_READ_VERBOSE_CONFIG, sizeof(HCI_VSC_READ_VERBOSE_CONFIG));
                break;

            case kFirmwareVersion:
                // Unable to retrieve firmware store
                if (!(firmwareStore = getFirmwareStore()))
                {
                    mDeviceState = kUpdateAborted;
                    continue;
                }
                instructions = firmwareStore->getFirmware(OSDynamicCast(OSString, getProperty(kFirmwareKey)));
                // Unable to retrieve firmware instructions
                if (!instructions)
                {
                    mDeviceState = kUpdateAborted;
                    continue;
                }

                // Initiate firmware upgrade
                hciCommand(&HCI_VSC_DOWNLOAD_MINIDRIVER, sizeof(HCI_VSC_DOWNLOAD_MINIDRIVER));
                break;

            case kMiniDriverComplete:
                // Write firmware data to bulk pipe
                iterator = OSCollectionIterator::withCollection(instructions);
                if (!iterator)
                {
                    mDeviceState = kUpdateAborted;
                    continue;
                }

                // If this IOSleep is not issued, the device is not ready to receive
                // the firmware instructions and we will deadlock due to lack of
                // responses.
                IOSleep(10);

                // Write first 2 instructions to trigger response
                if ((data = OSDynamicCast(OSData, iterator->getNextObject())))
                    bulkWrite(data->getBytesNoCopy(), data->getLength());
                if ((data = OSDynamicCast(OSData, iterator->getNextObject())))
                    bulkWrite(data->getBytesNoCopy(), data->getLength());
                break;

            case kInstructionWrite:
                // should never happen, but would cause a crash
                if (!iterator)
                {
                    mDeviceState = kUpdateAborted;
                    continue;
                }

                if ((data = OSDynamicCast(OSData, iterator->getNextObject())))
                    bulkWrite(data->getBytesNoCopy(), data->getLength());
                else
                    // Firmware data fully written
                    hciCommand(&HCI_VSC_END_OF_RECORD, sizeof(HCI_VSC_END_OF_RECORD));
                break;

            case kInstructionWritten:
                mDeviceState = kInstructionWrite;
                continue;

            case kFirmwareWritten:
                hciCommand(&HCI_RESET, sizeof(HCI_RESET));
                break;

            case kResetComplete:
                resetDevice();
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
        if (!continuousRead())
        {
            mDeviceState = kUpdateAborted;
            continue;
        }
        // wait for completion of the async read
        IOLockSleep(mCompletionLock, this, 0);
    }

    IOLockUnlock(mCompletionLock);
    OSSafeRelease(iterator);

    return mDeviceState == kUpdateComplete || mDeviceState == kUpdateNotNeeded;
}

#ifdef DEBUG
const char* BrcmPatchRAM::getState(DeviceState deviceState)
{
    static const IONamedValue state_values[] = {
        {kUnknown,            "Unknown"              },
        {kInitialize,         "Initialize"           },
        {kFirmwareVersion,    "Firmware version"     },
        {kMiniDriverComplete, "Mini-driver complete" },
        {kInstructionWrite,   "Instruction write"    },
        {kInstructionWritten, "Instruction written"  },
        {kFirmwareWritten,    "Firmware written"     },
        {kResetComplete,      "Reset complete"       },
        {kUpdateComplete,     "Update complete"      },
        {kUpdateNotNeeded,    "Update not needed"    },
        {0,                   NULL                   }
    };
    
    return IOFindNameForValue(deviceState, state_values);
}
#endif //DEBUG

#ifndef kIOUSBClearPipeStallNotRecursive
// from 10.7 SDK
#define kIOUSBClearPipeStallNotRecursive iokit_usb_err(0x48)
#endif

const char* BrcmPatchRAM::stringFromReturn(IOReturn rtn)
{
    static const IONamedValue IOReturn_values[] = {
        {kIOReturnIsoTooOld,          "Isochronous I/O request for distant past"     },
        {kIOReturnIsoTooNew,          "Isochronous I/O request for distant future"   },
        {kIOReturnNotFound,           "Data was not found"                           },
//REIVEW: new error identifiers?
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