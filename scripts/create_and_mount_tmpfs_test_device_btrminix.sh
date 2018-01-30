#!/bin/sh
sudo umount /tmp/testmount

rm -rf /tmp/tmpfs 2>/dev/null
rm -rf /tmp/testmount 2>/dev/null

mkdir /tmp/tmpfs
sudo mount -t tmpfs none /tmp/tmpfs

dd if=/dev/zero of=/tmp/tmpfs/testdevice bs=1M count=256

mkdir /tmp/testmount

sudo losetup -d /dev/loop0
sudo losetup /dev/loop0 /tmp/tmpfs/testdevice

sudo ../util/mkfs.minix -s 3 /dev/loop0

sudo mount -t btrminix /dev/loop0 /tmp/testmount
sudo chown 1000:1000 /tmp/testmount