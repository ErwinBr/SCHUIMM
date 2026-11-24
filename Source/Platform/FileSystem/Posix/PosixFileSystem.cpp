// POSIX implementation of FileSystem.h (macOS and Linux). Paths are passed to the kernel as
// UTF-8 bytes, which both systems treat as the native encoding.

#include "Core/Common/StringBuilder.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace schuimm::fs {

namespace {

Error systemError(int error, std::string_view operation, std::string_view path)
{
    ErrorCode code = ErrorCode::IoFailure;
    switch (error) {
    case ENOENT:
    case ENOTDIR: code = ErrorCode::NotFound; break;
    case EACCES:
    case EPERM: code = ErrorCode::PermissionDenied; break;
    case EEXIST: code = ErrorCode::AlreadyExists; break;
    default: break;
    }
    return Error{code, concat(operation, " '", path, "': ", std::strerror(error))};
}

FileType typeOf(mode_t mode)
{
    if (S_ISREG(mode)) {
        return FileType::Regular;
    }
    if (S_ISDIR(mode)) {
        return FileType::Directory;
    }
    return FileType::Other;
}

// Closes a file descriptor on scope exit.
class FileDescriptor {
public:
    explicit FileDescriptor(int fd) noexcept : fd_(fd) {}
    ~FileDescriptor()
    {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    [[nodiscard]] int get() const noexcept { return fd_; }

private:
    int fd_;
};

} // namespace

Result<FileInfo> fileInfo(std::string_view path)
{
    const std::string native(path);
    struct stat status {};
    if (::stat(native.c_str(), &status) != 0) {
        return std::unexpected(systemError(errno, "cannot inspect", path));
    }
    FileInfo info;
    info.type = typeOf(status.st_mode);
    info.size = info.type == FileType::Regular ? static_cast<std::uint64_t>(status.st_size) : 0;
    return info;
}

Result<std::vector<DirectoryEntry>> listDirectory(std::string_view path)
{
    const std::string native(path);
    DIR* directory = ::opendir(native.c_str());
    if (directory == nullptr) {
        return std::unexpected(systemError(errno, "cannot open directory", path));
    }
    std::vector<DirectoryEntry> entries;
    for (;;) {
        errno = 0;
        const dirent* entry = ::readdir(directory);
        if (entry == nullptr) {
            break;
        }
        const std::string_view name(entry->d_name);
        if (name == "." || name == "..") {
            continue;
        }
        DirectoryEntry result{std::string(name), FileType::Other};
        // d_type is not reliable on every file system; fall back to stat() when unknown.
        if (entry->d_type == DT_REG) {
            result.type = FileType::Regular;
        } else if (entry->d_type == DT_DIR) {
            result.type = FileType::Directory;
        } else {
            struct stat status {};
            const std::string full = native + "/" + result.name;
            if (::stat(full.c_str(), &status) == 0) {
                result.type = typeOf(status.st_mode);
            }
        }
        entries.push_back(std::move(result));
    }
    const int readError = errno;
    ::closedir(directory);
    if (readError != 0) {
        return std::unexpected(systemError(readError, "cannot read directory", path));
    }
    std::sort(entries.begin(), entries.end(),
              [](const DirectoryEntry& a, const DirectoryEntry& b) { return a.name < b.name; });
    return entries;
}

Result<std::vector<std::byte>> readFile(std::string_view path, std::uint64_t maxBytes)
{
    const std::string native(path);
    const FileDescriptor file(::open(native.c_str(), O_RDONLY | O_CLOEXEC));
    if (file.get() < 0) {
        return std::unexpected(systemError(errno, "cannot open file", path));
    }
    struct stat status {};
    if (::fstat(file.get(), &status) != 0) {
        return std::unexpected(systemError(errno, "cannot inspect", path));
    }
    if (!S_ISREG(status.st_mode)) {
        return makeError(S_ISDIR(status.st_mode) ? ErrorCode::InvalidArgument : ErrorCode::IoFailure,
                         concat("cannot read '", path, "': not a regular file"));
    }
    const auto size = static_cast<std::uint64_t>(status.st_size);
    if (size > maxBytes) {
        return makeError(ErrorCode::InvalidArgument,
                         concat("cannot read '", path, "': ", size, " bytes exceeds the limit of ", maxBytes));
    }
    std::vector<std::byte> data(static_cast<std::size_t>(size));
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t count = ::read(file.get(), data.data() + offset, data.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(systemError(errno, "cannot read", path));
        }
        if (count == 0) {
            data.resize(offset); // The file shrank while being read.
            break;
        }
        offset += static_cast<std::size_t>(count);
    }
    return data;
}

Status writeFile(std::string_view path, std::span<const std::byte> data)
{
    const std::string native(path);
    const FileDescriptor file(::open(native.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));
    if (file.get() < 0) {
        return std::unexpected(systemError(errno, "cannot create file", path));
    }
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t count = ::write(file.get(), data.data() + offset, data.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(systemError(errno, "cannot write", path));
        }
        offset += static_cast<std::size_t>(count);
    }
    return {};
}

Status createDirectory(std::string_view path)
{
    const std::string native(path);
    if (::mkdir(native.c_str(), 0755) == 0) {
        return {};
    }
    const int error = errno;
    if (error == EEXIST) {
        struct stat status {};
        if (::stat(native.c_str(), &status) == 0 && S_ISDIR(status.st_mode)) {
            return {};
        }
        return makeError(ErrorCode::AlreadyExists,
                         concat("cannot create directory '", path, "': a file of another type exists"));
    }
    return std::unexpected(systemError(error, "cannot create directory", path));
}

Status remove(std::string_view path)
{
    const std::string native(path);
    struct stat status {};
    if (::lstat(native.c_str(), &status) != 0) {
        return std::unexpected(systemError(errno, "cannot remove", path));
    }
    const int result = S_ISDIR(status.st_mode) ? ::rmdir(native.c_str()) : ::unlink(native.c_str());
    if (result != 0) {
        return std::unexpected(systemError(errno, "cannot remove", path));
    }
    return {};
}

Result<std::string> executablePath()
{
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string raw(size, '\0');
    if (_NSGetExecutablePath(raw.data(), &size) != 0) {
        return makeError(ErrorCode::PlatformFailure, "_NSGetExecutablePath failed");
    }
    raw.resize(std::strlen(raw.c_str()));
    // The dyld path may contain symbolic links or "..": resolve to the canonical file.
    char resolved[PATH_MAX] = {};
    if (::realpath(raw.c_str(), resolved) == nullptr) {
        return std::unexpected(systemError(errno, "cannot resolve executable path", raw));
    }
    return path::normalise(resolved);
#else
    std::string buffer(256, '\0');
    for (;;) {
        const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) {
            return std::unexpected(systemError(errno, "cannot read", "/proc/self/exe"));
        }
        if (static_cast<std::size_t>(length) < buffer.size()) {
            buffer.resize(static_cast<std::size_t>(length));
            return path::normalise(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
#endif
}

} // namespace schuimm::fs
