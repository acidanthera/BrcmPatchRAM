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

#include "Common.h"
#include "IntelHex.h"

#define HEX_LINE_PREFIX ':'
#define HEX_HEADER_SIZE 4

#define REC_TYPE_DATA 0 // Data
#define REC_TYPE_EOF 1  // End of File
#define REC_TYPE_ESA 2  // Extended Segment Address
#define REC_TYPE_SSA 3  // Start Segment Address
#define REC_TYPE_ELA 4  // Extended Linear Address
#define REC_TYPE_SLA 5  // Start Linear Address

// Vendor Specific: Launch RAM
uint8_t HCI_VSC_LAUNCH_RAM[] { 0x4c, 0xfc };

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

IntelHex::IntelHex()
{
    mInstructions = OSArray::withCapacity(1);
}

IntelHex::~IntelHex()
{
    OSSafeReleaseNULL(mInstructions);
}

bool IntelHex::load(unsigned char* data)
{
    uint32_t address = 0;
    unsigned char binary[0x110];
    
    if (*data != HEX_LINE_PREFIX)
    {
        DEBUG_LOG("IntelHex::load - Invalid firmware data.\n");
        return false;
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
            DEBUG_LOG("IntelHex::load - Invalid firmware, checksum mismatch.\n");
            return false;
        }
        
        // IntelHex class only supports I32HEX
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
                
                mInstructions->setObject(instruction);
                break;
            }
            // End of File
            case REC_TYPE_EOF:
                return true;
            // Extended Segment Address
            case REC_TYPE_ESA:
                // Segment address multiplied by 16
                address = binary[4] << 8 | binary[5];
                address <<= 4;
                break;
            // Start Segment Address
            case REC_TYPE_SSA:
                // Set CS:IP register for 80x86
                DEBUG_LOG("IntelHex::load - Invalid firmware, unsupported start segment address instruction.\n");
                return false;
            // Extended Linear Address
            case REC_TYPE_ELA:
                // Set new higher 16 bits of the current address
                address = binary[4] << 24 | binary[5] << 16;
                break;
            // Start Linear Address
            case REC_TYPE_SLA:
                // Set EIP of 80386 and higher
                DEBUG_LOG("IntelHex::load - Invalid firmware, unsupported start linear address instruction.\n");
                return false;
            default:
                DEBUG_LOG("IntelHex::load - Invalid firmware, unknown record type encountered: 0x%02x.\n", record_type);
                return false;
        }
        
        // Skip over any trailing newlines / whitespace
        while (!validHexChar(*data) && !(*data == HEX_LINE_PREFIX))
            data++;
    }
    
    DEBUG_LOG("IntelHex::load - Invalid firmware.\n");
    return false;
}

/*
 * Obtain iterator for parsed instructions
 */
OSCollectionIterator* IntelHex::getInstructions()
{
    return OSCollectionIterator::withCollection(mInstructions);
}