#!/bin/bash

# Sizes: 1kb, 10kb, 1mb, 10mb
array=( 1 10 1024 10240 )

for i in "${array[@]}"
do
	# Create file
  	dd if=/dev/urandom of=/tmp/testmount/test.dat bs=1k count=$i

	# Create report with iterations
	(perf stat -B -r 1000 cp --reflink /tmp/testmount/test.dat /tmp/testmount/test_copy.dat > /dev/null) 2> results/btrminix_clone_$i.out
done

# Alternatively: Create perf.data
# sudo perf record -o cp/minix-copy-1000_perf.data -c 1000 cp --reflink /tmp/testmount/test.dat /tmp/testmount/test_copy.dat
# and then perf report