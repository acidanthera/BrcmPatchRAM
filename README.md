BrcmPatchRAM
============

Broadcom Bluetooth devices require "PatchRAM" updates to work successfully.
"PatchRAM" updates are volatile and need to be re-applied on every machine startup.

The BrcmPatchRAM kext is an OS X driver which applies PatchRAM updates for Broadcom based devices.

Note that BrcmPatchRAM works when injected through Clover or placed in /System/Library/Extensions.

Details
=======

BrcmPatchRAM is responsible to communicate with configured Broadcom bluetooth USB devices and updates
their firmware using configured updates on the fly.

Firmwares are configured in the Info.plist file in hex format. Optionally they can be zlib compressed (in order to keep the .plist size down).

In the plugins a code-less kext BrcmNonApple.kext is included, this kext matches third-party vendor and product ids to the Apple Broadcom bluetooth USB drivers (BroadcomBluetoothHostControllerUSBTransport).

New devices
===========

In order to support a new device, the firmware for the device needs to be extracted from existing drivers.
The Windows drivers usually place the hex firmware file in \Windows\System32\Drivers, from where it can be copied.

The firmware hex can then be optionally compressed using zlib.pl from Revogirl (its included in the Firmwares folder).

After that configure a new IOKit Personality with the correct vendor id and product id and add the Firmware as a plist data entry.
Additionally configure the correct vendor id and product id in the code-less BrcmNonApple.kext in order to load the Apple drivers.