BrcmPatchRAM
============

[![Build Status](https://github.com/acidanthera/BrcmPatchRAM/actions/workflows/main.yml/badge.svg?branch=master)](https://github.com/acidanthera/BrcmPatchRAM/actions) [![Scan Status](https://scan.coverity.com/projects/22191/badge.svg?flat=1)](https://scan.coverity.com/projects/22191)

### Translate Language

- [简体中文](./README_CN.md)
- English

Most Broadcom USB Bluetooth devices make use of a system called RAMUSB. RAMUSB allows the firmware for the device to be updated on-the-fly, however any updates previously applied are lost when shutting down the machine.

The Broadcom Windows driver will upload firmware into the Broadcom Bluetooth device on every startup, however for macOS this functionality is not supported out of the box. BrcmPatchRAM kext is a macOS driver which applies PatchRAM updates for Broadcom RAMUSB based devices. It will apply the firmware update to your Broadcom Bluetooth device on every startup / wakeup, identical to the Windows drivers. The firmware applied is extracted from the Windows drivers and the functionality should be equal to Windows.

Note that the original Apple Broadcom bluetooth devices are not RAMUSB devices, and thus do not have the same firmware mechanism.

### Installation

__Note if you have an Apple MacBook/iMac/Mac Pro etc, follow the [Mac instructions](https://github.com/acidanthera/BrcmPatchRAM/blob/master/README-Mac.md)__


Install one of BrcmPatchRAM.kext or BrcmPatchRAM2.kext or BrcmPatchRAM3.kext depending on macOS version, never both.

  * BrcmPatchRAM.kext: for 10.10 or earlier.

  * BrcmPatchRAM2.kext: for 10.11 - 10.14.
  
  * BrcmPatchRAM3.kext: for 10.15 or later.


Also, install one firmware kext BrcmFirmwareData.kext or BrcmFirmwareRepo.kext, depending on installation location, never both.

  * BrcmFirmwareData.kext: Most appropriate for bootloader injection. This is the preferred configuration.

  * BrcmFirmwareRepo.kext: Install to /System/Library/Extensions (/Library/Extensions on 10.11 and later). This kext is slightly more memory efficient than BrcmFirmwareData.kext, but cannot be injected by a bootloader.

  * Advanced users: For custom firmware injectors, install the injector plus BrcmFirmwareRepo.kext.  This works from either /S/L/E or through bootloader injection.  Optionally, you may remove all the firmwares from BrcmFirmwareRepo.kext/Contents/Resources.  If you're using the injector through the bootloader, the IOProviderClass in the Info.plist for BrcmFirmwareRepo.kext must be changed from "disabled_IOResources" to "IOResources".

Keep in mind that BrcmPatchRAM3.kext also requires BrcmBluetoothInjector.kext to be installed. Starting with macOS 10.15, this is the only supported configuration because due to framework changes BrcmPatchRAM.kext and BrcmPatchRAM2.kext are incompatible with macOS 10.15. In case you forget to install BrcmBluetoothInjector.kext, Bluetooth will appear to be available but it won't work at all. 

Also, if you have a non-PatchRAM device (or you're not sure), install one of BrcmNonPatchRAM.kext or BrcmNonPatchRAM2.kext, depending on macOS version, never both.  Although these kexts do not install any firmware (these devices have firmware built-in), they still depend on BrcmPatchRAM/BrcmPatchRAM2.kext.

  * BrcmNonPatchRAM.kext: for 10.10 or earlier
  * BrcmNonPatchRAM2.kext: for 10.11 or later.

### BrcmBluetoothInjector.kext

To be used for macOS 10.11-11. Use BrcmBluetoothInjectorLegacy.kext for older systems. Using BrcmPatchRAM3.kext also requires BrcmBluetoothInjector.kext as changes in macOS Catalina (10.15) requires the use of a separate injector kext. This is due to the removal of the following IOCatalogue methods:

    IOCatalogue::addDrivers, IOCatalogue::removeDrivers and IOCatalogue::startMatching

Consequently to have the native BT driver load for the device (BroadcomBluetoothHostControllerUSBTransport) we inject using a plist with a slightly lower IOProbeScore than BrcmPatchRAM3 so it doesn't probe before the firmware upload.

The BrcmBluetoothInjector.kext is a [codeless kernel extension](https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/KEXTConcept/KEXTConceptAnatomy/kext_anatomy.html) which injects the BT hardware data using a plist; it does not contain a firmware uploader. You might also want to try this kext if you wish to see if your device will work without a firmware uploader.

Do not use BrcmPatchRAM or BrcmPatchRAM2 with this kext.

BrcmBluetoothInjector supported devices:

  * ``[0489:e030]`` BCM2070 - BCM943224HMB, BCM943225HMB Combo
  * ``[0489:e032]`` 20702 E032 Combo
  * ``[0489:e042]`` 20702A1 Lenovo China standalone
  * ``[0489:e046]`` 20702A1 Acer 43228+20702 combo card
  * ``[0489:e04f]`` 20702A1 Lenovo China 43227 WLAN + 20702A1 Combo card
  * ``[0489:e052]`` 20702 non-UHE Generic
  * ``[0489:e055]`` 43142A0 Acer combo
  * ``[0489:e059]`` Acer 43228 + 20702A1 combo
  * ``[0489:e079]`` Lenovo China 43162 NGFF
  * ``[0489:e07a]`` Lenovo China 4352+20702 NGFF
  * ``[0489:e087]`` Acer 43228 NGFF combo module
  * ``[0489:e096]`` BCM43142A0
  * ``[0489:e097]`` Acer Foxconn BCM4356A2 NGFF
  * ``[0489:e0a1]`` 20703A1 Lenovo 43602 NGFF combo
  * ``[04b4:f901]`` Laird BT851 Bluetooth 5.0 USB dongle
  * ``[04ca:2003]`` 20702A1 Lenovo China standalone
  * ``[04ca:2004]`` LiteOn 43228+20702 combo
  * ``[04ca:2005]`` LiteOn 43228+20702 combo
  * ``[04ca:2006]`` LiteOn 43142 combo
  * ``[04ca:2009]`` LiteOn 43142 combo
  * ``[04ca:200a]`` LiteOn 4352 combo
  * ``[04ca:200b]`` LiteOn 4352 combo
  * ``[04ca:200c]`` LiteOn 4352 combo
  * ``[04ca:200e]`` Liteon 43228 NGFF combo
  * ``[04ca:200f]`` Acer_LiteOn BCM20702A1_4352
  * ``[04ca:2012]`` Acer BCM943142Y NGFF
  * ``[04ca:2013]`` Acer LiteOn BCM4356A2 NGFF
  * ``[04ca:2014]`` Asus LiteOn BCM4356A2 NGFF
  * ``[04ca:2016]`` Lenovo 43162 NGFF combo module
  * ``[04f2:b4a1]`` ASUS Chicony BCM43142A0 NGFF
  * ``[04f2:b4a2]`` BCM4356A2
  * ``[050d:065a]`` 20702 standalone
  * ``[0930:021e]`` 20702A1 Toshiba standalone
  * ``[0930:021f]`` Toshiba 43142
  * ``[0930:0221]`` 20702A1 Toshiba 4352
  * ``[0930:0223]`` 20702A1 Toshiba 4352
  * ``[0930:0225]`` Toshiba 43142 combo NGFF
  * ``[0930:0226]`` Toshiba 43142 combo NGFF
  * ``[0930:0229]`` 43162 combo NGFF
  * ``[0a5c:2168]`` BRCM Generic 43162Z
  * ``[0a5c:2169]`` BRCM Generic 43228z
  * ``[0a5c:216a]`` Dell DW1708 43142Y combo
  * ``[0a5c:216b]`` HP Rapture 4352z ngff combo
  * ``[0a5c:216c]`` HP Harrier 43142
  * ``[0a5c:216d]`` HP Hornet 43142Y ngff combo
  * ``[0a5c:216e]`` HP Blackbird 43162 NGFF
  * ``[0a5c:216f]`` Dell DW1560 4352+20702 M.2
  * ``[0a5c:217d]`` BCM2070 - BCM943224HMB, BCM943225HMB Combo
  * ``[0a5c:21d7]`` BRCM Generic 43142A0 RAMUSB
  * ``[0a5c:21de]`` 4352+20702A1 combo
  * ``[0a5c:21e1]`` 20702A1 non-UHE HP SoftSailing
  * ``[0a5c:21e3]`` 20702A1 non-UHE 4313 combo HP Valentine
  * ``[0a5c:21e6]`` 20702 non-UHE Lenovo Japan
  * ``[0a5c:21e8]`` 20702A1 dongles
  * ``[0a5c:21ec]`` 20702A1 REF6 OTP module standalone
  * ``[0a5c:21f1]`` 43228 combo
  * ``[0a5c:21f3]`` Lenovo Edge 43228 + 20702A1 combo
  * ``[0a5c:21f4]`` Lenovo Edge 4313 + 20702A1 combo
  * ``[0a5c:21fb]`` HP Supra 4352 20702A1 combo
  * ``[0a5c:21fd]`` BRCM Generic 4352z RAMUSB
  * ``[0a5c:640a]`` BRCM Generic Reference 4356
  * ``[0a5c:640b]`` HP Luffy 43228 + 20702 M.2
  * ``[0a5c:640e]`` Lenovo 4356 NGFF combo
  * ``[0a5c:6410]`` 20703A1 RAM download - DW1830 43602
  * ``[0a5c:6412]`` Dell 4350C5
  * ``[0a5c:6413]`` Broadcom Generic 4350C5
  * ``[0a5c:6414]`` Lenovo 4350C5
  * ``[0a5c:6417]`` Zebra 4352
  * ``[0a5c:6418]`` HP Brook 2x2ac
  * ``[0a5c:7460]`` 20703A1 RAM download
  * ``[0a5c:828d]`` Fenvi BCM94352Z
  * ``[0b05:178a]`` BCM2070 - BCM943224HMB, BCM943225HMB Combo
  * ``[0b05:17b5]`` Asus 43228+20702A1 combo
  * ``[0b05:17cb]`` 20702 standalone
  * ``[0b05:17cf]`` Asus 4352_20702A1 combo
  * ``[0b05:180a]`` Azurewave 4360+20702 combo
  * ``[0b05:181d]`` Asus AZUREWAVE MB BCM4356A2
  * ``[0bb4:0306]`` 20703A1 HTC runtime RAM dongle
  * ``[105b:e065]`` LenovoChina 43142A0 combo
  * ``[105b:e066]`` LenovoChina 43228+20702 combo
  * ``[13d3:3384]`` 20702A1 Azurewave standalone
  * ``[13d3:3388]`` BRCM Generic 43142A0 RAMUSB
  * ``[13d3:3389]`` BRCM Generic 43142A0 RAMUSB
  * ``[13d3:3392]`` Azurewave 43228+20702
  * ``[13d3:3404]`` 4352HMB Azurewave Module
  * ``[13d3:3411]`` Dell Alienware 4352 20702A1 combo
  * ``[13d3:3413]`` Azurewave 4360+20702 combo
  * ``[13d3:3418]`` Azurewave 4352+20702 combo module
  * ``[13d3:3427]`` Toshiba 43142 combo NGFF
  * ``[13d3:3435]`` AZUREWAVE BCM20702A1_4352
  * ``[13d3:3456]`` AZUREWAVE BCM20702A1_4352
  * ``[13d3:3473]`` Asus AZUREWAVE BCM4356A2 NGFF
  * ``[13d3:3482]`` AZUREWAVE BCM43142A0 NGFF
  * ``[13d3:3484]`` Acer AZUREWAVE BCM43142A0 NGFF
  * ``[13d3:3485]`` Asus AZUREWAVE BCM4356A2 NB 2217NF
  * ``[13d3:3488]`` Asus AZUREWAVE BCM4356A2 NB 2210
  * ``[13d3:3492]`` Asus AZUREWAVE BCM4356A2 NGFF
  * ``[13d3:3504]`` AW CM217NF BCM4371C2
  * ``[13d3:3508]`` AW ASUS CM217NF BCM4371C2
  * ``[13d3:3517]`` AW CE160H BCM20702
  * ``[145f:01a3]`` 20702A1 Asus Trust standalone
  * ``[2b54:5600]`` Emdoor AP6356SD BCM4356A2
  * ``[2b54:5601]`` Asus AP6356SDP1A BCM4356A2
  * ``[2b54:5602]`` AMPAK AP6356SDP2A BCM4356A2
  * ``[33ba:03e8]`` TOULINEUA BCM94360Z4 4360+20702 combo
  * ``[33ba:03e9]`` TOULINEUA BCM94360Z3 4360+20702 combo  
  * ``[413c:8143]`` DW1550 4352+20702 combo
  * ``[413c:8197]`` Dell DW380 Nancy Blakes standalone

If yours is not present, edit the Info.plist as needed.  

### BlueToolFixup.kext
  
Required for macOS 12 or newer, as in macOS 12 Apple has changed parts of the Bluetooth stack from kernel-space to user-space as detailed in [here](https://github.com/acidanthera/bugtracker/issues/1669) and [here](https://github.com/acidanthera/BrcmPatchRAM/pull/12). Requires Lilu 1.5.4+  
  
Do not use it with BrcmBluetoothInjector for macOS 12 or newer.

### Supported Devices

BrcmPatchRAM supports any Broadcom USB bluetooth device based on the BCM20702 chipset (possibly other chipsets are supported also, but this has not been tested).

The following devices are supported at the moment:

*Devices marked with * are tested successfully*

Non-PatchRAM devices (BrcmPatchRAM used to speed up recovery after sleep):

  * ``[03f0:231d]`` HP 231d (ProBook BT built-in firmware)
  * ``[13d3:3295]`` Azurewave BCM943225 (20702A bult-in firmware)

Tested PatchRAM devices:
  * ``[0489:e032]`` 20702 Combo USB
  * ``[0489:e042]`` 20702A1 Lenovo China *
  * ``[0489:e079]`` Lenovo China 43162 NGFF
  * ``[0489:e07a]`` Lenovo NGFF (4352 / 20702)
  * ``[04ca:2003]`` 20702A1 Lenovo China
  * ``[04ca:200a]`` LiteOn (4352 Combo)
  * ``[04ca:200b]`` LiteOn (4352 Combo) *
  * ``[04ca:200c]`` LiteOn (4352 Combo)
  * ``[04ca:200f]`` Acer / LiteOn (4352 Combo) 
  * ``[050d:065a]`` Belkin (20702)
  * ``[0930:0221]`` Toshiba (4352 / 20702)
  * ``[0930:0223]`` Toshiba NGFF (4352 / 20702) *
  * ``[0a5c:216b]`` HP Rapture 4352Z NGFF Combo
  * ``[0a5c:216e]`` HP Blackbird 43162 NGFF
  * ``[0a5c:216f]`` Dell DW1560 (4352/20702)
  * ``[0a5c:21de]`` 4352/20702A1 combo
  * ``[0a5c:21e1]`` HP Softsailing (20702A1)
  * ``[0a5c:21e6]`` non-UHE Lenovo Bluetooth (20702)
  * ``[0a5c:21e8]`` Bluetooth USB Dongle (20702A1) *
  * ``[0a5c:21ec]`` Inateck Bluetooth (20702A1)
  * ``[0a5c:21fb]`` HP Supra 4352 (20702A1 Combo)
  * ``[0a5c:21fd]`` Broadcom 4352Z
  * ``[0a5c:22be]`` Broadcom BCM20702 Bluetooth 4.0 USB Device
  * ``[0a5c:6410]`` Dell Wireless 1830 Bluetooth 4.1 LE
  * ``[0a5c:6412]`` Dell Wireless 1820 Bluetooth 4.1 LE
  * ``[0a5c:828d]`` Fenvi BCM94352Z
  * ``[0b05:17cb]`` Asus BT-400 (20702 stand-alone) *
  * ``[0b05:17cf]`` Asus (4352/20702A1 combo) *
  * ``[0b05:180a]`` Azurewave (4360/20702 combo)
  * ``[13d3:3404]`` Azurewave (4352HMB) *
  * ``[13d3:3411]`` Dell Alienware (4352/20702A1 combo) *
  * ``[13d3:3413]`` Azurewave (4360/20702 combo)
  * ``[13d3:3418]`` Azurewave (4352/20702 combo)
  * ``[13d3:3435]`` Azurewave (4352/20702 combo)
  * ``[13d3:3456]`` Azurewave (4352/20702 combo)
  * ``[413c:8143]`` Dell DW1550 (4352/20702 combo)

All of the firmwares from the Windows package are present in the kext and automatically associated with their vendor/device-ids.  They are expected to work, but have not been confirmed.  If you can confirm a working device not listed above, please notify via the "issues" database on github. The firmwares have been updated to version 12.0.1.1105.


### More Installation Details

BrcmPatchRAM.kext, BrcmPatchRAM2.kext and BrcmPatchRAM3.kext can be installed either through bootloader kext injection or placed in /System/Library/Extensions (/Library/Extensions on 10.11 and later).
Make sure to install only one of them, depending on system version.

BrcmFirmwareRepo.kext does not work with bootloader kext injection, unless using a device specific firmware injector.
BrcmFirmwareData.kext can work with bootloader kext injection.

You can also use a device specific firmware injector (in conjunction with BrcmFirmwareRepo.kext).  In this scenario, BrcmFirmwareRepo.kext does work from bootloader kexts.

You will find device specfic injectors in the 'firmwares' directory of the git repository.  They are not included in the distribution ZIP.


### Configuration

There are a number of delays which can be changed with the following kernel boot arguments. You might change these values if you find BrcmPatchRAM is hanging during firmware load. Refer to the source for futher details on these delays.

- `bpr_initialdelay`: Changes `mInitialDelay`, the delay in ms before any communication happens with the device. Default value is `100`.
- `bpr_handshake`: Overrides `mSupportsHandshake`, firmware uploaded handshake support status. `0` means wait `bpr_preresetdelay` ms after uploading firmware, and then reset the device. `1` means wait for a specific response from the device and then reset the device. Default value depends on the device identifier.
- `bpr_preresetdelay`: Changes `mPreResetDelay`, the delay in ms assumed to be needed for the device to accept the firmware. The value is unused when `bpr_handshake` is `1` (passed manually or applied automatically based on the device identifier). Default value is `250`.
- `bpr_postresetdelay`: Changes `mPostResetDelay`, the delay in ms assumed to be needed for the firmware to initialise after reseting the device upon firmware upload. Default value is `100`.
- `bpr_probedelay`: Changes `mProbeDelay` (removed in BrcmPatchRAM3), the delay in ms before probing the device. Default value is `0`.

For example, to change `mPostResetDelay` to 400 ms, use the kernel boot argument: `bpr_postresetdelay=400`.

Note: Some with the typical "wake from sleep" problems are reporting success with: `bpr_probedelay=100 bpr_initialdelay=300 bpr_postresetdelay=300`.  Or slightly longer delays: `bpr_probedelay=200 bpr_initialdelay=400 bpr_postresetdelay=400`.

On macOS 12.4 and newer versions, a new address check has been introduced in `bluetoothd`, thus an error will be triggered if two Bluetooth devices have the same address. However, this check can be circumvented by adding the boot argument `-btlfxallowanyaddr`.

### Details

BrcmPatchRAM consists of 2 parts:

 * BrcmPatchRAM itself communicates with supported Broadcom Bluetooth USB devices (as configured in the Info.plist), and detects if they require a firmware update. 

  If a firmware update is required, the matching firmware data will be uploaded to the device and the device will be reset.
	
 * BrcmFirmwareStore (implemented by either BrcmFirmwareData.kext or BrcmFirmwareRepo.kext) is a shared resource which holds all the configured firmwares for different Broadcom Bluetooth USB devices.

   Some devices require device specific firmware, while others can use the newest version available in the Windows drivers without issue.

   New firmwares are added/configured on a regular basis to support devices, so be sure to follow release updates, or log an issue if you find your device is not supported.

	Firmwares can be stored using zlib compression in order to keep the configuration size manageable.
	

After the device firmware is uploaded, the device control is handed over to Apple's BroadcomBluetoothHostControllerUSBTransport.
This means that for all intents and purposes your device will be native on macOS and support all functionalities fully.

It is possible to use the Continuity Activation Patch in combination with BrcmPatchRAM through [BT4LEContinuityFixup](https://github.com/acidanthera/BT4LEContinuityFixup), or through dokterdok's script [Continuity-Activation-Tool](https://github.com/dokterdok/Continuity-Activation-Tool), or through [OpenCore](https://github.com/acidanthera/OpenCorePkg) quirk `ExtendBTFeatureFlags`.  


### Troubleshooting

After installing BrcmPatchRAM, even though your Bluetooth icon may show up, it could be that the firmware has not been properly updated.

Verify the firmware is updated by going to System Information and check the Bluetooth firmware version number under the Bluetooth information panel.

If the version number is "4096", this means no firmware was updated for your device and it will not work properly.

Verify any errors in the system log by running the following command in the terminal:
```bash
    # For 10.12 and newer:
    log show --last boot | grep -i brcm[fp]
    # For older macOS versions:
    cat /var/log/system.log | grep -i brcm[fp]
```
Ensure you check only the latest boot messages, as the system.log might go back several days.

If the firmware upload failed with an error, try installing the debug version of BrcmPatchRAM in order to get more detailed information in the log.

In order to report an error log an issue on github with the following information:

 * Device product ID
 * Device vendor ID
 * BrcmPatchRAM version used
 * Dump of BrcmPatchRAM debug output from /var/log/system.log showing the firmware upload failure


### Firmware Compatibility

Some USB devices are very firmware specific and trying to upload any other firmware for the same chipset into them will fail.

This usually displays in the system log as:
    
  BrcmPatchRAM: Version 0.5 starting.
	BrcmPatchRAM: USB [0a5c:21e8 5CF3706267E9 v274] "BCM20702A0" by "Broadcom Corp"
	BrcmPatchRAM: Retrieved firmware for firmware key "BCM20702A1_001.002.014.1443.1612_v5708".
	BrcmPatchRAM: Decompressed firmware (29714 bytes --> 70016 bytes).
	BrcmPatchRAM: device request failed (0xe000404f).
	BrcmPatchRAM: Failed to reset the device (0xe00002d5).
	BrcmPatchRAM: Unable to get device status (0xe000404f).
	BrcmPatchRAM: Firmware upgrade completed successfully.

The errors in between mean the firmware was not uploaded successfully, and the device will most likely need a specific firmware configured.

For other devices the newest firmware available (even though not specified specifically in the Windows drivers) works fine.


### New devices

In order to support a new device, the firmware for the device needs to be extracted from existing Windows drivers.

A copy of the (current) latest Broadcom USB bluetooth drivers can be found here:
http://drivers.softpedia.com/get/BLUETOOTH/Broadcom/ASUS-X99-DELUXE-Broadcom-Bluetooth-Driver-6515800-12009860.shtml#download

For an automatic update you can unpack the Broadcom USB bluetooth files and run `firmware_update.tool`.

*Should you come across newer drivers than 12.0.1.1012, please let me know.*

In order to get the device specific firmware for your device take the following steps:

 * Look up your USB device vendor and product ID, in this example we will be using the BCM94352Z PCI NGFF WiFi/BT combo card, for which the vendor is 0930 and product ID 0233.
  
 * Extract the Windows Bluetooth driver package and open the bcbtums-win8x64-brcm.inf file.
  
 * Find your vendor / device ID combination in the .inf file
```dosini
%BRCM20702.DeviceDesc%=BlueRAMUSB0223, USB\VID_0930&PID_0223       ; 20702A1 Toshiba 4352
```
 * Locate the mentioned "RAMUSB0223" device in the .inf file:
```dosini
;;;;;;;;;;;;;RAMUSB0223;;;;;;;;;;;;;;;;;
[RAMUSB0223.CopyList]
bcbtums.sys
btwampfl.sys
BCM20702A1_001.002.014.1443.1457.hex
```

 *  Copy the firmware hex file matching your device from the Windows package, in this case "BCM20702A1_001.002.014.1443.1457.hex"
	
 *  The firmware file can now optionally be compressed using the included zlib.pl script:
```bash	
zlib.pl deflate BCM20702A1_001.002.014.1443.1457.hex > BCM20702A1_001.002.014.1443.1457.zhx
```
 * After this a hex dump can be created for pasting into a plist editor:
```bash	
xxd -ps BCM20702A1_001.002.014.1443.1457.zhx|tr '\n' ' ' > BCM20702A1_001.002.014.1443.1457.dmp
```
 * Using a plist editor create a new firmware key under the *BcmFirmwareStore/Firmwares* dictionary.

  Note that the version number displayed in macOS is the last number in the file name (1457 in our sample) + 4096.

  So in this case the firmware version in macOS would be: "*c14 v5553*".
	
 * After configuring a key under *BcmFirmwareStore/Firmwares*, add your device ID as a new device for BrcmPatchRAM.

Firmwares can also be loaded directly from BrcmFirmwareRepo.kext/Contents/Resources, either by firmware key name (see above), or by naming the file with just the vendor and device-id.  For example, 0930_0223.hex (uncompressed) or 0930_0223.zhx (compressed).

 Copying an existing IOKit personality and modifying its properties is the easiest way to do this. 
 Configure the earlier firmware using its unique firmware key.

### Support and discussion  
[InsanelyMac topic](https://www.insanelymac.com/forum/topic/339175-brcmpatchram2-for-1015-catalina-broadcom-bluetooth-firmware-upload/) in English  
[AppleLife topic](https://applelife.ru/threads/bluetooth.2944352/) in Russian  
