#!/bin/bash

SRC_DIR=$(dirname "$0")
pushd "$SRC_DIR" &>/dev/null
SRC_DIR="$(pwd)"
popd &>/dev/null

abort() {
  echo "ERROR: $1"
  exit 1
}

if [ "$#" -le 1 ]; then
  echo -n "Drag and drop Broadcom firmware directory and press [ENTER]: "
  read FWDIR
else
  FWDIR="$1"
  shift
fi

if [ ! -f "${FWDIR}/bcbtums.inf" ]; then
  abort "Broadcom firmware directory should contain bcbtums.inf file, but it is missing!"
fi

rm -rf "${SRC_DIR}/firmwares"

"${SRC_DIR}/firmware.rb" "${FWDIR}" "${SRC_DIR}/firmwares" || abort "Cannot process Broadcom firmware directory"

/usr/libexec/PlistBuddy -c "Delete :IOKitPersonalities" "${SRC_DIR}/BrcmPatchRAM/BrcmPatchRAM-Info.plist" 2>/dev/null
/usr/libexec/PlistBuddy -c "Merge ${SRC_DIR}/firmwares/firmwares.plist" "${SRC_DIR}/BrcmPatchRAM/BrcmPatchRAM-Info.plist" || abort "Cannot update BrcmPatchRAM.kext"

/usr/libexec/PlistBuddy -c "Delete :IOKitPersonalities" "${SRC_DIR}/BrcmPatchRAM/BrcmPatchRAM2-Info.plist" 2>/dev/null
/usr/libexec/PlistBuddy -c "Merge ${SRC_DIR}/firmwares/firmwares2.plist" "${SRC_DIR}/BrcmPatchRAM/BrcmPatchRAM2-Info.plist" || abort "Cannot update BrcmPatchRAM.kext"

/usr/libexec/PlistBuddy -c "Delete :IOKitPersonalities" "${SRC_DIR}/BrcmPatchRAM/BrcmPatchRAM3-Info.plist" 2>/dev/null
/usr/libexec/PlistBuddy -c "Merge ${SRC_DIR}/firmwares/firmwares3.plist" "${SRC_DIR}/BrcmPatchRAM/BrcmPatchRAM3-Info.plist" || abort "Cannot update BrcmPatchRAM.kext"

echo "All good, please rebuilt the KEXTs!"
