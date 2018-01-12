#include <iostream>
#include <sys/ioctl.h>
#include <string>
#include <sstream>
#include <cstring>
#include <chrono>

#include "snapshots.h"
#include "errors.h"
#include "utils.h"
#include "../btrminix-fs/ioctl_basic.h"

#define SNAPSHOT_NAME_LENGTH    32

void create_snapshot(int ioctl_fd, const char *snapshot_name) {
    // Copy name to fixed length
    char name[SNAPSHOT_NAME_LENGTH];
    memcpy(name, snapshot_name, SNAPSHOT_NAME_LENGTH);
    
    // Call IOCTL
    int ioctl_ret = ioctl(ioctl_fd, IOCTL_BTRMINIX_CREATE_SNAPSHOT, name);

    if(ioctl_ret == 0) {
        int slot = slot_of_snapshot(ioctl_fd, (char*)snapshot_name);
        std::cout << "Sucessfully created snapshot \"" << snapshot_name << "\"" << " in slot " << slot << std::endl;
    } else {
        ioctl_error(errno);
    }
}

void remove_snapshot(int ioctl_fd, const char *snapshot_name) {
    // Copy name to fixed length
    char name[SNAPSHOT_NAME_LENGTH];
    memcpy(name, snapshot_name, SNAPSHOT_NAME_LENGTH);
    
    // Call IOCTL
    int ioctl_ret = ioctl(ioctl_fd, IOCTL_BTRMINIX_REMOVE_SNAPSHOT, name);

    if(ioctl_ret == 0) {
        std::cout << "Sucessfully removed snapshot \"" << snapshot_name << "\"" << std::endl;
    } else {
        ioctl_error(errno);
    }
}

void rollback_snapshot(int ioctl_fd, const char *snapshot_name) {
    // Get number of slots
    int slot_info[2] = {0, 0}; // [overall, taken]
    int ioctl_ret = ioctl(ioctl_fd, IOCTL_BTRMINIX_SNAPSHOT_SLOTS, slot_info);
    if(ioctl_ret != 0) {
        ioctl_error(errno);
    }

    if(slot_info[0] == slot_info[1]) {
        std::cout << "There is no free slot to create an automatic snapshot before the rollback." << std::endl;
        std::cout << "Do you still want to perform the rollback? (y/n): ";
        char response;
        std::cin >> response;
        if(response != 'y') {
            exit(EXIT_FAILURE);
        }
    } else {
        // Create automatic snapshot
        std::ostringstream auto_name;
        auto_name << "auto_" << currentDateTime();

        create_snapshot(ioctl_fd, auto_name.str().c_str());
    }  

    // Copy name to fixed length
    char name[SNAPSHOT_NAME_LENGTH];
    memcpy(name, snapshot_name, SNAPSHOT_NAME_LENGTH);
    
    // Call IOCTL
    ioctl_ret = ioctl(ioctl_fd, IOCTL_BTRMINIX_ROLLBACK_SNAPSHOT, name);

    if(ioctl_ret == 0) {
        std::cout << "Sucessfully rolled back to snapshot \"" << snapshot_name << "\"" << std::endl;
    } else {
        ioctl_error(errno);
    }
}

int slot_of_snapshot(int ioctl_fd, char *snapshot_name) {
    // Copy name to fixed length
    char name[SNAPSHOT_NAME_LENGTH];
    memcpy(name, snapshot_name, SNAPSHOT_NAME_LENGTH);
    
    // Call IOCTL
    int slot = -1;
    struct snapshot_slot data;
    data.name = snapshot_name;
    data.slot = &slot;

    int ioctl_ret = ioctl(ioctl_fd, IOCTL_BTRMINIX_SLOT_OF_SNAPSHOT, &data);

    if(ioctl_ret != 0) {
        ioctl_error(errno);
    }

    return slot;
}

void list_snapshots(int ioctl_fd) {
    // Get number of slots
    int slot_info[2] = {0, 0}; // [overall, taken]
    int ioctl_ret = ioctl(ioctl_fd, IOCTL_BTRMINIX_SNAPSHOT_SLOTS, &slot_info);
    if(ioctl_ret != 0) {
        ioctl_error(errno);
    }

    // Get names
    char names[SNAPSHOT_NAME_LENGTH * slot_info[0]];
    ioctl_ret = ioctl(ioctl_fd, IOCTL_BTRMINIX_LIST_SNAPSHOTS, &names);
    if(ioctl_ret != 0) {
        ioctl_error(errno);
    }

    for(int i = 0; i < slot_info[0]; i++) {
        std::cout << i << ": " << names+(i*SNAPSHOT_NAME_LENGTH) << std::endl;
    }
}