#!/bin/sh

cd $1
autoreconf -i
./configure --with-openssl=/home/a
make asf-dist
cd rpm
rpm_create $2.spec -v $3 -r $4 -p /usr

for mypk in `ls *.rpm`
do
	t_pk_l=${mypk//-/ }
	t_pk_array=($t_pk_l)
	ac=${#t_pk_array[*]}
	buildnumber=${t_pk_array[$ac-1]}
        sf=`echo $buildnumber|awk -F'.' '{print "."$(NF-2)"."$(NF-1)"."$NF}'`
        buildnumber=${buildnumber/$sf/}
        echo $buildnumber >  $1/rpm/BUILDNO.txt
	break
done
