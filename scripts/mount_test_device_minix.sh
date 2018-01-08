#!/bin/sh
sudo losetup -d /dev/loop0
sudo losetup /dev/loop0 /tmp/testdevice
sudo mount -t minix /dev/loop0 /tmp/testmount
sudo chown 1000:1000 /tmp/testmount