BrcmPatchRAM Changelog
======================

#### v2.5.4
- Added inject BCM2070 - BCM943224HMB, BCM943225HMB Combo

#### v2.5.3
- Fix parsing firmware versions (e.g. 8785 is 4689)
- Use 4689 firmware for DW1820A [0a5c:6412]
- Log uncompressed firmware SHA-1 in DEBUG builds
- Reworked device reset to improve compatibility (thx @mishurov)

#### v2.5.2
- Revert DW1820A from 8785 to 8784 [0a5c:6412]
- Add older firmwares from 12.0.1.1012
- Add `bpr_handshake` boot argument to override handshake support mode
- Change `bpr_preresetdelay=0` behaviour to no longer imply `bpr_handshake=1`

#### v2.5.1
- Add Lenovo 00JT494 [0a5c:6414]

#### v2.5.0
- Initial import based on @headkaze [2.2.12](https://github.com/headkaze/OS-X-BrcmPatchRAM/releases)
- Merged BrcmPatchRAM3 improvements authored by @Mieze
