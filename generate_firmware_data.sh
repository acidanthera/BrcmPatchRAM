#!/bin/bash

out="GeneratedFirmwares.cpp"
cksum="GeneratedFirmwaresMD5.txt"
cksum_temp="/tmp/org_rehabman_GeneratedFirmwareMD5.txt"

firmwaredir=./firmwares
firmwares=$firmwaredir/*.zhx

if [[ "$1" == "clean" ]]; then
    if [ -e $out ]; then rm $out; fi
    if [ -e $cksum ]; then rm $cksum; fi
    exit 0
fi

if [ -e $cksum_temp ]; then rm $cksum_temp; fi
for firmware in $firmwares; do
    echo "`basename $firmware` `md5 -q $firmware`" >>$cksum_temp
done

cksum_old="unknown"
if [[ -e $cksum ]]; then
    cksum_old=`md5 -q $cksum`
fi

cksum_new=`md5 -q $cksum_temp`
if [[ $cksum_new == $cksum_old ]]; then
    echo "Firmwares unchanged, no need to update GeneratedFirmwares.cpp"
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

cp $cksum_temp $cksum
