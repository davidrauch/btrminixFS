#include <stdlib.h>
#include <iostream>

#include "errors.h"
#include "../minix/ioctl_basic.h"

void params_invalid() {
    std::cout << "Usage: btrminix snapshot (create|remove|rollback|list) volume_path [snapshot_name]" << std::endl;
    exit(EXIT_FAILURE);
}

void source_volume_invalid() {
    std::cout << "Error: The source volume is invalid" << std::endl;
    exit(EXIT_FAILURE);
}

void snapshot_name_invalid() {
    std::cout << "Error: The snapshot name is invalid" << std::endl;
    exit(EXIT_FAILURE);
}

void ioctl_error(long code) {
	//std::cout << "Code is " << code << std::endl;
	switch(code) {
	case -IOCTL_ERROR_SNAPSHOT_EXISTS:
		std::cout << "Error: This snapshot name already exists" << std::endl;
		break;
	case -IOCTL_ERROR_SNAPSHOT_DOES_NOT_EXIST:
		std::cout << "Error: The specified snapshot does not exist" << std::endl;
		break;
	case -IOCTL_ERROR_NO_FREE_SNAPSHOTS:
		std::cout << "Error: There are no free snapshot slots" << std::endl;
		break;
	}
}