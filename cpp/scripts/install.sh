#!/bin/sh
#
# $Id: install.sh 2896 2013-07-05 19:02:04Z gerhardus $
#
# Gerhardus Muller
# Install script for txProc - should run as root
#
# update-rc.d on debian - see also http://wiki.debian.org/LSBInitScripts
# chkconfig -a on suse

logDir=/var/log/txProc
execDir=/usr/local/bin
etcDir=/usr/local/etc
runAsUser=uucp
groupOwner=uucp
#confFile=
#runAsUser=gerhardus
#groupOwner=users
#confFile=gerhardusMac.cfg

echo "creating log directory $logDir"
install -g $groupOwner -o $runAsUser -m 'u=rwx,g=rwx' -d $logDir
install -g $groupOwner -o $runAsUser -m 'u=rwx,g=rwx' -d $logDir/recovery
install -g $groupOwner -o $runAsUser -m 'u=rwx,g=rwx' -d $logDir/stats
install -g $groupOwner -o $runAsUser -m 'u=rwx,g=rwx' -d $logDir/oldlogs

echo "creating etc directory $etcDir"
install -o root -m 'u=rwx,g=rwx' -d $etcDir

echo "installing the binary $execDir/txProc"
install -g $groupOwner -s -o $runAsUser ../bin/txProc $execDir/

if [ ! -e $etcDir/txProc.cfg ]; then
  echo "installing the config template $etcDir/txProc.cfg"
  #install -g $groupOwner -o $runAsUser ../bin/txProc.cfg $etcDir/
  ln -s `pwd`/../conf/$confFile $etcDir/txProc.cfg
else
  echo "Config file $etcDir/txProc.cfg already exists"
fi
echo "Config file $etcDir/txProc.cfg points to:" `ls -l $etcDir/txProc.cfg`

echo "installing the init.d script"
install -g root -o root ../scripts/txProc.init_d /etc/init.d/txProc
if [ ! -e /etc/logrotate.d/txProc ]; then
  echo "installing the logrotate config file"
  install -g root -o root ../scripts/txProc.logrotate_d /etc/logrotate.d/txProc
else
  echo "existing logrotate config file - skipping"
fi

if [ ! -e $logDir/txProc.log ]; then
  echo "installing the log files $logDir"
  install -g $groupOwner -o $runAsUser -m 'u=rw,g=rw' ../bin/log.template $logDir/txProc.log
  install -g $groupOwner -o $runAsUser -m 'u=rw,g=rw' ../bin/log.template $logDir/nucleus.log
  install -g $groupOwner -o $runAsUser -m 'u=rw,g=rw' ../bin/log.template $logDir/socket.log
else
  echo "log files $logDir already exist - skipping"
fi

if [ ! -e $execDir/txProcLogGrep.pl ]; then
  echo "installing the log grep script $execDir/txProcLogGrep.pl"
  #install -g $groupOwner -o $runAsUser txProcLogGrep.pl $execDir/
  ln -s `pwd`/../scripts/txProcLogGrep.pl $execDir/
  echo "installing the recovery script $execDir/txProcRecover.pl"
  #install -g $groupOwner -o $runAsUser txProcRecover.pl $execDir/
  ln -s `pwd`/../scripts/txProcRecover.pl $execDir/
  echo "installing the admin script $execDir/txProcAdmin.pl"
  #install -g $groupOwner -o $runAsUser txProcAdmin.pl $execDir/
  ln -s `pwd`/../scripts/txProcAdmin.pl $execDir/
  echo "installing the queueLen script $execDir/txProcQueueLen.sh"
  #install -g $groupOwner -o $runAsUser txProcQueueLen.sh $execDir/
  ln -s `pwd`/../scripts/txProcQueueLen.sh $execDir/
else
  echo "links for scripts in $execDir already exist - skipping"
fi

#echo "creating init.d links"
#update-rc.d txProc defaults 95

