##### BrcmPatchRAM on Apple Mac

The instructions in the readme are for a [Hackintosh](http://en.wikipedia.org/wiki/OSx86), a normal PC modified to run macOS.

You should __not__ follow the original instructions on a real Mac as it might inadvertently break things.

BrcmPatchRAM.kext, BrcmPatchRAM2.kext, BrcmFirmwareRepo.kext, and BrcmFirmwareData.kext are all unsigned kernel extensions.

In order to use them, unsigned kernel extensions need to be enabled. Refer to [this page](https://developer.apple.com/library/archive/documentation/Security/Conceptual/System_Integrity_Protection_Guide/KernelExtensions/KernelExtensions.html) for more details. Hint: you need to disable System Integrity Protection.

For macOS older than 10.11, install the required kexts inside /System/Library/Extensions.
```
sudo cp -R ~/Downloads/BrcmPatchRAM.kext /System/Library/Extensions
sudo cp -R ~/Downloads/BrcmFirmwareRepo.kext /System/Library/Extensions
sudo touch /System/Library/Extensions
```

Or for 10.11 and later, to /Library/Extensions:
```
sudo cp -R ~/Downloads/BrcmPatchRAM2.kext /Library/Extensions
sudo cp -R ~/Downloads/BrcmFirmwareRepo.kext /Library/Extensions
sudo touch /Library/Extensions
```

Wait about a minute before rebooting the Mac again.

If all works properly the firmware version in the Bluetooth profiler will now show a higher version than v4096 (4096 means its non-upgraded).

Additionally its possible to confirm BrcmPatchRAM did its job by executing the following in the terminal:
```
sudo cat /var/log/system.log | grep -i brcm[fp]
```

This will show a log excerpt of BrcmPatchRAM output during startup.
