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

#include "zlib.h"

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
unsigned char* decompressFirmware(OSData* firmware, uint32_t* uncompressedSize)
{
    z_stream zstream;
    int zlib_result;
    int bufferSize = 0;
    void* buffer = NULL;
    void* result = NULL;
    
    // Verify if the data is compressed
    uint16_t* magic = (uint16_t*)firmware->getBytesNoCopy();
    
    if (*magic != 0x0178 // Zlib no compression
        && *magic != 0x9c78 // Zlib default compression
        && *magic != 0xda78) // Zlib maximum compression
    {
        // Copy the data as-is
        *uncompressedSize = firmware->getLength();
        result = IOMalloc(*uncompressedSize);
        memcpy(result, firmware->getBytesNoCopy(), *uncompressedSize);
        
        return (unsigned char*)result;
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
    {
        // Allocate final result
        *uncompressedSize = (uint32_t)zstream.total_out;
        result = IOMalloc(*uncompressedSize);
        
        memcpy(result, buffer, *uncompressedSize);
    }
    
    inflateEnd(&zstream);    
    IOFree(buffer, bufferSize);
    
    return (unsigned char*)result;
}

