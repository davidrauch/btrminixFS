#include "utils.h"

bool string_ends_with(const std::string &a, const std::string &b) {
    if (b.size() > a.size()) {
        return false;
    }
    return std::equal(a.begin() + a.size() - b.size(), a.end(), b.begin());
}

bool string_begins_with(const std::string &a, const std::string &b) {
    if (b.size() > a.size()) {
        return false;
    }
    return std::equal(a.begin(), a.begin() + b.size(), b.begin());
}

std::string join_paths(std::string path1, std::string path2) {
    if(string_ends_with(path1, "/")) {
    	path1 = path1.substr(0, path1.length() - 1);
    }
    if(string_begins_with(path2, "/")) {
    	path2 = path2.substr(1, path2.length() - 1);
    }
    return path1 + std::string("/") + path2;
}