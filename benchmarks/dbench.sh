#!/bin/bash

dbench -D /tmp/testmount/ -t 60 1 > benchmarks/btrminixfs-60s-1user.out