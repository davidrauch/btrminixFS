#!/bin/bash

### Measure time to clone file

# Create report with iterations
perf stat -B -r 1000 cp --reflink /tmp/testmount/test.dat /tmp/testmount/test_copy.dat

# Alternatively: Create perf.data
sudo perf record -o cp/minix-copy-1000_perf.data -c 1000 cp --reflink /tmp/testmount/test.dat /tmp/testmount/test_copy.dat
# and then perf report


### Measure time to copy file

# Create report with iterations
perf stat -B -r 1000 cp --reflink /tmp/testmount/test.dat /tmp/testmount/test_copy.dat

# Alternatively: Create perf.data
sudo perf record -o cp/minix-copy-1000_perf.data cp --reflink /tmp/testmount/test.dat /tmp/testmount/test_copy.dat
# and then perf report


# todo: do with different file size