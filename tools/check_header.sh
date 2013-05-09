#!/bin/sh

HEADER="$1"
CC="${2:-gcc}"

echo "#include <$HEADER>" | "${CC}" -E - &>/dev/null
if [ $? -eq 0 ]; then 
    echo 1
else 
    echo 0
fi

