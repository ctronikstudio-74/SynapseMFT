#include "LDPFileOps.h"
#include <system_error>

namespace fs = std::filesystem;

bool LDPFileOps::rename(const std::string& old_path, const std::string& new_path, std::string& err) {
    std::error_code ec;
    fs::rename(old_path, new_path, ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    return true;
}

bool LDPFileOps::rmdir(const std::string& path, std::string& err) {
    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    return true;
}

bool LDPFileOps::chmod(const std::string& path, fs::perms permissions, std::string& err) {
    std::error_code ec;
    fs::permissions(path, permissions, ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    return true;
}

bool LDPFileOps::stat(const std::string& path, fs::file_status& status, std::string& err) {
    std::error_code ec;
    status = fs::status(path, ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    return true;
}

bool LDPFileOps::symlink(const std::string& target, const std::string& link, std::string& err) {
    std::error_code ec;
    fs::create_symlink(target, link, ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    return true;
}

