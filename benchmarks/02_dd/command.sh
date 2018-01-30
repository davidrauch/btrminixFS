#!/bin/sh

rm /tmp/testmount/testfile
touch /tmp/testmount/testfile

# dd if=/dev/urandom of=/tmp/testmount/testfile bs=1M count=256 # btrminix-cloned only

for i in `seq 10`
do
	# cp --reflink /tmp/testmount/testfile /tmp/testmount/testfile2 # btrminix-cloned only
	dd if=/dev/zero of=/tmp/testmount/testfile bs=1M count=256 2>> results/btrminix-tmpfs-zero-256M-10.out
	# rm /tmp/testmount/testfile2 # btrminix-cloned only
done