#!/bin/bash

# Measure time to clone file
#./create_testfile.py
#for i in `seq 100`; do 
#	{ time cp --reflink /tmp/testmount/test.dat /tmp/testmount/test_copy.dat; } &>>cp/btrminix-clone-100.out; 
#done


# Measure time to copy file
#./create_testfile.py
for i in `seq 100`; do 
	{ time cp /tmp/testmount/test.dat /tmp/testmount/test_copy.dat; } &>>cp/btrminix-copy-100.out; 
done