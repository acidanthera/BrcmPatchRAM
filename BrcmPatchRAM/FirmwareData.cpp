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
#include "FirmwareData.h"
#include "../GeneratedFirmwares.cpp"

#include <string.h>

OSData* lookupFirmware(const char* filename)
{
    OSData* result = NULL;
    for (const FirmwareEntry* entry = firmwares; entry->filename != NULL; entry++)
    {
        if (0 == strcmp(filename, entry->filename))
        {
            result = OSData::withBytes(entry->firmwareData, (unsigned int)entry->firmwareSize);
            break;
        }
    }
    return result;
}

