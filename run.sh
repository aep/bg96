#!/bin/sh
set -ex

THIS=$(dirname $0)
cd $THIS

make
./main
