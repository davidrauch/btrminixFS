#include <iostream>
#include <cstring>
#include <string>
#include <set>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <experimental/filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <mntent.h>

#include "snapshots.h"
#include "errors.h"
#include "utils.h"

#include "../minix/ioctl_basic.h"

namespace stdfs = std::experimental::filesystem;

void validate_args(int argc, char * argv[]) {
    std::set<std::string> commands {"create", "remove", "rollback", "list"};

    if (argc <= 3) {
        params_invalid();
    }

    std::string command(argv[2]);
    
    if (command.compare("create") == 0) {
        if (argc != 5 ||
            strlen(argv[3]) == 0 ||
            strlen(argv[4]) == 0 ||
            strlen(argv[4]) >= 32) {
            params_invalid();
        }
    } else if (command.compare("remove") == 0) {
        if (argc != 5 ||
            strlen(argv[3]) == 0 ||
            strlen(argv[4]) == 0) {
            params_invalid();
        }
    } else if (command.compare("rollback") == 0) {
        if (argc != 5 ||
            strlen(argv[3]) == 0 ||
            strlen(argv[4]) == 0) {
            params_invalid();
        }
    } else if (command.compare("list") == 0) {
        if (argc != 4 ||
            strlen(argv[3]) == 0) {
            params_invalid();
        }
    }
}

std::string get_device_path(std::string mount_path) {
    struct mntent *ent;
    FILE *mnt_file = setmntent("/proc/mounts", "r");
    if (mnt_file == NULL) {
        perror("setmntent");
        exit(1);
    }

    while (NULL != (ent = getmntent(mnt_file))) {
        if(mount_path.compare(ent->mnt_dir) == 0) {
            return std::string(ent->mnt_fsname);
        }
    }
    endmntent(mnt_file);

    return std::string();
}

void remount(std::string volume_path, std::string device_path) {
    // TODO: Use syscalls
    std::ostringstream umount_cmd;
    umount_cmd << "umount " << volume_path;
    system(umount_cmd.str().c_str());

    std::ostringstream mount_cmd;
    mount_cmd << "mount -t altminix " << device_path << " " << volume_path;
    system(mount_cmd.str().c_str());
}

int main(int argc, char * argv[]) {
    //std::cout << EBADF << " " << EFAULT << " " << EINVAL << " " << ENOTTY << " " << std::endl;

    // Validate params
    validate_args(argc, argv);

    // At this point we know we have valid params
    std::string volume_path(argv[3]);
    std::string snapshots_dir(join_paths(volume_path, snapshots_dir_name));

    // Check source volume
    if (!stdfs::is_directory(volume_path) || !stdfs::is_directory(snapshots_dir)) {
        source_volume_invalid();
    }

    std::string device_path = get_device_path(volume_path);
    if(device_path.length() ==0) {
        source_volume_invalid();
    }

    // Umount and remond to clean up
    // TODO: Use syscall, or fix driver so that this is not needed
    remount(volume_path, device_path);

    // Test ioctl
    int fd = open("/tmp/testmount/.snapshots/.interface", O_RDWR);

    if (fd == -1) {
        printf("Error in opening file \n");
        exit(-1);
    }

    // At this point we can perform the action
    std::string command(argv[2]);
    if (command.compare("create") == 0) {
        char name[32];
        memcpy(name, argv[4], 32);
        int ioctl_ret = ioctl(fd, IOCTL_ALTMINIX_CREATE_SNAPSHOT, name);
        //create_snapshot(volume_path, std::string(argv[4]));
    } else if (command.compare("remove") == 0) {
        char name[32];
        memcpy(name, argv[4], 32);
        int ioctl_ret = ioctl(fd, IOCTL_ALTMINIX_REMOVE_SNAPSHOT, name);
    } else if (command.compare("rollback") == 0) {
        char name[32];
        memcpy(name, argv[4], 32);
        int ioctl_ret = ioctl(fd, IOCTL_ALTMINIX_ROLLBACK_SNAPSHOT, name);
    } else if (command.compare("list") == 0) {
        //list_snapshots(volume_path);
    }

    close(fd);

    remount(volume_path, device_path);
}