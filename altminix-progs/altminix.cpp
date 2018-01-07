#include "altminix.h"

#include <iostream>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <experimental/filesystem>

namespace stdfs = std::experimental::filesystem;

static std::string snapshots_dir_name(".snapshots/");

void print_usage() {
    std::cout << "Usage: altminix snapshot volume_path snapshot_name" << std::endl;
}

void source_volume_invalid() {
    std::cout << "The source volume is invalid" << std::endl;
    exit(EXIT_FAILURE);
}

void snapshot_name_invalid() {
    std::cout << "The snapshot name is invalid" << std::endl;
    exit(EXIT_FAILURE);
}

bool string_ends_with(const std::string &a, const std::string &b) {
    if (b.size() > a.size()) {
        return false;
    }
    return std::equal(a.begin() + a.size() - b.size(), a.end(), b.begin());
}

std::string join_paths(const std::string &path1, const std::string &path2) {
    // TODO: Make this work properly
    return path1 + path2;
}

void validate_args(int argc, char * argv[]) {
    if (argc != 4 ||
        strcmp(argv[1], "snapshot") != 0 ||
        strlen(argv[2]) == 0 ||
        strlen(argv[3]) == 0) {
        print_usage();
        exit(EXIT_FAILURE);;
    }
}

std::vector<std::string> get_filenames(std::experimental::filesystem::path path)
{
    std::vector<std::string> filenames;
    
    const stdfs::directory_iterator end{};
    
    for(stdfs::directory_iterator iter{path}; iter != end; ++iter)
    {
        filenames.push_back(iter->path().string());
    }

    return filenames;
}

void create_snapshot(std::string volume_path, std::string snapshot_name) {
    std::string snapshots_dir(join_paths(volume_path, snapshots_dir_name));
    std::string snapshot_dir(join_paths(snapshots_dir, snapshot_name) + "/");

    // Create folder for new snapshot
    stdfs::create_directory(snapshot_dir);

    // Go over the content of the root folder
    // copy everything except the .snapshots folder
    auto filenames = get_filenames(volume_path);
    for (auto filename : filenames) {
        if (stdfs::is_directory(filename)) {
            filename = filename + "/";
        }

        if (filename.compare(snapshots_dir) == 0) {
            continue;
        }

        auto filepath = stdfs::path(filename);
        auto target_filename = snapshot_dir + filepath.filename().string();

        std::ostringstream cmd;
        cmd << "cp --reflink -r \"" << filepath << "\" \"" << target_filename << "\"";
 
        system(cmd.str().c_str());
    }
}

int main(int argc, char * argv[]) {
    // Validate params
    validate_args(argc, argv);

    // At this point we know we have valid params
    std::string volume_path(argv[2]);
    std::string snapshot_name(argv[3]);
    std::string snapshots_dir(join_paths(volume_path, snapshots_dir_name));
    std::string snapshot_dir(join_paths(snapshots_dir, snapshot_name));

    // Check source volume
    // Check that it exists
    struct stat stat_info;
    //std::cout << "Checking " << volume_path << std::endl;
    if (stat(volume_path.c_str(), &stat_info) != 0) {
        source_volume_invalid();
    } else if (!(stat_info.st_mode & S_IFDIR)) {
        source_volume_invalid();
    }

    // Check that it has a .snapshots folder
    //std::cout << "Checking " << snapshots_dir << std::endl;
    if (stat(snapshots_dir.c_str(), &stat_info) != 0) {
        source_volume_invalid();
    } else if (!(stat_info.st_mode & S_IFDIR)) {
        source_volume_invalid();
    }

    // Make sure snapshot name does not already exist
    //std::cout << "Checking " << snapshot_dir << std::endl;
    if (stat(snapshot_dir.c_str(), &stat_info) == 0) {
        snapshot_name_invalid();
    }

    // At this point we can create the snapshot
    create_snapshot(volume_path, snapshot_name);
}