#!/bin/sh
/usr/bin/find /var/log/txProc/stats/ -depth -type f -mtime +180 -delete
