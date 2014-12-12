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
 *  Zlib implementation based on /apple/xnu/libkern/c++/OSKext.cpp
 */

#include "Common.h"
#include "BrcmFirmwareStore.h"

/***************************************
 * Zlib Decompression
 ***************************************/
#include <libkern/zlib.h>

extern "C"
{
    static void* z_alloc(void*, u_int items, u_int size);
    static void z_free(void*, void *ptr);
    
    typedef struct z_mem
    {
        uint32_t alloc_size;
        uint8_t data[0];
    } z_mem;
    
    /*
     * Space allocation and freeing routines for use by zlib routines.
     */
    void* z_alloc(void* notused __unused, u_int num_items, u_int size)
    {
        void* result = NULL;
        z_mem* zmem = NULL;
        uint32_t total = num_items * size;
        uint32_t allocSize =  total + sizeof(zmem);
        
        zmem = (z_mem*)IOMalloc(allocSize);
        
        if (zmem)
        {
            zmem->alloc_size = allocSize;
            result = (void*)&(zmem->data);
        }
        
        return result;
    }
    
    void z_free(void* notused __unused, void* ptr)
    {
        uint32_t* skipper = (uint32_t *)ptr - 1;
        z_mem* zmem = (z_mem*)skipper;
        IOFree((void*)zmem, zmem->alloc_size);
    }
};

/*
 * Decompress the firmware using zlib inflate (If not compressed, return data normally)
 */
OSData* BrcmFirmwareStore::decompressFirmware(OSData* firmware)
{
    z_stream zstream;
    int zlib_result;
    int bufferSize = 0;
    void* buffer = NULL;
    OSData* result = NULL;
    
    // Verify if the data is compressed
    uint16_t* magic = (uint16_t*)firmware->getBytesNoCopy();
    
    if (*magic != 0x0178     // Zlib no compression
        && *magic != 0x9c78  // Zlib default compression
        && *magic != 0xda78) // Zlib maximum compression
    {
        // Copy the data as-is
        result = OSData::withData(firmware);
        OSSafeRelease(firmware);
        return result;
    }
    
    bufferSize = firmware->getLength() * 4;
    
    buffer = IOMalloc(bufferSize);
    
    bzero(&zstream, sizeof(zstream));
    
    zstream.next_in   = (unsigned char*)firmware->getBytesNoCopy();
    zstream.avail_in  = firmware->getLength();
    
    zstream.next_out  = (unsigned char*)buffer;
    zstream.avail_out = bufferSize;
    
    zstream.zalloc    = z_alloc;
    zstream.zfree     = z_free;
    
    zlib_result = inflateInit(&zstream);
    
    if (zlib_result != Z_OK)
    {
        IOFree(buffer, bufferSize);
        return NULL;
    }
    
    zlib_result = inflate(&zstream, Z_FINISH);
    
    if (zlib_result == Z_STREAM_END || zlib_result == Z_OK)
        // Allocate final result
        result = OSData::withBytes(buffer, (unsigned int)zstream.total_out);
    
    inflateEnd(&zstream);
    IOFree(buffer, bufferSize);
    
    return result;
}

/**********************************************
 * IntelHex firmware parsing
 **********************************************/
#define HEX_LINE_PREFIX ':'
#define HEX_HEADER_SIZE 4

#define REC_TYPE_DATA 0 // Data
#define REC_TYPE_EOF 1  // End of File
#define REC_TYPE_ESA 2  // Extended Segment Address
#define REC_TYPE_SSA 3  // Start Segment Address
#define REC_TYPE_ELA 4  // Extended Linear Address
#define REC_TYPE_SLA 5  // Start Linear Address

/*
 * Validate if the current character is a valid hexadecimal character
 */
static inline bool validHexChar(unsigned char hex)
{
    return (hex >= 'a' && hex <= 'f') || (hex >= 'A' && hex <= 'F') || (hex >= '0' && hex <= '9');
}

/*
 * Convert char '0-9,A-F' to hexadecimal values
 */
