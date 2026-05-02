#pragma once

#include <sys/stat.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace aicam {
namespace fileutil {

inline bool directoryExists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

inline bool ensureDirectory(const std::string& path) {
    if (path.empty() || path == ".") {
        return true;
    }

    if (directoryExists(path)) {
        return true;
    }

    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        const char ch = path[i];
        current.push_back(ch);

        if (ch != '/' && i + 1 != path.size()) {
            continue;
        }

        if (!current.empty() && current != "/" && !directoryExists(current)) {
            if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                return false;
            }
        }
    }

    if (!directoryExists(path)) {
        if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }

    return true;
}

inline std::string parentPath(const std::string& file_path) {
    const size_t slash = file_path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return file_path.substr(0, slash);
}

inline bool ensureParentDirectory(const std::string& file_path) {
    return ensureDirectory(parentPath(file_path));
}

inline std::string strerrorString(int error_code) {
    const char* message = std::strerror(error_code);
    return message == nullptr ? "unknown error" : std::string(message);
}

}  // namespace fileutil
}  // namespace aicam
