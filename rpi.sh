#!/bin/sh

set -ex


make clean

rsync -a . root@bg96.lan:bg96
ssh -t root@bg96.lan ./bg96/run.sh
