#pragma once
#include <string>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

class LDPFileOps {
public:
    static bool rename(const std::string& old_path, const std::string& new_path, std::string& err);
    static bool rmdir(const std::string& path, std::string& err);
    static bool chmod(const std::string& path, fs::perms permissions, std::string& err);
    static bool stat(const std::string& path, fs::file_status& status, std::string& err);
    static bool symlink(const std::string& target, const std::string& link, std::string& err);
};
