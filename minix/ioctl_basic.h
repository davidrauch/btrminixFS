#include <linux/ioctl.h>

#define IOC_MAGIC 'k'
#define IOCTL_HELLO _IOWR(IOC_MAGIC, 0, int32_t*)
