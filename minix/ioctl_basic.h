#include <linux/ioctl.h>

#define IOC_MAGIC 'k'
#define IOCTL_ALTMINIX_CREATE_SNAPSHOT _IOR(IOC_MAGIC, 0, char*)
#define IOCTL_ALTMINIX_ROLLBACK_SNAPSHOT _IOR(IOC_MAGIC, 1, char*)
#define IOCTL_ALTMINIX_REMOVE_SNAPSHOT _IOR(IOC_MAGIC, 2, char*)
