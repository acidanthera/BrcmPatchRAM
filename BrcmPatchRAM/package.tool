#!/bin/bash

if [ "${TARGET_BUILD_DIR}" = "" ]; then
  echo "This must not be run outside of Xcode"
  exit 1
fi

cd "${TARGET_BUILD_DIR}"

# clean / build
if [ "$1" != "analyze" ]; then
  rm -rf *.zip || exit 1
fi

if [ "$1" != "" ]; then
  echo "Got action $1, skipping!"
  exit 0
fi

dist=()
if [ -d "$DWARF_DSYM_FILE_NAME" ]; then dist+=("$DWARF_DSYM_FILE_NAME"); fi

for kext in *.kext; do
  dist+=("$kext")
done

archive="BrcmPatchRAM-${CURRENT_PROJECT_VERSION}-$(echo $CONFIGURATION | tr /a-z/ /A-Z/).zip"
rm -rf *.zip
zip -qry -FS "${archive}" "${dist[@]}"
