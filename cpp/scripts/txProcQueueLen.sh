#!/bin/bash

cd /var/log/txProc/stats/
for file in *
do
  filename="$file/${file}_`date '+%Y%m%d'`.log"
  if [ -e $filename ]
  then
    echo `tail -n1 $filename`" 	|$file"
  fi
done

