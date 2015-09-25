#set -x

# extract minor version (eg. 10.9 vs. 10.10 vs. 10.11)
MINOR_VER=$([[ "$(sw_vers -productVersion)" =~ [0-9]+\.([0-9]+) ]] && echo ${BASH_REMATCH[1]})
if [[ $MINOR_VER -ge 11 ]]; then
    echo 10.11+
else
    echo 10.10-
fi
