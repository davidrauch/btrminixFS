#!/bin/sh
rm /tmp/testdevice
rm -rf /tmp/testmount

dd if=/dev/zero of=/tmp/testdevice bs=1M count=100
sudo ../util/mkfs.minix -s 10 /tmp/testdevice
mkdir /tmp/testmount
