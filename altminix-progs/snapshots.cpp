#include <iostream>

#include "snapshots.h"
#include "errors.h"
#include "utils.h"

namespace stdfs = std::experimental::filesystem;

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

    // Make sure snapshot name does not already exist
    if (stdfs::exists(snapshot_dir)) {
        snapshot_name_invalid();
    }

    // Create folder for new snapshot
    stdfs::create_directory(snapshot_dir);

    // Go over the content of the root folder
    // copy everything except the .snapshots folder
    for (auto filename : get_filenames(volume_path)) {
        if (stdfs::is_directory(filename)) {
            filename = filename + "/";
        }

        if (filename.compare(snapshots_dir) == 0) {
            continue;
        }

        auto filepath = stdfs::path(filename);
        auto target_filename = snapshot_dir + filepath.filename().string();

        std::ostringstream cmd;
        cmd << "cp --reflink=always --preserve=all -r \"" << filepath << "\" \"" << target_filename << "\"";
 
        system(cmd.str().c_str());
    }

    std::cout << "Sucessfully created snapshot \"" << snapshot_name << "\"" << std::endl;
}

void remove_snapshot(std::string volume_path, std::string snapshot_name) {
    std::string snapshots_dir(join_paths(volume_path, snapshots_dir_name));
    std::string snapshot_dir(join_paths(snapshots_dir, snapshot_name) + "/");

    // Make sure snapshot does exist
    if (!stdfs::exists(snapshot_dir)) {
        snapshot_name_invalid();
    }

    if (stdfs::is_directory(snapshot_dir)) {
        stdfs::remove_all(snapshot_dir);
    }

    std::cout << "Sucessfully removed snapshot \"" << snapshot_name << "\"" << std::endl;
}

void rollback_snapshot(std::string volume_path, std::string snapshot_name) {
    std::string snapshots_dir(join_paths(volume_path, snapshots_dir_name));
    std::string snapshot_dir(join_paths(snapshots_dir, snapshot_name) + "/");

    // Make sure snapshot does exist
    if (!stdfs::exists(snapshot_dir)) {
        snapshot_name_invalid();
    }

    // Remove all current files
    for (auto filename : get_filenames(volume_path)) {
        if (stdfs::is_directory(filename)) {
            filename = filename + "/";
        }

        if (filename.compare(snapshots_dir) == 0) {
            continue;
        }

        stdfs::remove_all(filename);
    }

    // Restore from snapshot
    for (auto filename : get_filenames(snapshot_dir)) {
        if (stdfs::is_directory(filename)) {
            filename = filename + "/";
        }

        auto filepath = stdfs::path(filename);
        auto target_filename = volume_path + filepath.filename().string();

        std::ostringstream cmd;
        cmd << "cp --reflink=always --preserve=all -r \"" << filepath << "\" \"" << target_filename << "\"";
 
        system(cmd.str().c_str());
    }

    std::cout << "Sucessfully rolled back to snapshot \"" << snapshot_name << "\"" << std::endl;
}

void list_snapshots(std::string volume_path) {
    std::string snapshots_dir(join_paths(volume_path, snapshots_dir_name));

    // List snapshots
    // Remove all current files
    for (auto filename : get_filenames(snapshots_dir)) {
        auto filepath = stdfs::path(filename);
        std::cout << filepath.filename().string() << std::endl;
    }

}