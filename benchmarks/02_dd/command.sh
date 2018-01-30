#!/bin/sh

rm /tmp/testmount/testfile
touch /tmp/testmount/testfile
dd if=/dev/zero of=/tmp/testmount/testfile bs=1M count=256 2> results/minix-256M.out
