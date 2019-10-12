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

#include <AvailabilityVersions.h>
#include <libkern/c++/OSObject.h>

#ifdef __MAC_10_15

/**
 *  Ensure the symbol is not exported
 */
#define PRIVATE __attribute__((visibility("hidden")))

/**
 *  For private fallback symbol definition
 */
#define WEAKFUNC __attribute__((weak))

// macOS 10.15 adds Dispatch function to all OSObject instances and basically
// every header is now incompatible with 10.14 and earlier.
// Here we add a stub to permit older macOS versions to link.
// Note, this is done in both kern_util and plugin_start as plugins will not link
// to Lilu weak exports from vtable.

kern_return_t WEAKFUNC PRIVATE OSObject::Dispatch(const IORPC rpc) {
    (panic)("OSObject::Dispatch plugin stub called");
}

kern_return_t WEAKFUNC PRIVATE OSMetaClassBase::Dispatch(const IORPC rpc) {
    (panic)("OSMetaClassBase::Dispatch plugin stub called");
}

#endif
