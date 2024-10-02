BrcmPatchRAM Changelog
======================
#### v2.6.9
- Added constants for macOS 15 support

#### v2.6.8
- Added vendor callback patch for Bluetooth power status toggling on macOS 13.3+ (thx @zxystd)
- Added patch that skips Internal Bluetooth Controller NVRAM checking (thx @zxystd)

#### v2.6.7
- Added constants for macOS 14 support

#### v2.6.6
- Added firmware for legacy BCM20702A1 (thx @chapuza)

#### v2.6.5
- Fixed legacy Mac compatibility (thx @AsdMonio)

#### v2.6.4
- Improve compatibility with BCM43142A0 on macOS Big Sur (thx lalithkota)

#### v2.6.3
- Added constants for macOS 13 support
- Fixed Skip Address Check patch for 13.0 Beta 1 and newer

#### v2.6.2
- Added Skip Address Check patch for 12.4 Beta 3 and newer (thx @khronokernel)

#### v2.6.1
- Improved BlueToolFixup compatibility with macOS 12b10 (thx @dhinakg, @williambj1)
- Fixed bluetooth support on MBP15,4 and other similar boards (thx @dhinakg, @usr-sse2)
- Fixed bluetooth not working on macOS 12 after the first power cycle (thx @williambj1, @zxystd)

#### v2.6.0
- Added BlueToolFixup for macOS 12 compatibility

#### v2.5.9
- Added BCM94352Z identifiers for injection

#### v2.5.8
- Added BCM94360Z3 identifiers for injection

#### v2.5.7
- Added BCM94360Z4 identifiers for injection

#### v2.5.6
- Added inject Laird BT851 Bluetooth 5.0 USB dongle
- Added legacy Bluetooth injection kext

#### v2.5.5
- Initial MacKernelSDK and Xcode 12 compatibility
- Added bundled BrcmPatchRAM.kext to the binaries

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
