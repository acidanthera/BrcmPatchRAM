##### BrcmPatchRAM on Apple Mac

The instructions in the readme are for a [Hackintosh](http://en.wikipedia.org/wiki/OSx86), a normal PC modified to run Mac OS X.

You should __not__ follow the original instructions on a real Mac as it might inadvertently break things.

BrcmPatchRAM.kext, BrcmPatchRAM2.kext, BrcmFirmwareRepo.kext, and BrcmFirmwareData.kext are all unsigned kernel extensions.

In order to use them, unsigned kernel extensions need to be enabled.
Take the following steps in the Terminal:

 * Retrieve the current system boot arguments:
 
  ```
  sudo nvram boot-args
  ```  
   
 * Append "kext-dev-mode=1" to the boot-args:
 
  If for example boot-args was empty before:
  ```
  sudo nvram boot-args="kext-dev-mode=1"
  ```  
 * Reboot the Mac   

Note: In 10.11 and later, you must disable SIP before attempting to modify NVRAM. Refer to Apple provided documentation for disabling SIP (hint: use csrutil in Recovery).

For OS X older than 10.11, install the required kexts inside /System/Library/Extensions.
```
sudo cp -R ~/Downloads/BrcmPatchRAM.kext /System/Library/Extensions
sudo cp -R ~/Downloads/BrcmFirmwareRepo.kext /System/Library/Extensions
sudo touch /System/Library/Extensions
```

Or for 10.11 and later, to /Library/Extensions:
```
sudo cp -R ~/Downloads/BrcmPatchRAM2.kext /Library/Extensions
sudo cp -R ~/Downloads/BrcmFirmwareRepo.kext /Library/Extensions
sudo touch /System/Library/Extensions
```

Wait about a minute before rebooting the Mac again.

If all works properly the firmware version in the Bluetooth profiler will now show a higher version than v4096 (4096 means its non-upgraded).

Additionally its possible to confirm BrcmPatchRAM did its job by executing the following in the terminal:
```
sudo cat /var/log/system.log | grep -i brcm[fp]
```

This will show a log excerpt of BrcmPatchRAM output during startup.
