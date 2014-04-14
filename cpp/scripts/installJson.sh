#!/bin/sh                                               
#                                                       
# $Id: installJson.sh 2629 2012-10-19 16:52:17Z gerhardus $
# Gerhardus Muller

destDir=/usr/local/lib/
srcDir=/home/vts/vts/system/tools/json-cpp
lib=libjson_linux-gcc-4.3_libmt.so

install $srcDir/$lib $destDir

