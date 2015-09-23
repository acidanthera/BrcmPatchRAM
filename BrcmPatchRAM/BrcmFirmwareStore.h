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

#ifndef __BrcmPatchRAM__BrcmFirmwareStore__
#define __BrcmPatchRAM__BrcmFirmwareStore__

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <libkern/OSKextLib.h>

#define kBrcmFirmwareCompressed     "zhx"
#define kBrmcmFirwareUncompressed   "hex"

#define kBrcmFirmwareStoreService "BrcmFirmwareStore"

class BrcmFirmwareStore : public IOService
{
private:
    typedef IOService super;
    OSDeclareDefaultStructors(BrcmFirmwareStore);

    struct ResourceCallbackContext
    {
        BrcmFirmwareStore* me;
        OSData* firmware;
    };

    IOLock* mDataLock;
    OSDictionary* mFirmwares;
    IOLock* mCompletionLock = NULL;

    OSData* decompressFirmware(OSData* firmware);
    OSArray* parseFirmware(OSData* firmwareData);
    static void requestResourceCallback(OSKextRequestTag requestTag, OSReturn result, const void * resourceData, uint32_t resourceDataLength, void* context);
    OSData* loadFirmwareFile(const char* filename, const char* suffix);
    OSData* loadFirmwareFiles(UInt16 vendorId, UInt16 productId, OSString* firmwareIdentifier);
    OSArray* loadFirmware(UInt16 vendorId, UInt16 productId, OSString* firmwareIdentifier);

public:
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);

    virtual OSArray* getFirmware(UInt16 vendorId, UInt16 productId, OSString* firmwareIdentifier);
};

#endif /* defined(__BrcmPatchRAM__BrcmFirmwareStore__) */
