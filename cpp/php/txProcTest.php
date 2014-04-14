<?php
/**
 *
 * **/
require_once "utils/trace.inc.php";
require_once "txProc.inc.php";

$event = new txProc();
$socket = NULL;                // leave undefined for no return
$serverName = 'outbound';
$serverService = 'txproc';

//  $event->eventType( 'EV_COMMAND' );
//  $event->command( 'CMD_STATS' );

//  $event->eventType( 'EV_COMMAND' );
//  $event->command( 'CMD_NUCLEUS_CONF' );
//  $event->addParam( 'queue', 'default' );
//  $event->addParam( 'cmd', 'updateworkers' );
//  $event->addParam( 'val', '4' );

$event->eventType( 'EV_URL' );
$event->destQueue( 'default' );
$event->url( 'http://utilities/test/test.php' );
$event->addParam( 'param1', '4' );
$event->addParam( 'param2', 'param2' );
$event->addParam( 'param3', '3' );

// to receive a reply or the output from the execution
$event->returnFd( 0 );
list($socket,$err) = $event->createSocket( NULL,$serverName,$serverService );
if($socket === false) {trace($err);return;};

// submit
list($retVal,$errString) = $event->serialise( $socket,NULL,$serverName,$serverService );
//list($retVal,$errString) = $event->serialise( NULL,NULL,$serverName,$serverService );
if( !isset($errString) ) $errString = '';
trace( "serialise returned:$retVal err:'$errString'" );

// reply the reply if required
if( $retVal == 2 )
{
  trace( "waiting for return packet" );
  list($reply,$err) = txProc::unSerialise($socket);
  trace( "reply: err:'$err' :".$reply->toString() );
} // if

if( isset($socket) ) socket_close($socket);
?>
