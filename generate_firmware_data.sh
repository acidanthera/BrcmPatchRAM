#!/bin/bash

out="GeneratedFirmwares.cpp"
firmwaredir=./build/Products/Release/BrcmFirmwareRepo.kext/Contents/Resources
if [[ ! -e $firmwares ]]; then firmwaredir=./build/Products/Debug/BrcmFirmwareRepo.kext/Contents/Resources; fi
firmwares=$firmwaredir/*

if [[ "$1" == "clean" ]]; then
    if [ -e $out ]; then rm $out; fi
    exit 0
fi

echo "// GeneratedFirmwares.cpp">$out
echo "//">>$out
echo "// generated from generate_firmware_data.sh">>$out
echo "//">>$out

for firmware in $firmwares; do
    fname=`basename $firmware`
    cname=${fname//./_}

    echo "static const unsigned char $cname[] = ">>$out
    echo "{">>$out
    xxd -i <$firmware >>$out
    echo "};">>$out
    echo "">>$out
done

echo "static const FirmwareEntry firmwares[] = ">>$out
echo "{">>$out
for firmware in $firmwares; do
    fname=`basename $firmware`
    cname=${fname//./_}
    echo "    { \"${fname}\", ${cname}, sizeof(${cname}), },">>$out
done
echo "    { NULL, NULL, 0, },">>$out
echo "};">>$out
