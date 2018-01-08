#!/bin/sh
./unmount_test_device.sh
./create_test_device.sh
./reload_and_remount.sh

mkdir /tmp/testmount/.snapshots
touch /tmp/testmount/.snapshots/.interface