#include <linux/ioctl.h>

struct snapshot_slot {
	char* name;
	int* slot;
};

#define IOC_MAGIC 'k'
#define IOCTL_BTRMINIX_CREATE_SNAPSHOT 		_IOR(IOC_MAGIC, 0, char*)
#define IOCTL_BTRMINIX_ROLLBACK_SNAPSHOT 	_IOR(IOC_MAGIC, 1, char*)
#define IOCTL_BTRMINIX_REMOVE_SNAPSHOT 		_IOR(IOC_MAGIC, 2, char*)
#define IOCTL_BTRMINIX_SLOT_OF_SNAPSHOT 	_IOWR(IOC_MAGIC, 3, struct snapshot_slot*)
#define IOCTL_BTRMINIX_LIST_SNAPSHOTS 		_IOWR(IOC_MAGIC, 4, char*)
#define IOCTL_BTRMINIX_SNAPSHOT_SLOTS 		_IOW(IOC_MAGIC, 5, int*)

#define IOCTL_ERROR_SNAPSHOT_EXISTS			-1
#define IOCTL_ERROR_SNAPSHOT_DOES_NOT_EXIST	-2
#define IOCTL_ERROR_NO_FREE_SNAPSHOTS		-3