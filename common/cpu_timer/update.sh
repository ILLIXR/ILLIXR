#!/bin/sh

rm -rf /tmp/cpu_timer
git clone git@github.com:charmoniumQ/cpu_timer.git /tmp/cpu_timer
rm -rf *.hpp test
cp -r /tmp/cpu_timer/include/*.hpp .