static inline void hex_nibble(unsigned char hex, uint8_t &output)
{
    output <<= 4;
    
    if (hex >= 'a')
        output |= (0x0A + (hex - 'a')) & 0x0F;
    if (hex >= 'A')
        output |= (0x0A + (hex - 'A')) & 0x0F;
    else
        output |= (hex - '0') & 0x0F;
}

/*
 * Two's complement checksum
 */
static char check_sum(const uint8_t* data, uint16_t len)
{
    uint32_t crc = 0;
    
    for (int i = 0; i < len; i++)
        crc += *(data + i);
    
    return (~crc + 1) & 0xFF;
}

OSArray* BrcmFirmwareStore::parseFirmware(OSData* firmwareData)
{
    // Vendor Specific: Launch RAM
    uint8_t HCI_VSC_LAUNCH_RAM[] { 0x4c, 0xfc };
    
    OSArray* instructions = OSArray::withCapacity(1);
    unsigned char* data = (unsigned char*)firmwareData->getBytesNoCopy();
    uint32_t address = 0;
    unsigned char binary[0x110];
    
    if (*data != HEX_LINE_PREFIX)
    {
        DEBUG_LOG("%s::parseFirmware - Invalid firmware data.\n", this->getName());
        goto exit_error;
    }
    
    while (*data == HEX_LINE_PREFIX)
    {
        bzero(binary, sizeof(binary));
        data++;
        
        int offset = 0;
        
        // Read all hex characters for this line
        while (validHexChar(*data))
        {
            hex_nibble(*data++, binary[offset]);
            hex_nibble(*data++, binary[offset++]);
        }
        
        // Parse line data
        uint8_t length = binary[0];
        uint16_t addr = binary[1] << 8 | binary[2];
        uint8_t record_type = binary[3];
        uint8_t checksum = binary[HEX_HEADER_SIZE + length];
        
        uint8_t calc_checksum = check_sum(binary, HEX_HEADER_SIZE  + length);
        
        if (checksum != calc_checksum)
        {
            DEBUG_LOG("%s::parseFirmware - Invalid firmware, checksum mismatch.\n", this->getName());
            goto exit_error;
        }
        
        // ParseFirmware class only supports I32HEX format
        switch (record_type)
        {
            // Data
            case REC_TYPE_DATA:
            {
                address = (address & 0xFFFF0000) | addr;
                
                // Reserved 4 bytes for the address
                length += 4;
                
                // Allocate instruction (Opcode - 2 bytes, length - 1 byte)
                OSData* instruction = OSData::withCapacity(3 + length);
                
                instruction->appendBytes(HCI_VSC_LAUNCH_RAM, sizeof(HCI_VSC_LAUNCH_RAM));
                instruction->appendBytes(&length, sizeof(length));
                instruction->appendBytes(&address, sizeof(address));
                instruction->appendBytes(&binary[4], length - 4);
                
                instructions->setObject(instruction);
                break;
            }
            // End of File
            case REC_TYPE_EOF:
                return instructions;
            // Extended Segment Address
            case REC_TYPE_ESA:
                // Segment address multiplied by 16
                address = binary[4] << 8 | binary[5];
                address <<= 4;
                break;
                // Start Segment Address
            case REC_TYPE_SSA:
                // Set CS:IP register for 80x86
                DEBUG_LOG("%s::parseFirmware - Invalid firmware, unsupported start segment address instruction.\n", this->getName());
                goto exit_error;
                // Extended Linear Address
            case REC_TYPE_ELA:
                // Set new higher 16 bits of the current address
                address = binary[4] << 24 | binary[5] << 16;
                break;
                // Start Linear Address
            case REC_TYPE_SLA:
                // Set EIP of 80386 and higher
                DEBUG_LOG("%s::parseFirmware - Invalid firmware, unsupported start linear address instruction.\n", this->getName());
                goto exit_error;
            default:
                DEBUG_LOG("%s::parseFirmware - Invalid firmware, unknown record type encountered: 0x%02x.\n", this->getName(), record_type);
                goto exit_error;
        }
        
        // Skip over any trailing newlines / whitespace
        while (!validHexChar(*data) && !(*data == HEX_LINE_PREFIX))
            data++;
    }
    
    DEBUG_LOG("%s::parseFirmware - Invalid firmware.\n", this->getName());
    
exit_error:
    OSSafeRelease(instructions);
    return NULL;
}

