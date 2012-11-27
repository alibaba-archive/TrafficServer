#!/bin/sh
cd $1/rpm
if [ `uname -i` == "x86_64" ]
then
        plat="x86_64"
else
        plat="i386"
fi
for name in `ls *.rpm`
do 
  yum-upload $name  --osver $ABS_OS --arch ${plat} --group yum --batch
done
