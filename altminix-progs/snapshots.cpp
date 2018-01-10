#include <iostream>
#include <sys/ioctl.h>
#include <cstring>

#include "snapshots.h"
#include "errors.h"
#include "utils.h"
#include "../minix/ioctl_basic.h"

void create_snapshot(int ioctl_fd, char *snapshot_name) {
    // Copy name to fixed length
    char name[32];
    memcpy(name, snapshot_name, 32);
    
    // Call IOCTL
    int ioctl_ret = ioctl(ioctl_fd, IOCTL_ALTMINIX_CREATE_SNAPSHOT, name);

    if(ioctl_ret == 0) {
        std::cout << "Sucessfully created snapshot \"" << snapshot_name << "\"" << std::endl;
    } else {
        ioctl_error(errno);
    }
}

void remove_snapshot(int ioctl_fd, char *snapshot_name) {
    // Copy name to fixed length
    char name[32];
    memcpy(name, snapshot_name, 32);
    
    // Call IOCTL
    int ioctl_ret = ioctl(ioctl_fd, IOCTL_ALTMINIX_REMOVE_SNAPSHOT, name);

    if(ioctl_ret == 0) {
        std::cout << "Sucessfully removed snapshot \"" << snapshot_name << "\"" << std::endl;
    } else {
        ioctl_error(errno);
    }
}

void rollback_snapshot(int ioctl_fd, char *snapshot_name) {
    // Copy name to fixed length
    char name[32];
    memcpy(name, snapshot_name, 32);
    
    // Call IOCTL
    int ioctl_ret = ioctl(ioctl_fd, IOCTL_ALTMINIX_ROLLBACK_SNAPSHOT, name);

    if(ioctl_ret == 0) {
        std::cout << "Sucessfully rolled back to snapshot \"" << snapshot_name << "\"" << std::endl;
    } else {
        ioctl_error(errno);
    }
}

void list_snapshots(int ioctl_fd, char *snapshot_name) {


}