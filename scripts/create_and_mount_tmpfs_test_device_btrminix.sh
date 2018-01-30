#!/bin/sh

sudo umount /tmp/testmount

sudo losetup -d /dev/loop0

sudo umount /tmp/tmpfs

sudo rm -rf /tmp/tmpfs
sudo rm -rf /tmp/testmount

# finished tidying up

mkdir /tmp/tmpfs
sudo mount -t tmpfs none /tmp/tmpfs

dd if=/dev/zero of=/tmp/tmpfs/testdevice bs=1M seek=699 count=1

sudo losetup /dev/loop0 /tmp/tmpfs/testdevice

sudo ../util/mkfs.minix -s 3 /dev/loop0
#sudo mkfs.minix -3 /dev/loop0
#sudo mkfs.btrfs /dev/loop0

mkdir /tmp/testmount
sudo mount -t btrminix -o loop /tmp/tmpfs/testdevice /tmp/testmount
sudo chown 1000:1000 /tmp/testmount