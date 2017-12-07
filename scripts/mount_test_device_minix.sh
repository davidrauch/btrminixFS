#!/bin/sh
sudo mount -t minix -o loop /tmp/testdevice /tmp/testmount
sudo chown 1000:1000 /tmp/testmount