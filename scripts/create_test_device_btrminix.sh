#!/bin/sh
rm /tmp/testdevice
rm -rf /tmp/testmount

dd if=/dev/zero of=/tmp/testdevice bs=1M count=1500
sudo ../util/mkfs.minix -s 3 /tmp/testdevice
mkdir /tmp/testmount
