if [ "$(id -u)" != "0" ]; then
    echo "This script requires superuser access..."
fi

if [[ "`sw_vers -productVersion`" == "10.11*" ]]; then
    sudo cp -R ./Build/Products/$1/BrcmBluetoothInjector.kext /System/Library/Extensions/
else
    install ./Build/Products/$1/BrcmPatchRAM.kext /System/Library/Extensions/
fi
