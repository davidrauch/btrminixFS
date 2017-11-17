#!/bin/sh
rm /tmp/testdevice
rm -rf /tmp/testmount

dd if=/dev/zero of=/tmp/testdevice bs=1M count=1024
sudo ../util/mkfs.minix -3 /tmp/testdevice
mkdir /tmp/testmount
