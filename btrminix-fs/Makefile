#
# Makefile for the Linux minix filesystem routines.
#

obj-m := btrminix.o

btrminix-objs := bitmap.o itree_v1.o itree_v2.o namei.o inode.o file.o dir.o snapshot.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