OSDefineMetaClassAndStructors(BrcmFirmwareStore, IOService)

IOService* BrcmFirmwareStore::probe(IOService *provider, SInt32 *probeScore)
{
    DEBUG_LOG("%s::probe\n", this->getName());
    return super::probe(provider, probeScore);
}

bool BrcmFirmwareStore::init(OSDictionary *dictionary)
{
    DEBUG_LOG("BrcmFirmwareStore::init\n"); // this->getName() is not available yet
    return super::init(dictionary);
}

bool BrcmFirmwareStore::start(IOService *provider)
{
    DEBUG_LOG("%s::start\n", this->getName());
    
    if (!super::start(provider))
        return false;
    
    mFirmwares = OSDictionary::withCapacity(1);
    
    IOService::publishResource(kBrcmFirmwareStoreService, this);
  
    return true;
}

void BrcmFirmwareStore::stop(IOService *provider)
{
    DEBUG_LOG("%s::stop\n", this->getName());
    
    OSSafeRelease(mFirmwares);
    
    super::stop(provider);
}

OSArray* BrcmFirmwareStore::loadFirmware(OSString* firmwareKey)
{
    DEBUG_LOG("%s::loadFirmware\n", this->getName());
    
    OSDictionary* firmwares = OSDynamicCast(OSDictionary, this->getProperty("Firmwares"));
    
    if (!firmwares)
    {
        IOLog("%s: Unable to locate BrcmFirmwareStore configured firmwares.\n", this->getName());
        return NULL;
    }
    
    OSData* configuredData = OSDynamicCast(OSData, firmwares->getObject(firmwareKey));
    
    if (!configuredData)
    {
        IOLog("%s: No firmware for firmware key \"%s\".\n", this->getName(), firmwareKey->getCStringNoCopy());
        return NULL;
    }
    
    IOLog("%s: Retrieved firmware for firmware key \"%s\".\n", this->getName(), firmwareKey->getCStringNoCopy());
    
    OSData* firmwareData = decompressFirmware(configuredData);
    
    if (!firmwareData)
    {
        IOLog("%s: Failed to decompress firmware.\n", this->getName());
        return NULL;
    }
    
    if (configuredData->getLength() < firmwareData->getLength())
        IOLog("%s: Decompressed firmware (%d bytes --> %d bytes).\n", this->getName(), configuredData->getLength(), firmwareData->getLength());
    else
        IOLog("%s: Non-compressed firmware.\n", this->getName());
    
    OSArray* instructions = parseFirmware(firmwareData);
    OSSafeRelease(firmwareData);
    
    if (!instructions)
    {
        IOLog("%s: Firmware is not valid IntelHex firmware.\n", this->getName());
        return NULL;
    }
    
    DEBUG_LOG("%s: Firmware is valid IntelHex firmware.\n", this->getName());
    
    return instructions;
}

OSArray* BrcmFirmwareStore::getFirmware(OSString* firmwareKey)
{
    DEBUG_LOG("%s::getFirmware\n", this->getName());
    
    if (!firmwareKey || firmwareKey->getLength() == 0)
    {
        IOLog("%s: Current device has no FirmwareKey configured.\n", this->getName());
        return NULL;
    }
    
    OSArray* instructions = OSDynamicCast(OSArray, mFirmwares->getObject(firmwareKey));
    
    // Cached instructions found for firmwareKey?
    if (!instructions)
    {
        // Load instructions for firmwareKey
        instructions = loadFirmware(firmwareKey);
        
        // Add instructions to the firmwares cache
        if (instructions)
            mFirmwares->setObject(firmwareKey, instructions);
    }
    else
     IOLog("%s: Retrieved cached firmware for \"%s\".\n", this->getName(), firmwareKey->getCStringNoCopy());
    
    return instructions;
}


