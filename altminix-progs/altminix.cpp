#include <iostream>
#include <cstring>
#include <string>
#include <set>
#include <sys/types.h>
#include <sys/stat.h>
#include <experimental/filesystem>

#include "snapshots.h"
#include "errors.h"
#include "utils.h"

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
            strlen(argv[4]) == 0) {
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

int main(int argc, char * argv[]) {
    // Validate params
    validate_args(argc, argv);

    // At this point we know we have valid params
    std::string volume_path(argv[3]);
    std::string snapshots_dir(join_paths(volume_path, snapshots_dir_name));

    // Check source volume
    if (!stdfs::is_directory(volume_path) || !stdfs::is_directory(snapshots_dir)) {
        source_volume_invalid();
    }

    // At this point we can perform the action
    std::string command(argv[2]);
    if (command.compare("create") == 0) {
        create_snapshot(volume_path, std::string(argv[4]));
    } else if (command.compare("remove") == 0) {
        remove_snapshot(volume_path, std::string(argv[4]));
    } else if (command.compare("rollback") == 0) {
        rollback_snapshot(volume_path, std::string(argv[4]));
    } else if (command.compare("list") == 0) {
        list_snapshots(volume_path);
    }
}