#!/bin/bash 

# Debugging script for dwb, needs gdb 

BUILDDIR="/tmp/DWB_DEBUG_$UID"
LOGFILE="$BUILDDIR/gdb.log"
DWBDIR="${BUILDDIR}/dwb"


if [ ! -d "${DWBDIR}" ]; then 
  mkdir -p "${DWBDIR}"
fi
  
cd ${BUILDDIR}
wget https://bitbucket.org/portix/dwb/get/master.tar.gz 
tar xvf master.tar.gz -C "${DWBDIR}" --strip-components=1
 
cd "${DWBDIR}/src/util"
make
cd "${DWBDIR}/src"
make debug
gdb -batch -ex "set logging on ${LOGFILE}" -ex "run" -ex "bt" -ex "quit" dwb_d 
