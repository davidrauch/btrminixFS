#include <vector>
#include <string>
#include <experimental/filesystem>

std::vector<std::string> get_filenames(std::experimental::filesystem::path path);
void create_snapshot(std::string volume_path, std::string snapshot_name);
void remove_snapshot(std::string volume_path, std::string snapshot_name);
void rollback_snapshot(std::string volume_path, std::string snapshot_name);
void list_snapshots(std::string volume_path);