#!/bin/sh

NOTIFY_FILENAME='/var/run/trafficserver/trafficserver.notify'
run_prefix='/etc/init.d'
bin_prefix='/usr/bin'

echo "traffic recovery start ..."
$run_prefix/trafficserver deactivate

while [ 1 -eq 1 ]; do
  sleep 1
  if [ ! -f $NOTIFY_FILENAME ]; then
    break
  fi

  content=$(/bin/cat $NOTIFY_FILENAME)
  if [ "x$content" = 'x1' ]; then
    break
  fi
done

$run_prefix/trafficserver activate

echo "traffic recovery done"

